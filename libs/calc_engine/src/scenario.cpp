// SPDX-License-Identifier: GPL-3.0-or-later
// Scenario Manager (scenario.h, M78, D-087): the values a scenario keeps,
// Excel's checks, Show, and the Scenario Summary report — worked out by
// showing each scenario in turn and putting every changing cell's own
// content back afterwards (what_if_internal.h), formulas included.

#include "scenario.h"

#include "sheet.h"
#include "what_if_internal.h"
#include "workbook.h"

#include <QSet>

namespace QindaOffice::Calc {

namespace {

bool validSheet(const Workbook &book, qint32 sheet)
{
    return sheet >= 0 && sheet < book.sheetCount();
}

// A value as <inputCells val> keeps it.
QString scenarioText(const Value &value)
{
    if (const auto *number = std::get_if<double>(&value)) {
        return WhatIf::numberText(*number);
    }
    if (const auto *flag = std::get_if<bool>(&value)) {
        return *flag ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    }
    return toText(value); // text as it is, an error's spelling, "" for a blank
}

// One value into its cell, the way typing it would put it there.
void enterValue(Workbook &book, qint32 sheet, const CellAddr &addr, const QString &value)
{
    if (value.isEmpty()) {
        book.setCellValue(sheet, addr, Value());
        return;
    }
    static_cast<void>(book.setCellInput(sheet, addr, value));
}

// What the report calls a cell: the defined name that names exactly it, as
// Excel does, else its absolute address.
QString cellLabel(const Workbook &book, qint32 sheet, const CellAddr &addr)
{
    const QString sheetName = book.sheetName(sheet);
    const QHash<QString, RangeRef> &names = book.namedRanges();
    QStringList matches;
    for (auto it = names.constBegin(); it != names.constEnd(); ++it) {
        const RangeRef &ref = it.value();
        const bool here = ref.sheet.isEmpty() || unquotedSheetName(ref.sheet).compare(sheetName, Qt::CaseInsensitive) == 0;
        if (here && ref.isSingleCell() && ref.range.topLeft == addr) {
            matches.append(it.key());
        }
    }
    if (!matches.isEmpty()) {
        matches.sort(Qt::CaseInsensitive); // a stable choice when several names fit
        return matches.first();
    }
    return addrText(addr, true, true);
}

} // namespace

QStringList scenarioInputs(const Workbook &book, qint32 sheet, const QList<CellAddr> &cells)
{
    QStringList out;
    for (const CellAddr &cell : cells) {
        out.append(validSheet(book, sheet) ? scenarioText(book.cellValue(sheet, cell.row, cell.col)) : QString());
    }
    return out;
}

QString scenarioProblem(const ScenarioSet &set, const Scenario &scenario, qint32 ignore)
{
    const QString name = scenario.name.trimmed();
    if (name.isEmpty()) {
        return QStringLiteral("Enter a name for the scenario.");
    }
    for (qsizetype i = 0; i < set.scenarios.size(); ++i) {
        if (i != ignore && set.scenarios.at(i).name.trimmed().compare(name, Qt::CaseInsensitive) == 0) {
            return QStringLiteral("Scenario name already exists.");
        }
    }
    if (scenario.cells.isEmpty()) {
        return QStringLiteral("Changing cells reference is not valid.");
    }
    for (const CellAddr &cell : scenario.cells) {
        if (!cell.isValid()) {
            return QStringLiteral("Changing cells reference is not valid.");
        }
    }
    if (scenario.cells.size() > kMaxScenarioCells) {
        return QStringLiteral("Too many changing cells.");
    }
    if (scenario.values.size() != scenario.cells.size()) {
        return QStringLiteral("Enter a value for each changing cell.");
    }
    return {};
}

bool showScenario(Workbook &book, qint32 sheet, qint32 index, QString *error)
{
    if (!validSheet(book, sheet) || index < 0 || index >= book.sheetAt(sheet).scenarios().scenarios.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("There is no such scenario on this sheet.");
        }
        return false;
    }
    ScenarioSet set = book.sheetAt(sheet).scenarios();
    const Scenario &scenario = set.scenarios.at(index);
    book.beginBatch(); // one recalculation for every cell the scenario sets
    for (qsizetype i = 0; i < scenario.cells.size(); ++i) {
        enterValue(book, sheet, scenario.cells.at(i), scenario.values.value(i));
    }
    book.endBatch();
    set.current = index;
    set.shown = index;
    book.sheetAt(sheet).setScenarios(set);
    return true;
}

ScenarioSummary scenarioSummary(Workbook &book, qint32 sheet, const QList<CellRange> &resultCells)
{
    ScenarioSummary out;
    if (!validSheet(book, sheet)) {
        return out;
    }
    const ScenarioSet set = book.sheetAt(sheet).scenarios();
    out.names.append(QStringLiteral("Current Values"));
    QSet<quint64> seen;
    for (const Scenario &scenario : set.scenarios) {
        out.names.append(scenario.name);
        for (const CellAddr &cell : scenario.cells) {
            if (!seen.contains(cell.key())) {
                seen.insert(cell.key());
                out.changing.append(cell);
            }
        }
    }
    QSet<quint64> listed;
    for (const CellRange &range : resultCells) {
        for (qint32 r = range.topLeft.row; range.isValid() && r <= range.bottomRight.row; ++r) {
            for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
                const CellAddr addr{r, c};
                if (!listed.contains(addr.key())) {
                    listed.insert(addr.key());
                    out.results.append(addr);
                }
            }
        }
    }
    QList<WhatIf::SavedCell> saved;
    for (const CellAddr &cell : std::as_const(out.changing)) {
        saved.append(WhatIf::saveCell(book, sheet, cell));
    }
    const auto readColumn = [&book, sheet, &out] {
        QList<Value> column;
        for (const CellAddr &cell : out.changing + out.results) {
            column.append(book.cellValue(sheet, cell.row, cell.col));
        }
        return column;
    };
    const auto restoreAll = [&book, sheet, &saved] {
        QSet<CellKey> keys;
        for (const WhatIf::SavedCell &cell : std::as_const(saved)) {
            keys.insert(WhatIf::restoreCell(book, sheet, cell));
        }
        book.recalc(keys);
    };
    out.values.append(readColumn());
    out.comments.append(QString());
    out.setBy.append(QList<CellAddr>());
    bool changed = false;
    for (const Scenario &scenario : set.scenarios) {
        if (changed) {
            restoreAll(); // a cell this scenario leaves alone shows its current value
        }
        book.beginBatch();
        for (qsizetype i = 0; i < scenario.cells.size(); ++i) {
            enterValue(book, sheet, scenario.cells.at(i), scenario.values.value(i));
        }
        book.endBatch();
        changed = true;
        out.values.append(readColumn());
        out.comments.append(scenario.comment);
        out.setBy.append(scenario.cells);
    }
    if (changed) {
        restoreAll();
    }
    return out;
}

void writeScenarioSummary(Workbook &book, qint32 target, qint32 source, const ScenarioSummary &summary)
{
    if (!validSheet(book, target) || !validSheet(book, source) || summary.names.isEmpty()) {
        return;
    }
    // Styles, made once: the title, the header row, and the values a
    // scenario sets (Excel highlights them in grey), each keeping the number
    // format of the cell it came from.
    QHash<QString, qint32> styles;
    const auto styleFor = [&book, &styles](bool bold, std::optional<Rgb> fill, const QString &numberFormat) {
        const QString key = QStringLiteral("%1|%2|%3")
                                .arg(int(bold))
                                .arg(fill.has_value() ? (fill->r << 16) | (fill->g << 8) | fill->b : -1)
                                .arg(numberFormat);
        if (const auto found = styles.constFind(key); found != styles.constEnd()) {
            return found.value();
        }
        CellStyle style;
        style.bold = bold;
        style.bg = fill;
        style.numberFormat = numberFormat.isEmpty() ? QStringLiteral("General") : numberFormat;
        const qint32 id = book.addStyle(style);
        styles.insert(key, id);
        return id;
    };
    const auto formatOf = [&book, source](const CellAddr &cell) {
        const CellStyle *style = book.style(book.sheetAt(source).styleIdAt(cell));
        return style != nullptr ? style->numberFormat : QString();
    };
    const Rgb header{217, 217, 217};
    const Rgb changedFill{191, 191, 191};
    Sheet &out = book.sheetAt(target);
    // The longest text per column, for the widths below (the notes spill
    // over and are not measured).
    QHash<qint32, qsizetype> longest;
    const auto put = [&book, &out, &longest, target](const CellAddr &addr, const Value &value, qint32 style,
                                                     bool measure = true) {
        book.setCellValue(target, addr, value);
        if (style > 0) {
            out.setStyleIdAt(addr, style);
        }
        if (measure) {
            const qsizetype length = isNumber(value) ? QString::number(std::get<double>(value), 'g', 12).size() : toText(value).size();
            longest[addr.col] = qMax(longest.value(addr.col), length);
        }
    };
    const qint32 firstColumn = 3; // column D: Current Values, then one per scenario
    const qsizetype columns = summary.names.size();
    book.beginBatch();
    put({1, 1}, QStringLiteral("Scenario Summary"), styleFor(true, header, QString()));
    for (qsizetype i = 0; i < columns; ++i) {
        const QString name = i == 0 ? summary.names.at(0) + QLatin1Char(':') : summary.names.at(i);
        put({2, firstColumn + qint32(i)}, name, styleFor(true, header, QString()));
    }
    put({2, 1}, Value(), styleFor(true, header, QString()));
    put({2, 2}, Value(), styleFor(true, header, QString()));
    // Row 4 is Excel's comment row ("Created by … on …"), under the names.
    qint32 row = 3;
    for (qsizetype i = 1; i < columns; ++i) {
        if (!summary.comments.value(i).isEmpty()) {
            put({row, firstColumn + qint32(i)}, summary.comments.value(i), 0);
        }
    }
    ++row;
    const auto block = [&](const QString &title, const QList<CellAddr> &cells, qsizetype offset, bool highlight) {
        put({row++, 1}, title, styleFor(true, std::nullopt, QString()));
        for (qsizetype k = 0; k < cells.size(); ++k, ++row) {
            const CellAddr &cell = cells.at(k);
            put({row, 2}, cellLabel(book, source, cell), 0);
            const QString format = formatOf(cell);
            for (qsizetype i = 0; i < columns && i < summary.values.size(); ++i) {
                // Grey where the column's scenario sets the cell (Excel's note).
                const bool grey = highlight && summary.setBy.value(i).contains(cell);
                put({row, firstColumn + qint32(i)}, summary.values.at(i).value(offset + k),
                    styleFor(false, grey ? std::optional<Rgb>(changedFill) : std::nullopt, format));
            }
        }
    };
    block(QStringLiteral("Changing Cells:"), summary.changing, 0, true);
    if (!summary.results.isEmpty()) {
        block(QStringLiteral("Result Cells:"), summary.results, summary.changing.size(), false);
    }
    put({row++, 1}, QStringLiteral("Notes:  Current Values column represents values of changing cells at"), 0, false);
    put({row++, 1}, QStringLiteral("time Scenario Summary Report was created.  Changing cells for each"), 0, false);
    put({row, 1}, QStringLiteral("scenario are highlighted in gray."), 0, false);
    book.endBatch();
    // Excel fits the report's columns to their text; estimated from the
    // text as a PivotTable's are (the app's font metrics are not here).
    out.setColumnWidth(0, qMin(out.columnWidth(0), 24.0)); // the margin column
    for (auto it = longest.constBegin(); it != longest.constEnd(); ++it) {
        const qreal wanted = qMin<qreal>(300.0, qreal(it.value()) * 7.0 + 14.0);
        if (wanted > out.columnWidth(it.key())) {
            out.setColumnWidth(it.key(), wanted);
        }
    }
}

} // namespace QindaOffice::Calc
