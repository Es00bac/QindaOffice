// SPDX-License-Identifier: GPL-3.0-or-later
// Workbook-level pieces of an xlsx save (xlsx_write_book.h, M61, D-069).

#include "xlsx_write_book.h"

#include "xlsx_formula.h"

#include "structured_ref.h"

#include <QSet>

#include <algorithm>

namespace QindaOffice::Calc::IO {

namespace {

// The totals row functions Excel names, by the SUBTOTAL code each writes.
QString totalsFunctionFor(int code)
{
    switch (code) {
    case 101: return QStringLiteral("average");
    case 102: return QStringLiteral("countNums");
    case 103: return QStringLiteral("count");
    case 104: return QStringLiteral("max");
    case 105: return QStringLiteral("min");
    case 107: return QStringLiteral("stdDev");
    case 109: return QStringLiteral("sum");
    case 110: return QStringLiteral("var");
    default: return {};
    }
}

// What the totals row cell of column `i` says (M69, D-078).
PatchManifest::TablePart::Column totalsColumn(const Workbook &wb, qint32 sheet, const TableSpec &table, qint32 i)
{
    PatchManifest::TablePart::Column column;
    const Cell *cell = wb.sheetAt(sheet).cellAt({table.range.bottomRight.row, table.range.topLeft.col + i});
    if (cell == nullptr) {
        return column;
    }
    if (!cell->ast) {
        column.totalsLabel = isText(cell->value) ? std::get<QString>(cell->value) : QString();
        return column;
    }
    const NodePtr &ast = cell->ast;
    if (ast->kind == Node::Kind::Call && ast->text == QLatin1String("SUBTOTAL") && ast->args.size() == 2
        && ast->args.at(0)->kind == Node::Kind::Number && ast->args.at(1)->kind == Node::Kind::StructRef) {
        const StructRef &ref = *ast->args.at(1)->structRef;
        const bool own = (ref.table.isEmpty() || ref.table.compare(table.name, Qt::CaseInsensitive) == 0)
            && ref.items == 0 && ref.lastColumn.isEmpty()
            && ref.firstColumn.trimmed().compare(table.columnNames.value(i).trimmed(), Qt::CaseInsensitive) == 0;
        const QString function = totalsFunctionFor(int(ast->args.at(0)->number));
        if (own && !function.isEmpty()) {
            column.totalsFunction = function;
            return column;
        }
    }
    column.totalsFunction = QStringLiteral("custom");
    column.totalsFormula = formulaForFile(cell->formula, table.name);
    return column;
}

// "'Q1 Sales'!$A$1:$D$20" for a print area.
QString areaText(const QString &sheet, const CellRange &range)
{
    return quotedSheetName(sheet) + QLatin1Char('!') + addrText(range.topLeft, true, true) + QLatin1Char(':')
        + addrText(range.bottomRight, true, true);
}

} // namespace

QList<PatchManifest::DefinedNameEntry> definedNamesForFile(const Workbook &wb)
{
    QList<PatchManifest::DefinedNameEntry> out;
    QSet<QString> taken; // "scope|NAME": Excel allows one name per scope
    const auto add = [&out, &taken](PatchManifest::DefinedNameEntry entry) {
        const QString key = QString::number(entry.localSheetId) + QLatin1Char('|') + entry.name.toUpper();
        if (!entry.body.isEmpty() && !taken.contains(key)) {
            taken.insert(key);
            out.append(std::move(entry));
        }
    };
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        const Sheet &sheet = wb.sheetAt(s);
        if (sheet.printArea().has_value()) {
            add({QStringLiteral("_xlnm.Print_Area"), s, areaText(sheet.name(), *sheet.printArea()), false, {}});
        }
        QStringList titles;
        const QString quoted = quotedSheetName(sheet.name());
        if (sheet.printTitleCols().has_value()) {
            titles.append(QStringLiteral("%1!$%2:$%3")
                              .arg(quoted, colName(sheet.printTitleCols()->topLeft.col),
                                   colName(sheet.printTitleCols()->bottomRight.col)));
        }
        if (sheet.printTitleRows().has_value()) {
            titles.append(QStringLiteral("%1!$%2:$%3")
                              .arg(quoted)
                              .arg(sheet.printTitleRows()->topLeft.row + 1)
                              .arg(sheet.printTitleRows()->bottomRight.row + 1));
        }
        if (!titles.isEmpty()) {
            add({QStringLiteral("_xlnm.Print_Titles"), s, titles.join(QLatin1Char(',')), false, {}});
        }
    }
    // The workbook range table is a hash: sorted, so a save is deterministic.
    QStringList rangeNames = wb.namedRanges().keys();
    std::sort(rangeNames.begin(), rangeNames.end());
    for (const QString &name : std::as_const(rangeNames)) {
        add({name, -1, rangeRefText(wb.namedRanges().value(name)), false, {}});
    }
    for (const DefinedName &name : wb.definedNames()) {
        if (name.sheet >= wb.sheetCount()) {
            continue; // scoped to a sheet that no longer exists
        }
        add({name.name, name.sheet, formulaForFile(name.formula), name.hidden, name.comment});
    }
    return out;
}

TableSpec tableForFile(const Workbook &wb, qint32 sheet, const TableSpec &table, QHash<quint64, QString> *headerText)
{
    TableSpec out = table;
    out.columnNames.clear();
    const qint32 columns = table.range.bottomRight.col - table.range.topLeft.col + 1;
    QSet<QString> seen;
    for (qint32 i = 0; i < columns; ++i) {
        const CellAddr header{table.range.topLeft.row, table.range.topLeft.col + i};
        const Value value = table.headerRow ? wb.cellValue(sheet, header.row, header.col) : Value();
        QString name = table.headerRow ? (isBlank(value) ? QString() : toText(value)) : table.columnNames.value(i);
        if (name.isEmpty()) {
            name = QStringLiteral("Column%1").arg(i + 1);
        }
        QString unique = name;
        for (int n = 2; seen.contains(unique.toUpper()); ++n) {
            unique = name + QString::number(n);
        }
        seen.insert(unique.toUpper());
        out.columnNames.append(unique);
        const bool matches = isText(value) && std::get<QString>(value) == unique;
        if (table.headerRow && !matches) {
            headerText->insert(header.key(), unique);
        }
    }
    return out;
}

PatchManifest::TablePart tablePartForFile(const Workbook &wb, qint32 sheet, const TableSpec &table,
                                          QHash<quint64, QString> *headerText)
{
    PatchManifest::TablePart part;
    part.spec = tableForFile(wb, sheet, table, headerText);
    const qint32 columns = table.range.bottomRight.col - table.range.topLeft.col + 1;
    const qint32 firstData = table.range.topLeft.row + (table.headerRow ? 1 : 0);
    for (qint32 i = 0; i < columns; ++i) {
        PatchManifest::TablePart::Column column = table.totalsRow ? totalsColumn(wb, sheet, part.spec, i)
                                                                  : PatchManifest::TablePart::Column{};
        if (!table.calculatedColumns.value(i).isEmpty()) {
            const Cell *first = wb.sheetAt(sheet).cellAt({firstData, table.range.topLeft.col + i});
            const QString text = first != nullptr && first->ast ? first->formula : table.calculatedColumns.at(i);
            column.calculatedFormula = formulaForFile(text, table.name);
        }
        part.columns.append(column);
    }
    return part;
}

} // namespace QindaOffice::Calc::IO
