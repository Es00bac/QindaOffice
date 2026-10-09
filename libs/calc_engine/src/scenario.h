// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Data ▸ What-If Analysis ▸ Scenario Manager (M78, D-087): named sets of
// values for up to 32 changing cells of one sheet, shown into those cells
// on request, and the Scenario Summary report — Excel's model and its
// <scenarios> element (ECMA-376 §18.3.1.76–77). The scenarios belong to
// their sheet (Sheet::scenarios), are moved by structural edits
// (structure_whatif.cpp) and ride undo and recovery like every rule.
//
// Plain data with free functions over the Workbook; Qt Core only. Every
// mutation here is the caller's undo step: the app snapshots around it.

#include "value.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Calc {

class Workbook;

struct Scenario {
    QString name;
    // The changing cells, on the sheet that holds the scenario, in the
    // order Excel lists them (≤ 32).
    QList<CellAddr> cells;
    // One per cell: the value as Excel stores it (<inputCells val>): a
    // number in the file's spelling ("100", "0.05"), text, TRUE/FALSE.
    QStringList values;
    QString comment;
    QString user;           // who made it (Excel's "Created by …")
    bool locked = true;     // Prevent changes — in force while the sheet is protected
    bool hidden = false;    // Hide — in force while the sheet is protected
    bool operator==(const Scenario &) const = default;
};

// The scenarios of one sheet (Excel's <scenarios>).
struct ScenarioSet {
    QList<Scenario> scenarios;
    qint32 current = 0;          // the scenario selected in the manager
    qint32 shown = -1;           // the scenario last shown (-1: none)
    QList<CellRange> resultCells; // the result cells of the last summary (sqref)
    [[nodiscard]] bool isEmpty() const { return scenarios.isEmpty(); }
    bool operator==(const ScenarioSet &) const = default;
};

// Excel's limit on changing cells per scenario.
inline constexpr int kMaxScenarioCells = 32;

// The values `cells` hold now, as a scenario stores them (Add Scenario
// fills its dialog with these): a number in the file's spelling, text as
// is, TRUE/FALSE. A formula cell gives its value (Excel replaces a changing
// cell's formula by the scenario's constant when the scenario is shown).
[[nodiscard]] QStringList scenarioInputs(const Workbook &book, qint32 sheet, const QList<CellAddr> &cells);

// Checks a scenario before it is added or changed: a name, 1–32 cells on
// the sheet, one value per cell, no other scenario of the sheet with the
// name (`ignore`: the index being edited). Empty when it is fine, else
// Excel's message.
[[nodiscard]] QString scenarioProblem(const ScenarioSet &set, const Scenario &scenario, qint32 ignore = -1);

// Show: puts the scenario's values into its cells (each value entered as
// typed input would be: a number, a date, TRUE/FALSE, text; an empty value
// empties the cell) and records it as the set's shown and current scenario.
// One recalculation. False (with `error`) when the index is out of range.
// AGENT-GUARD: Show and the summary batch their own edits; never call them
// inside a Workbook batch (the summary would read values not yet worked out).
bool showScenario(Workbook &book, qint32 sheet, qint32 index, QString *error = nullptr);

// The Scenario Summary report's numbers: for each scenario (and first for
// the values the changing cells hold now — Excel's "Current Values"
// column), what the changing cells and the result cells hold with it. The
// scenarios are shown one by one and the workbook is left exactly as it
// was (every changing cell gets its own content back, formulas included).
struct ScenarioSummary {
    QStringList names;            // "Current Values", then the scenarios in order
    QList<CellAddr> changing;     // the union of the scenarios' changing cells, in first-seen order
    QList<CellAddr> results;      // the result cells asked for
    // values[column][row]: column 0 is Current Values; rows are `changing`
    // then `results` (a cell a scenario does not change shows the current value).
    QList<QList<Value>> values;
    QStringList comments;         // per column (Current Values: empty)
    // per column: the changing cells that column's scenario sets (Current
    // Values: none) — the report highlights those in grey.
    QList<QList<CellAddr>> setBy;
};
[[nodiscard]] ScenarioSummary scenarioSummary(Workbook &book, qint32 sheet, const QList<CellRange> &resultCells);

// Writes the report into `target` (a new, empty sheet the caller added) the
// way Excel lays it out: "Scenario Summary" in B2, the scenario names
// across row 3 from D, "Changing Cells:" and the cells (by defined name
// when one names exactly that cell, else $A$1) with their values, "Result
// Cells:" and theirs, and Excel's three note lines. Values only, no
// formulas — the report is a snapshot, as in Excel.
void writeScenarioSummary(Workbook &book, qint32 target, qint32 source, const ScenarioSummary &summary);

} // namespace QindaOffice::Calc
