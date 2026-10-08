// SPDX-License-Identifier: GPL-3.0-or-later
// Workbook-level pieces of an xlsx save (xlsx_write_book.h, M61, D-069).

#include "xlsx_write_book.h"

#include "xlsx_formula.h"

#include <QSet>

#include <algorithm>

namespace QindaOffice::Calc::IO {

namespace {

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

} // namespace QindaOffice::Calc::IO
