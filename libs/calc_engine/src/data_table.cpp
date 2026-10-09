// SPDX-License-Identifier: GPL-3.0-or-later
// Data tables (data_table.h, M78, D-087): Excel's {=TABLE(r,c)} worked out
// the way Excel does it — every input value put into the input cell in
// turn, the workbook recalculated, the formula's value taken — after every
// top-level recalculation (Workbook::recomputeDataTables).
//
// AGENT-NOTE: the input cell gets a trial value through
// WhatIf::putTrialValue (formula set aside, dependency index untouched) and
// is recalculated directly with Workbook::recalc, never through
// setCellValue: inside a batch setCellValue would defer the recalculation
// and every value tried would read the same stale result. The guard
// (m_computingTables) keeps those recalculations from starting another
// round of tables.

#include "data_table.h"

#include "sheet.h"
#include "what_if_internal.h"
#include "workbook.h"

#include <QSet>

namespace QindaOffice::Calc {

namespace {

// The row just above `ref` and the column just left of it.
CellRange rowAbove(const CellRange &ref)
{
    return {{ref.topLeft.row - 1, ref.topLeft.col}, {ref.topLeft.row - 1, ref.bottomRight.col}};
}

CellRange columnLeft(const CellRange &ref)
{
    return {{ref.topLeft.row, ref.topLeft.col - 1}, {ref.bottomRight.row, ref.topLeft.col - 1}};
}

bool hasRoomAround(const CellRange &ref)
{
    return ref.isValid() && ref.topLeft.row >= 1 && ref.topLeft.col >= 1;
}

// Excel shows 0 for a formula cell that gives nothing.
Value tableValue(const Value &value)
{
    return isBlank(value) ? Value(0.0) : value;
}

// The results into the table's cells, as plain values; then one
// recalculation for the formulas that read them.
void writeResults(Workbook &book, qint32 sheet, const CellRange &ref, const QVector<Value> &results)
{
    Sheet &target = book.sheetAt(sheet);
    const qint32 cols = ref.bottomRight.col - ref.topLeft.col + 1;
    QSet<CellKey> changed;
    for (qint32 r = ref.topLeft.row; r <= ref.bottomRight.row; ++r) {
        for (qint32 c = ref.topLeft.col; c <= ref.bottomRight.col; ++c) {
            const Value &value = results.at((r - ref.topLeft.row) * cols + (c - ref.topLeft.col));
            const CellAddr addr{r, c};
            const CellKey key(sheet, r, c);
            const Cell *existing = target.cellAt(addr);
            if (existing != nullptr && !existing->ast && !existing->spillAnchor.isValid() && existing->value == value) {
                continue;
            }
            Cell &cell = target.ensureCell(addr);
            if (cell.ast) {
                // A result cell is the table's: a formula someone left there goes.
                book.dependencies().updateCell(key, {});
                cell.ast.reset();
                cell.formula.clear();
                cell.isVolatile = false;
                cell.dynamicArray = false;
                cell.arrayRange = {};
            }
            cell.spillAnchor = CellAddr{};
            cell.value = value;
            changed.insert(key);
        }
    }
    if (!changed.isEmpty()) {
        book.recalc(changed);
    }
}

// One table, with the guard already holding off nested rounds.
void computeTableNow(Workbook &book, qint32 sheet, const DataTableSpec &spec)
{
    if (sheet < 0 || sheet >= book.sheetCount() || !hasRoomAround(spec.ref)) {
        return;
    }
    const CellRange &ref = spec.ref;
    const qint32 rows = ref.bottomRight.row - ref.topLeft.row + 1;
    const qint32 cols = ref.bottomRight.col - ref.topLeft.col + 1;
    QVector<Value> results(qsizetype(rows) * cols, Value(0.0));
    const bool deleted = spec.r1Deleted || !spec.r1.isValid()
        || (spec.twoD && (spec.r2Deleted || !spec.r2.isValid()));
    if (deleted) {
        // An input cell that was deleted: Excel shows #REF! throughout.
        results.fill(Value(Err::Ref));
        writeResults(book, sheet, ref, results);
        return;
    }
    // The input values as they are now, before anything is tried.
    QVector<Value> rowValues(cols);
    QVector<Value> colValues(rows);
    for (qint32 c = 0; c < cols; ++c) {
        rowValues[c] = book.cellValue(sheet, ref.topLeft.row - 1, ref.topLeft.col + c);
    }
    for (qint32 r = 0; r < rows; ++r) {
        colValues[r] = book.cellValue(sheet, ref.topLeft.row + r, ref.topLeft.col - 1);
    }
    const WhatIf::SavedCell first = WhatIf::saveCell(book, sheet, spec.r1);
    const std::optional<WhatIf::SavedCell> second =
        spec.twoD && spec.r2 != spec.r1 ? std::optional(WhatIf::saveCell(book, sheet, spec.r2)) : std::nullopt;
    const auto at = [&results, cols](qint32 r, qint32 c) -> Value & { return results[qsizetype(r) * cols + c]; };
    if (spec.twoD) {
        // r1 takes the row's values (across), r2 the column's (down); the
        // formula is the corner cell.
        const CellAddr formula{ref.topLeft.row - 1, ref.topLeft.col - 1};
        for (qint32 r = 0; r < rows; ++r) {
            const CellKey down = WhatIf::putTrialValue(book, sheet, spec.r2, colValues.at(r));
            for (qint32 c = 0; c < cols; ++c) {
                const CellKey across = WhatIf::putTrialValue(book, sheet, spec.r1, rowValues.at(c));
                book.recalc(c == 0 ? QSet<CellKey>{down, across} : QSet<CellKey>{across});
                at(r, c) = tableValue(book.cellValue(sheet, formula.row, formula.col));
            }
        }
    } else if (spec.rowInput) {
        // The input values run across the row above; one formula per row,
        // in the column left of the results.
        for (qint32 c = 0; c < cols; ++c) {
            book.recalc({WhatIf::putTrialValue(book, sheet, spec.r1, rowValues.at(c))});
            for (qint32 r = 0; r < rows; ++r) {
                at(r, c) = tableValue(book.cellValue(sheet, ref.topLeft.row + r, ref.topLeft.col - 1));
            }
        }
    } else {
        // The input values run down the column left; one formula per
        // column, in the row above the results.
        for (qint32 r = 0; r < rows; ++r) {
            book.recalc({WhatIf::putTrialValue(book, sheet, spec.r1, colValues.at(r))});
            for (qint32 c = 0; c < cols; ++c) {
                at(r, c) = tableValue(book.cellValue(sheet, ref.topLeft.row - 1, ref.topLeft.col + c));
            }
        }
    }
    QSet<CellKey> restored{WhatIf::restoreCell(book, sheet, first)};
    if (second.has_value()) {
        restored.insert(WhatIf::restoreCell(book, sheet, *second));
    }
    book.recalc(restored);
    writeResults(book, sheet, ref, results);
}

} // namespace

CellRange DataTableSpec::inputValues(bool rows) const
{
    if (!hasRoomAround(ref)) {
        return {};
    }
    return rows ? rowAbove(ref) : columnLeft(ref);
}

std::optional<DataTableSpec> makeDataTable(const CellRange &selection, const CellAddr &rowInput,
                                           const CellAddr &colInput, QString *error)
{
    const auto fail = [error](const QString &message) -> std::optional<DataTableSpec> {
        if (error != nullptr) {
            *error = message;
        }
        return std::nullopt;
    };
    if (!selection.isValid() || selection.bottomRight.row == selection.topLeft.row
        || selection.bottomRight.col == selection.topLeft.col) {
        // Excel's: the selection must hold the formulas and the input values.
        return fail(QStringLiteral("Selection not valid."));
    }
    const bool hasRow = rowInput.isValid();
    const bool hasCol = colInput.isValid();
    if ((!hasRow && !hasCol) || (hasRow && selection.contains(rowInput)) || (hasCol && selection.contains(colInput))
        || (hasRow && hasCol && rowInput == colInput)) {
        return fail(QStringLiteral("Input cell reference is not valid."));
    }
    DataTableSpec spec;
    spec.ref = {{selection.topLeft.row + 1, selection.topLeft.col + 1}, selection.bottomRight};
    if (hasRow && hasCol) {
        spec.twoD = true;
        spec.rowInput = true; // Excel writes dtr="1" on its two-variable tables
        spec.r1 = rowInput;
        spec.r2 = colInput;
    } else {
        spec.rowInput = hasRow;
        spec.r1 = hasRow ? rowInput : colInput;
    }
    return spec;
}

QString dataTableFormulaText(const DataTableSpec &spec)
{
    const QString first = spec.r1Deleted || !spec.r1.isValid() ? QStringLiteral("#REF!") : addrText(spec.r1);
    if (spec.twoD) {
        const QString second = spec.r2Deleted || !spec.r2.isValid() ? QStringLiteral("#REF!") : addrText(spec.r2);
        return QStringLiteral("{=TABLE(%1,%2)}").arg(first, second);
    }
    return spec.rowInput ? QStringLiteral("{=TABLE(%1,)}").arg(first) : QStringLiteral("{=TABLE(,%1)}").arg(first);
}

const DataTableSpec *dataTableAt(const Sheet &sheet, const CellAddr &addr)
{
    for (const DataTableSpec &spec : sheet.dataTables()) {
        if (spec.ref.contains(addr)) {
            return &spec;
        }
    }
    return nullptr;
}

bool dataTableSplitBy(const Sheet &sheet, bool rows, qint32 at, qint32 count, bool insert)
{
    for (const DataTableSpec &spec : sheet.dataTables()) {
        // The table spans its formula row and input column too.
        const qint32 lo = (rows ? spec.ref.topLeft.row : spec.ref.topLeft.col) - 1;
        const qint32 hi = rows ? spec.ref.bottomRight.row : spec.ref.bottomRight.col;
        if (insert) {
            if (at > lo && at <= hi) {
                return true;
            }
            continue;
        }
        const qint32 last = at + count - 1;
        const bool touches = last >= lo && at <= hi;
        const bool whole = at <= lo && last >= hi;
        if (touches && !whole) {
            return true;
        }
    }
    return false;
}

void computeDataTable(Workbook &book, qint32 sheet, const DataTableSpec &spec)
{
    // AGENT-NOTE: every recalculation below ends in
    // Workbook::recomputeDataTables, which only the workbook can hold off;
    // the tables are put aside meanwhile so it finds nothing to do.
    QList<QList<DataTableSpec>> aside;
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        aside.append(book.sheetAt(s).dataTables());
        book.sheetAt(s).setDataTables({});
    }
    computeTableNow(book, sheet, spec);
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        book.sheetAt(s).setDataTables(aside.value(s));
    }
}

void Workbook::recomputeDataTables()
{
    if (m_computingTables || m_bulkLoading) {
        return;
    }
    bool any = false;
    for (const auto &sheet : m_sheets) {
        any = any || !sheet->dataTables().isEmpty();
    }
    if (!any) {
        return;
    }
    m_computingTables = true;
    for (qint32 s = 0; s < sheetCount(); ++s) {
        // A copy: working a table out never changes the list, but a
        // reference into it must not outlive anything that might.
        const QList<DataTableSpec> tables = sheetAt(s).dataTables();
        for (const DataTableSpec &spec : tables) {
            computeTableNow(*this, s, spec);
        }
    }
    m_computingTables = false;
}

} // namespace QindaOffice::Calc
