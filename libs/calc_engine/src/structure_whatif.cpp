// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087): scenarios and data tables through inserted and removed rows
// and columns (structure_internal.h). Both address cells of their own sheet
// only, so only the edited sheet's change. A data table follows its result
// cells; an input cell that is deleted leaves the table showing #REF!
// (Excel's del1/del2); a table whose results are all gone goes. A
// scenario's changing cells move; a deleted one leaves the scenario, its
// value with it; a scenario with no cell left goes. The summary's result
// cells move like any range.

#include "structure_internal.h"

#include "workbook.h"

namespace QindaOffice::Calc {

using namespace StructureMath;

namespace {

std::optional<CellAddr> movedAddr(CellAddr addr, const StructureEdit &edit)
{
    qint32 &index = editsRows(edit) ? addr.row : addr.col;
    const qint32 moved = movedIndex(index, bandOf(edit));
    if (moved < 0) {
        return std::nullopt;
    }
    index = moved;
    return addr;
}

std::optional<CellRange> movedRange(CellRange range, const StructureEdit &edit)
{
    const bool rows = editsRows(edit);
    qint32 lo = rows ? range.topLeft.row : range.topLeft.col;
    qint32 hi = rows ? range.bottomRight.row : range.bottomRight.col;
    if (!movedSpan(lo, hi, bandOf(edit))) {
        return std::nullopt;
    }
    (rows ? range.topLeft.row : range.topLeft.col) = lo;
    (rows ? range.bottomRight.row : range.bottomRight.col) = hi;
    return range;
}

QList<DataTableSpec> movedDataTables(const QList<DataTableSpec> &tables, const StructureEdit &edit)
{
    QList<DataTableSpec> out;
    for (DataTableSpec spec : tables) {
        const std::optional<CellRange> ref = movedRange(spec.ref, edit);
        if (!ref.has_value()) {
            continue;
        }
        spec.ref = *ref;
        if (!spec.r1Deleted && spec.r1.isValid()) {
            const std::optional<CellAddr> moved = movedAddr(spec.r1, edit);
            spec.r1Deleted = !moved.has_value();
            spec.r1 = moved.value_or(spec.r1);
        }
        if (spec.twoD && !spec.r2Deleted && spec.r2.isValid()) {
            const std::optional<CellAddr> moved = movedAddr(spec.r2, edit);
            spec.r2Deleted = !moved.has_value();
            spec.r2 = moved.value_or(spec.r2);
        }
        out.append(spec);
    }
    return out;
}

ScenarioSet movedScenarios(const ScenarioSet &set, const StructureEdit &edit)
{
    ScenarioSet out = set;
    out.scenarios.clear();
    QHash<qint32, qint32> newIndex; // old scenario index → new one
    for (qsizetype i = 0; i < set.scenarios.size(); ++i) {
        Scenario scenario = set.scenarios.at(i);
        QList<CellAddr> cells;
        QStringList values;
        for (qsizetype c = 0; c < scenario.cells.size(); ++c) {
            if (const std::optional<CellAddr> moved = movedAddr(scenario.cells.at(c), edit)) {
                cells.append(*moved);
                values.append(scenario.values.value(c));
            }
        }
        if (cells.isEmpty()) {
            continue;
        }
        scenario.cells = cells;
        scenario.values = values;
        newIndex.insert(qint32(i), qint32(out.scenarios.size()));
        out.scenarios.append(scenario);
    }
    out.current = newIndex.value(set.current, 0);
    out.shown = newIndex.value(set.shown, -1);
    out.resultCells.clear();
    for (const CellRange &range : set.resultCells) {
        if (const std::optional<CellRange> moved = movedRange(range, edit)) {
            out.resultCells.append(*moved);
        }
    }
    return out;
}

} // namespace

void StructureMath::moveWhatIfForEdit(Workbook &book, const StructureEdit &edit)
{
    if (edit.sheet < 0 || edit.sheet >= book.sheetCount()) {
        return;
    }
    Sheet &sheet = book.sheetAt(edit.sheet);
    if (!sheet.dataTables().isEmpty()) {
        sheet.setDataTables(movedDataTables(sheet.dataTables(), edit));
    }
    if (!sheet.scenarios().isEmpty() || !sheet.scenarios().resultCells.isEmpty()) {
        sheet.setScenarios(movedScenarios(sheet.scenarios(), edit));
    }
}

} // namespace QindaOffice::Calc
