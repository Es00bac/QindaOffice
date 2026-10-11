// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The numerics behind Data ▸ Solver (M90, D-098), free of the workbook:
// solver.cpp turns a model's cells into one of these problems, runs it and
// puts the answer back into the cells.
//
//   solveLinearProgram — Simplex LP: a dense two-phase primal simplex over
//       rows and bounded variables (solver_lp.cpp).
//   solveNonlinear     — GRG Nonlinear: an augmented-Lagrangian method
//       whose inner problem, bounds only, is solved by a projected
//       quasi-Newton (BFGS) active-set search over finite-difference
//       gradients (solver_nlp.cpp).
//   solveEvolutionary  — Evolutionary: differential evolution inside the
//       variables' bounds with feasibility-first selection (solver_evolve.cpp).
//   branchAndBound     — int, bin and dif constraints over any of the
//       relaxations above (solver_bnb.cpp).
//
// AGENT-NOTE: every function here minimizes; Max is the caller's negated
// objective. All of them are plain functions of their arguments — the
// callbacks are the only way out (progress, the stop button, the
// workbook) — so they run on whichever thread the caller owns.

#include <QList>
#include <QVector>

#include <functional>
#include <limits>

namespace QindaOffice::Calc::SolverMath {

inline constexpr double kInfinity = std::numeric_limits<double>::infinity();

// sense: -1 a·x <= rhs, 0 a·x = rhs, +1 a·x >= rhs.
struct LinearRow {
    QVector<double> a;
    int sense = -1;
    double rhs = 0;
};

// Minimize c·x subject to the rows and lower <= x <= upper (±kInfinity:
// unbounded on that side).
struct LinearProgram {
    QVector<double> c;
    QList<LinearRow> rows;
    QVector<double> lower;
    QVector<double> upper;
};

enum class LpStatus { Optimal, Infeasible, Unbounded, Stopped };

struct LpResult {
    LpStatus status = LpStatus::Infeasible;
    QVector<double> x;
    double objective = 0;
    int pivots = 0;
};

// `keepGoing` is asked every few pivots; false stops (LpStatus::Stopped).
[[nodiscard]] LpResult solveLinearProgram(const LinearProgram &lp, const std::function<bool()> &keepGoing = {});

// ---- nonlinear ----

// Minimize f(x) subject to g_i(x) <= 0, h_j(x) = 0 and lower <= x <= upper.
// `evaluate` fills f, g (inequalities entries) and h (equalities) at x and
// answers false when x gives an error value (a trial point Solver backs off
// from). `linear` rows are added inequalities a·x <= rhs the callback does
// not know about (branch and bound's dif cuts).
struct NonlinearProblem {
    int inequalities = 0;
    int equalities = 0;
    QVector<double> lower;
    QVector<double> upper;
    QList<LinearRow> linear;
    std::function<bool(const QVector<double> &x, double *f, QVector<double> *g, QVector<double> *h)> evaluate;
};

struct NlpOptions {
    double precision = 1e-6;   // the largest constraint violation accepted (scaled)
    double convergence = 1e-4; // relative objective change over five iterations
    bool central = false;      // central differences (forward otherwise)
    bool scale = true;         // divide the objective and each constraint by its size at the start
    qint64 maxIterations = 0;  // 0: unlimited (an internal cap still applies)
    std::function<bool()> keepGoing;                     // false: stop now
    std::function<void(int iteration, double f)> onIteration; // after each iteration (f as given)
};

enum class NlpStatus { Optimal, Converged, CannotImprove, Infeasible, Unbounded, IterationLimit, Stopped, ErrorValue };

struct NlpResult {
    NlpStatus status = NlpStatus::Infeasible;
    QVector<double> x;
    double f = 0;
    double violation = 0; // the largest violation at x (unscaled)
    int iterations = 0;
};

[[nodiscard]] NlpResult solveNonlinear(const NonlinearProblem &problem, const QVector<double> &start,
                                       const NlpOptions &options);

// ---- evolutionary ----

struct EvolveOptions {
    int population = 100;
    int seed = 0;                 // 0: from the clock
    double mutationRate = 0.075;
    double convergence = 1e-4;    // the population's spread in objective that ends the search
    double precision = 1e-6;
    double maxSecondsWithoutImprovement = 30;
    qint64 maxGenerations = 0;    // 0: unlimited (an internal cap applies)
    QVector<bool> integer;        // variables rounded to whole numbers
    std::function<bool()> keepGoing;
    std::function<void(int generation, double f)> onIteration;
};

enum class EvolveStatus { Converged, NoImprovement, Infeasible, Stopped, IterationLimit, ErrorValue };

struct EvolveResult {
    EvolveStatus status = EvolveStatus::Infeasible;
    QVector<double> x;
    double f = 0;
    double violation = 0;
    int generations = 0;
};

// Every variable needs finite bounds (the caller checks Require Bounds).
[[nodiscard]] EvolveResult solveEvolutionary(const NonlinearProblem &problem, const QVector<double> &start,
                                             const EvolveOptions &options);

// ---- branch and bound ----

// What a relaxation answers: its status as one of three, the point and the
// objective (minimized).
struct Relaxation {
    enum class Status { Solved, Infeasible, Unbounded, Stopped, Failed };
    Status status = Status::Failed;
    QVector<double> x;
    double objective = 0;
};

// One node's problem: the bounds and the extra rows (a·x <= rhs) branching
// added. The callback solves the relaxation with them.
using RelaxationSolver =
    std::function<Relaxation(const QVector<double> &lower, const QVector<double> &upper, const QList<LinearRow> &cuts)>;

struct IntegerSpec {
    QVector<bool> integer;            // per variable (bin variables are integer in [0, 1])
    QList<QVector<int>> allDifferent; // groups of variables taking 1…n, each once
    double tolerance = 0.01;          // Integer Optimality: the accepted gap, a fraction
    qint64 maxSubproblems = 0;        // 0: unlimited
    qint64 maxFeasible = 0;           // 0: unlimited
    std::function<bool()> keepGoing;
    std::function<void(int subproblems, double best)> onSubproblem;
};

enum class BnbStatus { Optimal, WithinTolerance, Infeasible, Unbounded, Stopped, SubproblemLimit, FeasibleLimit, Failed };

struct BnbResult {
    BnbStatus status = BnbStatus::Infeasible;
    QVector<double> x;
    double objective = 0;
    int subproblems = 0;
};

[[nodiscard]] BnbResult branchAndBound(const QVector<double> &lower, const QVector<double> &upper,
                                       const IntegerSpec &spec, const RelaxationSolver &relax);

} // namespace QindaOffice::Calc::SolverMath
