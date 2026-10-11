// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Data ▸ Solver (M90, D-098): solving a SolverModel (solver_model.h) on the
// workbook — Excel's Simplex LP, GRG Nonlinear and Evolutionary methods
// (solver_math.h), branch and bound for int, bin and dif, Excel's result
// codes and sentences, and the Answer Report sheet (solver_report.cpp).
//
// Each trial solution goes into the variable cells and the workbook
// recalculates (the objective and constraint cells are ordinary formulas),
// so a solve owns the workbook while it runs. AGENT-GUARD: never inside a
// Workbook batch (each trial must recalculate at once), and one thread per
// Workbook — QindaCalc solves on a worker thread over a copy of the
// document (apps/qindacalc/src/calc_solver.cpp) and puts the answer into
// the real one on the GUI thread.

#include "solver_model.h"
#include "value.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QVector>

#include <functional>
#include <optional>

namespace QindaOffice::Calc {

class Workbook;

// Excel's Solver result codes (the value SolverSolve returns).
enum class SolverStatus : int {
    Optimal = 0,          // a solution; all constraints and optimality conditions satisfied
    Converged = 1,        // converged to the current solution; all constraints satisfied
    CannotImprove = 2,    // cannot improve the current solution; all constraints satisfied
    IterationLimit = 3,
    NotConverging = 4,    // the objective cell values do not converge (unbounded)
    Infeasible = 5,
    Stopped = 6,
    NotLinear = 7,        // the linearity conditions required by the LP Solver are not satisfied
    TooLarge = 8,
    ErrorValue = 9,       // an error value in the objective cell or a constraint cell
    TimeLimit = 10,
    ModelError = 13,
    IntegerOptimal = 14,  // an integer solution within tolerance
    FeasibleLimit = 15,
    SubproblemLimit = 16,
    GlobalProbable = 17,  // converged in probability to a global solution (Evolutionary)
    NeedsBounds = 18,     // all variables must have both upper and lower bounds
    BoundsConflict = 19,  // variable bounds conflict in a binary or alldifferent constraint
};

// Excel's sentence for a status ("Solver found a solution.  All Constraints
// and optimality conditions are satisfied.").
[[nodiscard]] QString solverStatusMessage(SolverStatus status);
// Statuses whose solution satisfies every constraint.
[[nodiscard]] bool solverStatusFeasible(SolverStatus status);

struct SolverProgress {
    int trials = 0;       // trial solutions so far (iterations, subproblems, generations)
    int subproblems = 0;
    double objective = 0; // the objective cell's value at the latest trial (0 without one)
};

struct SolverCallbacks {
    // After each trial solution; false stops Solver (SolverStatus::Stopped).
    std::function<bool(const SolverProgress &)> progress;
};

// One line of the Answer Report's Constraints table.
struct SolverReportConstraint {
    CellAddr cell;
    QString formula;     // "$D$5<=$F$5", "$B$2=integer"
    Value value;         // the cell's value at the end
    bool binding = true;
    double slack = 0;
};

struct SolverOutcome {
    SolverStatus status = SolverStatus::ModelError;
    QString message;          // Excel's sentence for `status`, or why the model was refused
    bool started = false;     // false: refused before solving (`message` says why)
    bool changed = false;     // the variable cells hold `values` now
    QList<CellAddr> variables;
    QVector<double> originalValues;
    QVector<double> values;   // what the variable cells hold at the end
    std::optional<CellAddr> objective;
    Value originalObjective;
    Value finalObjective;
    QList<int> variableKinds; // per variable: 0 continuous, 1 integer, 2 binary (the report's Integer column)
    QList<SolverReportConstraint> constraints;
    int iterations = 0;
    int subproblems = 0;
    double seconds = 0;
};

// Solves `model` on `sheet` (the model's sheet). The workbook ends holding
// the solution in the variable cells when there is one to keep (`changed`:
// a solution, or the last trial of an infeasible or stopped search — what
// Excel shows behind its Solver Results dialog), its own content otherwise.
[[nodiscard]] SolverOutcome solveModel(Workbook &book, qint32 sheet, const SolverModel &model,
                                       const SolverCallbacks &callbacks = {});

// What Excel refuses before solving — the objective, the variable cells,
// each constraint's cells and right side: "" or Excel's message.
[[nodiscard]] QString solverModelProblem(const Workbook &book, qint32 sheet, const SolverModel &model);
// One constraint as Add Constraint checks it ("" or the message).
[[nodiscard]] QString solverConstraintProblem(const Workbook &book, qint32 sheet, const SolverModel &model,
                                              const SolverConstraint &constraint);

// The Answer Report (solver_report.cpp): a new sheet "Answer Report N" put
// before the model's sheet, in Excel's layout — the result, the engine,
// the options, the objective cell, the variable cells and the constraints,
// each with its name from the labels beside and above it. Returns its index.
[[nodiscard]] qint32 addAnswerReport(Workbook &book, qint32 modelSheet, const SolverModel &model,
                                     const SolverOutcome &outcome, const QString &workbookName,
                                     const QDateTime &created);

} // namespace QindaOffice::Calc
