// SPDX-License-Identifier: GPL-3.0-or-later
#include "chart_workbook.h"

#include "chart_edit.h"

#include <workbook.h>
#include <xlsx_bridge.h>

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>

#include <cmath>
#include <memory>

namespace QindaOffice::Show::ChartWorkbook {

namespace {

namespace Calc = QindaOffice::Calc;

struct Ref {
    QString sheet;
    Cell first;
    Cell last;
    bool ok = false;
    [[nodiscard]] bool vertical() const { return first.column == last.column && last.row > first.row; }
    [[nodiscard]] bool horizontal() const { return first.row == last.row && last.column > first.column; }
};

int columnIndex(const QString &letters)
{
    int n = 0;
    for (const QChar ch : letters) {
        n = n * 26 + (ch.toUpper().unicode() - u'A' + 1);
    }
    return n - 1;
}

QString columnLetters(int index)
{
    QString out;
    for (int n = index + 1; n > 0; n = (n - 1) / 26) {
        out.prepend(QChar(u'A' + (n - 1) % 26));
    }
    return out;
}

bool parseCell(const QString &text, Cell *cell)
{
    static const QRegularExpression kCell(QStringLiteral("^\\$?([A-Za-z]{1,3})\\$?([0-9]{1,7})$"));
    const QRegularExpressionMatch m = kCell.match(text.trimmed());
    if (!m.hasMatch()) {
        return false;
    }
    *cell = {m.captured(2).toInt() - 1, columnIndex(m.captured(1))};
    return cell->row >= 0 && cell->column >= 0;
}

// "Sheet1!$A$2:$A$5", "'My data'!$B$1", "[0]Sheet1!$B$2"; anything else
// (a defined name, a union) is not understood.
Ref parse(const QString &formula)
{
    Ref ref;
    QString text = formula.trimmed();
    if (text.startsWith(QLatin1Char('(')) && text.endsWith(QLatin1Char(')'))) {
        text = text.mid(1, text.size() - 2);
    }
    const qsizetype bang = text.lastIndexOf(QLatin1Char('!'));
    QString sheet = bang > 0 ? text.left(bang) : QString();
    const QString cells = bang > 0 ? text.mid(bang + 1) : text;
    if (sheet.size() >= 2 && sheet.startsWith(QLatin1Char('\'')) && sheet.endsWith(QLatin1Char('\''))) {
        sheet = sheet.mid(1, sheet.size() - 2).replace(QStringLiteral("''"), QStringLiteral("'"));
    }
    static const QRegularExpression kExternal(QStringLiteral("^\\[[0-9]+\\]"));
    sheet.remove(kExternal);
    const QStringList parts = cells.split(QLatin1Char(':'));
    if (parts.size() > 2 || !parseCell(parts.first(), &ref.first)) {
        return ref;
    }
    ref.last = ref.first;
    if (parts.size() == 2 && !parseCell(parts.last(), &ref.last)) {
        return ref;
    }
    ref.first = {qMin(ref.first.row, ref.last.row), qMin(ref.first.column, ref.last.column)};
    ref.last = {qMax(ref.first.row, ref.last.row), qMax(ref.first.column, ref.last.column)};
    ref.sheet = sheet;
    ref.ok = true;
    return ref;
}

QString quotedSheet(const QString &sheet)
{
    static const QRegularExpression kPlain(QStringLiteral("^[A-Za-z_][A-Za-z0-9_.]*$"));
    static const QRegularExpression kLooksLikeCell(QStringLiteral("^[A-Za-z]{1,3}[0-9]+$"));
    if (kPlain.match(sheet).hasMatch() && !kLooksLikeCell.match(sheet).hasMatch()) {
        return sheet;
    }
    QString escaped = sheet;
    return QLatin1Char('\'') + escaped.replace(QStringLiteral("'"), QStringLiteral("''")) + QLatin1Char('\'');
}

QString cellText(Cell cell)
{
    return QLatin1Char('$') + columnLetters(cell.column) + QLatin1Char('$') + QString::number(cell.row + 1);
}

// The block a table of data occupies: header row and category column
// (all the category levels) included.
Calc::CellRange blockOf(const Layout &layout, const ChartEdit::ChartTable &table)
{
    const int categories = int(qMax<qsizetype>(1, table.categories.size()));
    const int series = int(qMax<qsizetype>(1, table.series.size()));
    const Cell last = layout.valueCell(series - 1, categories - 1);
    const int outer = layout.levels - 1;
    const int top = qMin(layout.seriesInRows ? layout.headerRow - outer : layout.headerRow, layout.firstRow);
    const int left = qMin(layout.seriesInRows ? layout.categoryColumn : layout.categoryColumn - outer, layout.firstSeriesColumn);
    return {{qMax(0, top), qMax(0, left)}, {last.row, last.column}};
}

QByteArray fail(QString *error, const QString &message)
{
    if (error != nullptr) {
        *error = message;
    }
    return {};
}

} // namespace

Cell Layout::categoryCell(int category) const
{
    return seriesInRows ? Cell{headerRow, firstSeriesColumn + category} : Cell{firstRow + category, categoryColumn};
}

Cell Layout::levelCell(int level, int category) const
{
    return seriesInRows ? Cell{headerRow - level, firstSeriesColumn + category}
                        : Cell{firstRow + category, categoryColumn - level};
}

Cell Layout::nameCell(int series) const
{
    return seriesInRows ? Cell{firstRow + series, categoryColumn} : Cell{headerRow, firstSeriesColumn + series};
}

Cell Layout::valueCell(int series, int category) const
{
    return seriesInRows ? Cell{firstRow + series, firstSeriesColumn + category}
                        : Cell{firstRow + category, firstSeriesColumn + series};
}

Layout layoutFrom(const QString &nameFormula, const QString &categoryFormula, const QString &valueFormula)
{
    Layout layout;
    const Ref values = parse(valueFormula);
    if (!values.ok) {
        return layout;
    }
    const Ref categories = parse(categoryFormula);
    const Ref name = parse(nameFormula);
    if (!values.sheet.isEmpty()) {
        layout.sheet = values.sheet;
    }
    bool inRows = values.horizontal();
    // M82: multi-level categories span several columns (or rows) — the
    // values say which way the series run.
    const bool levelled = categories.ok && categories.first.row != categories.last.row
        && categories.first.column != categories.last.column;
    if (levelled) {
        inRows = values.horizontal();
    } else if (categories.ok && (categories.vertical() || categories.horizontal())) {
        inRows = categories.horizontal();
    } else if (categories.ok) {
        inRows = categories.first.column == values.first.column; // one category: same column → across
    }
    layout.seriesInRows = inRows;
    if (!inRows) {
        layout.firstRow = values.first.row;
        layout.firstSeriesColumn = values.first.column;
        layout.categoryColumn = categories.ok ? categories.last.column : qMax(0, values.first.column - 1);
        layout.headerRow = name.ok ? name.first.row : values.first.row - 1;
        layout.levels = levelled ? categories.last.column - categories.first.column + 1 : 1;
    } else {
        layout.firstRow = values.first.row;
        layout.firstSeriesColumn = values.first.column;
        layout.headerRow = categories.ok ? categories.last.row : qMax(0, values.first.row - 1);
        layout.categoryColumn = name.ok ? name.first.column : values.first.column - 1;
        layout.levels = levelled ? categories.last.row - categories.first.row + 1 : 1;
    }
    return layout;
}

QString reference(const QString &sheet, Cell first, Cell last)
{
    QString out = quotedSheet(sheet) + QLatin1Char('!') + cellText(first);
    if (first.row != last.row || first.column != last.column) {
        out += QLatin1Char(':') + cellText(last);
    }
    return out;
}

QByteArray write(const QByteArray &source, const ChartData &chart, const ChartData &previous, const Layout &layout,
                 QString *error)
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        return fail(error, QStringLiteral("no temporary directory for the chart's workbook"));
    }
    auto book = std::make_unique<Calc::Workbook>();
    if (!source.isEmpty()) {
        const QString in = dir.filePath(QStringLiteral("embedded.xlsx"));
        QFile file(in);
        bool read = file.open(QIODevice::WriteOnly) && file.write(source) == source.size();
        file.close();
        read = read && Calc::IO::readXlsxFile(in, book.get());
        if (!read) {
            book = std::make_unique<Calc::Workbook>(); // not an xlsx calc_io reads: a new one
        }
    }
    qint32 sheet = book->sheetIndex(layout.sheet);
    if (sheet < 0) {
        book->addSheet(layout.sheet);
        sheet = book->sheetCount() - 1;
    }
    const ChartEdit::ChartTable now = ChartEdit::tableOf(chart);
    const ChartEdit::ChartTable was = ChartEdit::tableOf(previous);
    const bool numericCategories = layout.categoriesNumeric || chart.type == u"scatter";
    QSet<quint64> written;
    const auto put = [&](Cell cell, const Calc::Value &value) {
        if (cell.row < 0 || cell.column < 0) {
            return;
        }
        const Calc::CellAddr addr{cell.row, cell.column};
        book->setCellValue(sheet, addr, value);
        written.insert(addr.key());
    };
    const auto text = [](const QString &s) { return s.isEmpty() ? Calc::Value() : Calc::Value(s); };
    book->beginBulkLoad();
    for (int k = 0; k < now.categories.size(); ++k) {
        const qreal number = ChartEdit::parseValue(now.categories.at(k));
        put(layout.categoryCell(k), numericCategories && !std::isnan(number) ? Calc::Value(double(number)) : text(now.categories.at(k)));
        for (int level = 1; level < layout.levels; ++level) {
            put(layout.levelCell(level, k), text(chart.categoryLevels.value(level - 1).value(k))); // M82
        }
    }
    for (int s = 0; s < now.series.size(); ++s) {
        put(layout.nameCell(s), text(now.series.at(s)));
        for (int k = 0; k < now.categories.size(); ++k) {
            const qreal v = now.values.at(s).value(k, std::nan(""));
            put(layout.valueCell(s, k), std::isnan(v) ? Calc::Value() : Calc::Value(double(v)));
        }
    }
    // What the previous data held and the new data does not reach empties.
    const auto clear = [&](Cell cell) {
        const Calc::CellAddr addr{cell.row, cell.column};
        if (cell.row >= 0 && cell.column >= 0 && !written.contains(addr.key())) {
            book->setCellValue(sheet, addr, Calc::Value());
        }
    };
    for (int k = 0; k < was.categories.size(); ++k) {
        clear(layout.categoryCell(k));
        for (int level = 1; level < layout.levels; ++level) {
            clear(layout.levelCell(level, k));
        }
    }
    for (int s = 0; s < was.series.size(); ++s) {
        clear(layout.nameCell(s));
        for (int k = 0; k < was.categories.size(); ++k) {
            clear(layout.valueCell(s, k));
        }
    }
    // PowerPoint keeps its chart data in an Excel table: it follows the data.
    Calc::Sheet &data = book->sheetAt(sheet);
    QList<Calc::TableSpec> tables = data.tables();
    const Calc::CellRange oldBlock = blockOf(layout, was);
    bool moved = false;
    for (Calc::TableSpec &table : tables) {
        if (table.range == oldBlock) {
            table.range = blockOf(layout, now);
            table.columnNames.clear(); // calc_io names them from the header cells
            moved = true;
        }
    }
    if (moved) {
        data.setTables(tables);
    }
    book->endBulkLoad();
    const QString out = dir.filePath(QStringLiteral("chart.xlsx"));
    QString writeError;
    if (!Calc::IO::writeXlsxFile(*book, out, &writeError)) {
        return fail(error, writeError);
    }
    QFile file(out);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(error, file.errorString());
    }
    return file.readAll();
}

} // namespace QindaOffice::Show::ChartWorkbook
