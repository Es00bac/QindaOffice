// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Data ▸ Solver (M90, D-098): the model Excel's Solver add-in keeps — an
// objective cell set to Max, Min or Value Of, the variable cells, the
// constraints and the options — and the hidden, sheet-scoped names Excel
// keeps it in: solver_opt, solver_typ, solver_val, solver_adj, solver_num,
// solver_lhsN / solver_relN / solver_rhsN, solver_neg, solver_eng and the
// option names (solver_pre, solver_tim, solver_itr, …, solver_ver).
//
// AGENT-CONTRACT (calc_io, Excel): Workbook::definedNames is the model's
// only home. calc_io reads and writes these names like any other hidden
// sheet-scoped name, so a workbook Excel saved opens with its model and a
// model saved here opens in Excel's Solver. Nothing else stores it; undo
// and recovery carry it with the names. Plain functions over a Workbook the
// caller owns; Qt Core only.

#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Calc {

class Workbook;

// Excel's solver_typ.
enum class SolverGoal : quint8 { Max = 1, Min = 2, ValueOf = 3 };
// Excel's solver_eng.
enum class SolverMethod : quint8 { GrgNonlinear = 1, SimplexLp = 2, Evolutionary = 3 };
// Excel's solver_relN: <=, =, >=, int, bin, dif (AllDifferent).
enum class SolverRelation : quint8 { LessEqual = 1, Equal = 2, GreaterEqual = 3, Integer = 4, Binary = 5, AllDifferent = 6 };

struct SolverConstraint {
    // A cell or range on the model's sheet, engine spelling, sheet-qualified
    // ("Sheet1!$D$5:$D$7").
    QString left;
    SolverRelation relation = SolverRelation::LessEqual;
    // A number, a reference or a formula, without '=' (sheet-qualified
    // references); unused for int, bin and dif.
    QString right;
    bool operator==(const SolverConstraint &) const = default;
};

// Solver Options, by tab (Excel's names in brackets). "Unlimited" is 0
// here; Excel stores 2147483647.
struct SolverOptions {
    // All Methods
    double precision = 1e-6;          // Constraint Precision (solver_pre)
    bool autoScale = true;            // Use Automatic Scaling (solver_scl 1)
    bool showIterations = false;      // Show Iteration Results (solver_sho 1)
    bool ignoreIntegers = false;      // Ignore Integer Constraints (solver_rlx 1)
    double integerOptimality = 0.01;  // Integer Optimality (%), as a fraction (solver_tol)
    double maxTime = 0;               // Max Time, seconds (solver_tim)
    qint64 iterations = 0;            // Iterations (solver_itr)
    qint64 maxSubproblems = 0;        // Max Subproblems (solver_mip)
    qint64 maxFeasible = 0;           // Max Feasible Solutions (solver_nod)
    // GRG Nonlinear and Evolutionary
    double convergence = 1e-4;        // Convergence (solver_cvg)
    bool centralDerivatives = false;  // Derivatives: Central (solver_drv 2)
    bool multistart = false;          // Use Multistart (solver_msl 1)
    int populationSize = 100;         // Population Size (solver_ssz)
    int randomSeed = 0;               // Random Seed (solver_rsd)
    bool requireBounds = true;        // Require Bounds on Variables (solver_rbv 1)
    double mutationRate = 0.075;      // Mutation Rate (solver_mrt)
    int maxTimeWithoutImprovement = 30; // seconds (solver_mni)
    // Excel 2007's GRG settings, kept as read: Estimates (solver_est),
    // Search (solver_nwt) — 1 tangent / Newton, 2 quadratic / conjugate.
    int estimates = 1;
    int search = 1;
    bool operator==(const SolverOptions &) const = default;
};

struct SolverModel {
    // One cell on the model's sheet, sheet-qualified; empty: no objective
    // (Solver looks for a feasible solution).
    QString objective;
    SolverGoal goal = SolverGoal::Max;
    double valueOf = 0;
    // The variable cells: ranges on the model's sheet, ',' between them.
    QString variables;
    QList<SolverConstraint> constraints;
    bool nonNegative = true; // Make Unconstrained Variables Non-Negative (solver_neg 1)
    SolverMethod method = SolverMethod::GrgNonlinear;
    SolverOptions options;
    bool operator==(const SolverModel &) const = default;
};

// The model the names scoped to `sheet` hold; nullopt when it has none
// (neither solver_opt nor solver_adj).
[[nodiscard]] std::optional<SolverModel> solverModelOf(const Workbook &book, qint32 sheet);
// Replaces the sheet's solver_* names with `model`'s — Excel's full set,
// hidden, scoped to the sheet, in Excel's (alphabetical) order. The names
// are used by no formula, so nothing recalculates.
void setSolverModel(Workbook &book, qint32 sheet, const SolverModel &model);
// Drops the sheet's solver_* names.
void clearSolverModel(Workbook &book, qint32 sheet);

// "<=", "=", ">=", "int", "bin", "dif" — the Add Constraint list.
[[nodiscard]] QString solverRelationText(SolverRelation relation);
[[nodiscard]] std::optional<SolverRelation> solverRelationFrom(const QString &text);
// A number as Excel's Solver writes it: plain decimals ("0.000001", not
// "1e-06"), the shortest text that reads back as the same double.
[[nodiscard]] QString solverNumberText(double value);
// The method's name as Excel's dialog and reports give it.
[[nodiscard]] QString solverMethodName(SolverMethod method);

// The parts of a comma-separated reference list ("A1:B2,D4"), commas
// inside quoted sheet names left alone.
[[nodiscard]] QStringList solverRefParts(const QString &text);

} // namespace QindaOffice::Calc
