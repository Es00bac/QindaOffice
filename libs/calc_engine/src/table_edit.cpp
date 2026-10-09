// SPDX-License-Identifier: GPL-3.0-or-later
// Excel tables as they are edited (table_edit.h, M69, D-078).

#include "table_edit.h"

#include "structured_ref.h"
#include "table_refs.h"
#include "workbook.h"

#include <QSet>

namespace QindaOffice::Calc {

namespace {

qsizetype tableIndexAt(const Sheet &sheet, const CellAddr &addr)
{
    const QList<TableSpec> &tables = sheet.tables();
    for (qsizetype i = 0; i < tables.size(); ++i) {
        if (tables.at(i).range.contains(addr)) {
            return i;
        }
    }
    return -1;
}

qint32 widthOf(const TableSpec &table)
{
    return table.range.bottomRight.col - table.range.topLeft.col + 1;
}

// The name a header's text gives a column among the others' (upper-case)
// names: Column<N> for nothing, a number added to a duplicate (Excel's).
QString uniqueColumnName(const QString &wanted, const QSet<QString> &taken)
{
    const QString base = wanted.isEmpty() ? QStringLiteral("Column") : wanted;
    if (!wanted.isEmpty() && !taken.contains(wanted.toUpper())) {
        return wanted;
    }
    for (int n = wanted.isEmpty() ? 1 : 2;; ++n) {
        const QString name = base + QString::number(n);
        if (!taken.contains(name.toUpper())) {
            return name;
        }
    }
}

bool sameName(const QString &a, const QString &b)
{
    return a.trimmed().compare(b.trimmed(), Qt::CaseInsensitive) == 0;
}

// Every structured reference to column `from` of `table` names `to`.
void renameColumnRefs(Workbook &book, const QString &table, const QString &from, const QString &to)
{
    rewriteStructRefs(book, [&](const StructRef &ref, const QString &tableName) -> NodePtr {
        if (tableName.compare(table, Qt::CaseInsensitive) != 0) {
            return nullptr;
        }
        const bool first = !ref.firstColumn.isEmpty() && sameName(ref.firstColumn, from);
        const bool last = !ref.lastColumn.isEmpty() && sameName(ref.lastColumn, from);
        if (!first && !last) {
            return nullptr;
        }
        StructRef renamed = ref;
        if (first) {
            renamed.firstColumn = to;
        }
        if (last) {
            renamed.lastColumn = to;
        }
        return Node::makeStructRef(renamed);
    });
}

bool hasContent(const Sheet &sheet, const CellAddr &addr)
{
    const Cell *cell = sheet.cellAt(addr);
    return cell != nullptr && (cell->ast || !isBlank(cell->value));
}

// Grows a table over `addr` when it is the row just below it (no totals
// row) or the column just right of it — Excel's auto-expansion.
bool expandTableOver(Workbook &book, qint32 sheetIndex, const CellAddr &addr)
{
    Sheet &sheet = book.sheetAt(sheetIndex);
    if (sheet.tableCovering(addr) != nullptr || sheet.mergeCovering(addr) != nullptr) {
        return false;
    }
    QList<TableSpec> tables = sheet.tables();
    for (TableSpec &table : tables) {
        const CellRange &r = table.range;
        const bool below = !table.totalsRow && addr.row == r.bottomRight.row + 1 && addr.col >= r.topLeft.col
            && addr.col <= r.bottomRight.col;
        const bool right = addr.col == r.bottomRight.col + 1 && addr.row >= r.topLeft.row
            && addr.row <= r.bottomRight.row;
        if (!below && !right) {
            continue;
        }
        // The new row or column must not run into another table.
        CellRange grown = r;
        (below ? grown.bottomRight.row : grown.bottomRight.col) += 1;
        for (const TableSpec &other : sheet.tables()) {
            if (other.name != table.name && other.range.topLeft.row <= grown.bottomRight.row
                && other.range.bottomRight.row >= grown.topLeft.row && other.range.topLeft.col <= grown.bottomRight.col
                && other.range.bottomRight.col >= grown.topLeft.col) {
                return false;
            }
        }
        table.range = grown;
        if (right) {
            table.columnNames.resize(widthOf(table) - 1);
            table.columnNames.append(QString()); // named from its header by syncTableHeaders
            table.calculatedColumns.resize(widthOf(table));
        }
        const TableSpec grownTable = table;
        sheet.setTables(tables);
        if (below) {
            fillCalculatedCells(book, sheetIndex, grownTable, {addr.row}, addr.col);
        }
        return true;
    }
    return false;
}

// A formula typed into a data cell of an empty or calculated column fills
// the column (Excel's calculated columns).
bool applyCalculatedColumn(Workbook &book, qint32 sheetIndex, const CellAddr &addr)
{
    Sheet &sheet = book.sheetAt(sheetIndex);
    const qsizetype index = tableIndexAt(sheet, addr);
    const Cell *typed = sheet.cellAt(addr);
    if (index < 0 || typed == nullptr || !typed->ast || typed->arrayRange.isValid()) {
        return false;
    }
    TableSpec table = sheet.tables().at(index);
    const CellRange data = tableDataRows(table);
    if (addr.row < data.topLeft.row || addr.row > data.bottomRight.row) {
        return false;
    }
    const qint32 column = addr.col - table.range.topLeft.col;
    const bool calculated = !table.calculatedColumns.value(column).isEmpty();
    bool empty = true;
    for (qint32 row = data.topLeft.row; empty && row <= data.bottomRight.row; ++row) {
        empty = row == addr.row || !hasContent(sheet, {row, addr.col});
    }
    if (!calculated && !empty) {
        return false; // a formula among values: just this cell, as in Excel
    }
    const NodePtr ast = typed->ast;
    const bool dynamic = typed->dynamicArray;
    book.beginBatch();
    for (qint32 row = data.topLeft.row; row <= data.bottomRight.row; ++row) {
        if (row != addr.row) {
            const NodePtr shifted = shiftNode(ast, row - addr.row, 0);
            book.installFormula(sheetIndex, {row, addr.col}, shifted, nodeText(shifted), dynamic);
        }
    }
    book.endBatch();
    table.calculatedColumns.resize(widthOf(table));
    table.calculatedColumns[column] = nodeText(shiftNode(ast, data.topLeft.row - addr.row, 0));
    QList<TableSpec> tables = book.sheetAt(sheetIndex).tables();
    tables[index] = table;
    book.sheetAt(sheetIndex).setTables(tables);
    return true;
}

} // namespace

QString nextTableName(const Workbook &book)
{
    for (int n = 1;; ++n) {
        const QString name = QStringLiteral("Table%1").arg(n);
        qint32 sheet = -1;
        bool taken = book.tableNamed(name, &sheet) != nullptr || book.namedRange(name).has_value();
        for (const DefinedName &defined : book.definedNames()) {
            taken = taken || defined.name.compare(name, Qt::CaseInsensitive) == 0;
        }
        if (!taken) {
            return name;
        }
    }
}

void fillCalculatedCells(Workbook &book, qint32 sheetIndex, const TableSpec &table, const QList<qint32> &rows,
                         qint32 skipCol)
{
    const Sheet &sheet = book.sheetAt(sheetIndex);
    const CellRange data = tableDataRows(table);
    struct Fill {
        CellAddr at;
        NodePtr ast;
        bool dynamic = false;
    };
    QList<Fill> fills;
    for (qint32 i = 0; i < widthOf(table); ++i) {
        const qint32 col = table.range.topLeft.col + i;
        if (col == skipCol || table.calculatedColumns.value(i).isEmpty()) {
            continue;
        }
        // The nearest data row of the column holding a formula, shifted —
        // so relative references mean the same thing on the new row; the
        // stored formula (relative to the first data row) otherwise.
        NodePtr source;
        qint32 sourceRow = data.topLeft.row;
        bool dynamic = false;
        for (qint32 row = data.topLeft.row; row <= data.bottomRight.row; ++row) {
            const Cell *cell = sheet.cellAt({row, col});
            if (!rows.contains(row) && cell != nullptr && cell->ast) {
                source = cell->ast;
                sourceRow = row;
                dynamic = cell->dynamicArray;
                break;
            }
        }
        if (!source) {
            const ParseResult parsed = parseFormula(table.calculatedColumns.at(i));
            if (!parsed.root || !parsed.error.isEmpty()) {
                continue;
            }
            source = parsed.root;
        }
        for (const qint32 row : rows) {
            if (row >= data.topLeft.row && row <= data.bottomRight.row) {
                fills.append({{row, col}, shiftNode(source, row - sourceRow, 0), dynamic});
            }
        }
    }
    book.beginBatch();
    for (const Fill &fill : std::as_const(fills)) {
        book.installFormula(sheetIndex, fill.at, fill.ast, nodeText(fill.ast), fill.dynamic);
    }
    book.endBatch();
}

bool syncTableHeaders(Workbook &book, qint32 sheetIndex)
{
    bool changed = false;
    const qsizetype count = book.sheetAt(sheetIndex).tables().size();
    for (qsizetype t = 0; t < count; ++t) {
        TableSpec table = book.sheetAt(sheetIndex).tables().at(t);
        if (!table.headerRow) {
            continue;
        }
        const qint32 width = widthOf(table);
        table.columnNames.resize(width);
        for (qint32 i = 0; i < width; ++i) {
            const CellAddr header{table.range.topLeft.row, table.range.topLeft.col + i};
            const Value value = book.cellValue(sheetIndex, header.row, header.col);
            const QString text = isBlank(value) || isErr(value) ? QString() : toText(value);
            const QString old = table.columnNames.at(i);
            if (!text.isEmpty() && text == old && isText(value)) {
                continue;
            }
            QSet<QString> taken;
            for (qint32 j = 0; j < width; ++j) {
                if (j != i) {
                    taken.insert(table.columnNames.at(j).toUpper());
                }
            }
            const QString name = uniqueColumnName(text, taken);
            if (!(value == Value(name))) {
                // Excel keeps a header as text: a blank, a duplicate, a
                // number or a formula there becomes the column's name.
                book.setCellValue(sheetIndex, header, name);
            }
            table.columnNames[i] = name;
            QList<TableSpec> tables = book.sheetAt(sheetIndex).tables();
            tables[t] = table;
            book.sheetAt(sheetIndex).setTables(tables);
            if (!old.isEmpty() && old != name) {
                renameColumnRefs(book, table.name, old, name);
                // The rewrite may have touched this table's calculated
                // column formulas: carry on from what the sheet holds.
                table = book.sheetAt(sheetIndex).tables().at(t);
            }
            changed = true;
        }
    }
    if (changed) {
        book.refreshStructuredRefs();
    }
    return changed;
}

bool setTableAwareInput(Workbook &book, qint32 sheetIndex, const CellAddr &addr, const QString &input)
{
    const bool grew = !input.isEmpty() && expandTableOver(book, sheetIndex, addr);
    const bool ok = book.setCellInput(sheetIndex, addr, input);
    const TableSpec *table = book.sheetAt(sheetIndex).tableCovering(addr);
    bool changed = grew;
    if (table != nullptr) {
        const bool header = table->headerRow && addr.row == table->range.topLeft.row;
        if (header || grew) {
            changed = syncTableHeaders(book, sheetIndex) || changed;
        }
        if (!header) {
            changed = applyCalculatedColumn(book, sheetIndex, addr) || changed;
        }
    }
    if (changed) {
        book.refreshStructuredRefs();
    }
    return ok;
}

bool createTable(Workbook &book, qint32 sheetIndex, const CellRange &requested, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!requested.isValid()) {
        return fail(QStringLiteral("Select the cells for the table first."));
    }
    CellRange range = requested;
    if (range.topLeft.row == range.bottomRight.row) {
        ++range.bottomRight.row; // a header needs a data row under it
    }
    Sheet &sheet = book.sheetAt(sheetIndex);
    for (const TableSpec &table : sheet.tables()) {
        if (table.range.topLeft.row <= range.bottomRight.row && table.range.bottomRight.row >= range.topLeft.row
            && table.range.topLeft.col <= range.bottomRight.col && table.range.bottomRight.col >= range.topLeft.col) {
            return fail(QStringLiteral("A table cannot overlap another table."));
        }
    }
    for (const CellRange &merge : sheet.merges()) {
        if (merge.topLeft.row <= range.bottomRight.row && merge.bottomRight.row >= range.topLeft.row
            && merge.topLeft.col <= range.bottomRight.col && merge.bottomRight.col >= range.topLeft.col) {
            return fail(QStringLiteral("A table cannot contain merged cells."));
        }
    }
    for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            const Cell *cell = sheet.cellAt({r, c});
            if (cell != nullptr && (cell->spillAnchor.isValid() || cell->arrayRange.isValid()
                                    || book.spillAt(sheetIndex, {r, c}).has_value())) {
                return fail(QStringLiteral("A table cannot contain an array formula or a spill."));
            }
        }
    }
    const AutoFilter &filter = sheet.autoFilter();
    if (filter.range.isValid() && filter.range.topLeft.row <= range.bottomRight.row
        && filter.range.bottomRight.row >= range.topLeft.row && filter.range.topLeft.col <= range.bottomRight.col
        && filter.range.bottomRight.col >= range.topLeft.col) {
        sheet.setAutoFilter({}); // the table brings its own filter buttons
    }
    TableSpec table;
    table.name = nextTableName(book);
    table.range = range;
    table.styleName = QStringLiteral("TableStyleMedium2");
    QList<TableSpec> tables = sheet.tables();
    tables.append(table);
    sheet.setTables(tables);
    syncTableHeaders(book, sheetIndex);
    book.refreshStructuredRefs();
    return true;
}

bool setTableTotalsRow(Workbook &book, qint32 sheetIndex, qsizetype index, bool on, QString *error)
{
    Sheet &sheet = book.sheetAt(sheetIndex);
    if (index < 0 || index >= sheet.tables().size()) {
        return false;
    }
    TableSpec table = sheet.tables().at(index);
    if (table.totalsRow == on) {
        return true;
    }
    const qint32 left = table.range.topLeft.col;
    const qint32 right = table.range.bottomRight.col;
    if (on) {
        const qint32 below = table.range.bottomRight.row + 1;
        for (qint32 col = left; col <= right; ++col) {
            if (hasContent(sheet, {below, col}) || sheet.mergeCovering({below, col}) != nullptr
                || sheet.tableCovering({below, col}) != nullptr) {
                if (error != nullptr) {
                    *error = QStringLiteral("The row below the table is in use: make room for the total row first.");
                }
                return false;
            }
        }
        const CellRange data = tableDataRows(table);
        table.range.bottomRight.row = below;
        table.totalsRow = true;
        QList<TableSpec> tables = sheet.tables();
        tables[index] = table;
        sheet.setTables(tables);
        // "Total" in the first column, the last column summed (counted when
        // it holds no number), as Excel's Total Row does.
        bool numeric = false;
        for (qint32 row = data.topLeft.row; !numeric && row <= data.bottomRight.row; ++row) {
            numeric = isNumber(book.cellValue(sheetIndex, row, right));
        }
        book.beginBatch();
        if (right > left) {
            book.setCellValue(sheetIndex, {below, left}, QStringLiteral("Total"));
        }
        StructRef column;
        column.firstColumn = table.columnNames.value(right - left);
        if (column.firstColumn.isEmpty()) {
            column.firstColumn = toText(book.cellValue(sheetIndex, table.range.topLeft.row, right));
        }
        const QString formula = QStringLiteral("SUBTOTAL(%1,%2)")
                                    .arg(numeric ? 109 : 103)
                                    .arg(structRefText(column, StructRefSpelling::Display));
        book.setCellInput(sheetIndex, {below, right}, QLatin1Char('=') + formula);
        book.endBatch();
    } else {
        const qint32 totals = table.range.bottomRight.row;
        book.beginBatch();
        for (qint32 col = left; col <= right; ++col) {
            book.setCellValue(sheetIndex, {totals, col}, Value());
        }
        table.range.bottomRight.row = totals - 1;
        table.totalsRow = false;
        QList<TableSpec> tables = book.sheetAt(sheetIndex).tables();
        tables[index] = table;
        book.sheetAt(sheetIndex).setTables(tables);
        book.endBatch();
    }
    book.refreshStructuredRefs();
    return true;
}

} // namespace QindaOffice::Calc
