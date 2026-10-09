// SPDX-License-Identifier: GPL-3.0-or-later
// Tables through a structural edit (structure_internal.h, M69, D-078): a
// table that loses columns or dies takes the structured references to
// them along (#REF!, or a column range that shrinks, as in Excel); inserted
// columns become named table columns and inserted rows get the calculated
// columns' formulas; a removed header or totals row leaves the table.

#include "structure_internal.h"

#include "structured_ref.h"
#include "table_edit.h"
#include "table_refs.h"
#include "workbook.h"

#include <optional>

namespace QindaOffice::Calc::StructureMath {

namespace {

qint32 widthOf(const TableSpec &table)
{
    return table.range.bottomRight.col - table.range.topLeft.col + 1;
}

// The table's column names, one per column (header text where the record
// has none).
QStringList namesOf(const Sheet &sheet, const TableSpec &table)
{
    QStringList names = table.columnNames;
    names.resize(widthOf(table));
    for (qint32 i = 0; i < names.size(); ++i) {
        if (names.at(i).isEmpty() && table.headerRow) {
            const Cell *header = sheet.cellAt({table.range.topLeft.row, table.range.topLeft.col + i});
            names[i] = header != nullptr ? toText(header->value) : QString();
        }
    }
    return names;
}

qint32 indexOf(const QStringList &names, const QString &name)
{
    for (qint32 i = 0; i < names.size(); ++i) {
        if (names.at(i).trimmed().compare(name.trimmed(), Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

bool removes(const Band &band, qint32 index)
{
    return !band.insert && index >= band.at && index < band.at + band.count;
}

} // namespace

void rewriteTableRefs(Workbook &book, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    if (band.insert) {
        return; // an insert removes no column and no table
    }
    struct Loss {
        QString table;
        QStringList names;
        QSet<qint32> gone;
        bool dead = false;
    };
    QList<Loss> losses;
    const Sheet &sheet = book.sheetAt(edit.sheet);
    for (const TableSpec &table : sheet.tables()) {
        Loss loss{table.name, namesOf(sheet, table), {}, false};
        qint32 lo = editsRows(edit) ? table.range.topLeft.row : table.range.topLeft.col;
        qint32 hi = editsRows(edit) ? table.range.bottomRight.row : table.range.bottomRight.col;
        loss.dead = !movedSpan(lo, hi, band);
        if (!editsRows(edit)) {
            for (qint32 i = 0; i < loss.names.size(); ++i) {
                if (removes(band, table.range.topLeft.col + i)) {
                    loss.gone.insert(i);
                }
            }
        }
        if (loss.dead || !loss.gone.isEmpty()) {
            losses.append(loss);
        }
    }
    if (losses.isEmpty()) {
        return;
    }
    rewriteStructRefs(book, [&losses](const StructRef &ref, const QString &tableName) -> NodePtr {
        for (const Loss &loss : losses) {
            if (loss.table.compare(tableName, Qt::CaseInsensitive) != 0) {
                continue;
            }
            if (loss.dead) {
                return Node::makeError(Err::Ref);
            }
            if (ref.firstColumn.isEmpty()) {
                return nullptr; // the whole table: it follows by itself
            }
            const qint32 a = indexOf(loss.names, ref.firstColumn);
            const qint32 b = ref.lastColumn.isEmpty() ? a : indexOf(loss.names, ref.lastColumn);
            if (a < 0 || b < 0 || (!loss.gone.contains(a) && !loss.gone.contains(b))) {
                return nullptr;
            }
            // A column range keeps the columns of it that remain.
            qint32 first = -1;
            qint32 last = -1;
            for (qint32 i = qMin(a, b); i <= qMax(a, b); ++i) {
                if (!loss.gone.contains(i)) {
                    first = first < 0 ? i : first;
                    last = i;
                }
            }
            if (first < 0) {
                return Node::makeError(Err::Ref);
            }
            StructRef kept = ref;
            kept.firstColumn = loss.names.at(first);
            kept.lastColumn = first == last ? QString() : loss.names.at(last);
            return Node::makeStructRef(kept);
        }
        return nullptr;
    });
}

QList<TableSpec> movedTables(const Sheet &sheet, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    const bool rows = editsRows(edit);
    QList<TableSpec> out;
    for (TableSpec table : sheet.tables()) {
        table.columnNames = namesOf(sheet, table);
        table.calculatedColumns.resize(widthOf(table));
        qint32 lo = rows ? table.range.topLeft.row : table.range.topLeft.col;
        qint32 hi = rows ? table.range.bottomRight.row : table.range.bottomRight.col;
        const qint32 first = lo;
        const qint32 last = hi;
        if (!movedSpan(lo, hi, band)) {
            continue; // the whole table went with the band
        }
        if (rows) {
            // A removed header or totals row leaves the table without one.
            table.headerRow = table.headerRow && !removes(band, first);
            table.totalsRow = table.totalsRow && !removes(band, last);
            table.range.topLeft.row = lo;
            table.range.bottomRight.row = hi;
            out.append(table);
            continue;
        }
        if (band.insert && band.at > first && band.at <= last) {
            // Columns inserted inside the table are table columns, named
            // from their (blank) headers afterwards: Column<N>.
            const qint32 index = band.at - first;
            for (qint32 k = 0; k < band.count; ++k) {
                table.columnNames.insert(index, QString());
                table.calculatedColumns.insert(index, QString());
            }
        } else if (!band.insert) {
            for (qint32 i = last - first; i >= 0; --i) {
                if (removes(band, first + i)) {
                    table.columnNames.removeAt(i);
                    table.calculatedColumns.removeAt(i);
                }
            }
        }
        table.range.topLeft.col = lo;
        table.range.bottomRight.col = hi;
        out.append(table);
    }
    return out;
}

void fillTablesAfterEdit(Workbook &book, const StructureEdit &edit)
{
    const Band band = bandOf(edit);
    if (!band.insert) {
        return;
    }
    if (!editsRows(edit)) {
        syncTableHeaders(book, edit.sheet); // names the inserted columns
        return;
    }
    const QList<TableSpec> tables = book.sheetAt(edit.sheet).tables();
    for (const TableSpec &table : tables) {
        const CellRange data = tableDataRows(table);
        QList<qint32> inserted;
        for (qint32 row = band.at; row < band.at + band.count; ++row) {
            if (row >= data.topLeft.row && row <= data.bottomRight.row && row > table.range.topLeft.row) {
                inserted.append(row);
            }
        }
        if (!inserted.isEmpty()) {
            fillCalculatedCells(book, edit.sheet, table, inserted, -1);
        }
    }
}

} // namespace QindaOffice::Calc::StructureMath
