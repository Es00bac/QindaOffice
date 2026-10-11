// SPDX-License-Identifier: GPL-3.0-or-later
// Solving a Solver model on the workbook (solver.h, M90, D-098).
//
// The model is read against the cells (solver_cells.cpp), then:
//   Simplex LP — the model is measured at the starting values and one step
//     along each variable (n + 1 recalculations: the objective's and each
//     constraint's coefficients), checked at two more points, solved as a
//     linear program and checked again at the answer; a model that does not
//     keep to its straight lines ends with Excel's "The linearity conditions
//     required by this LP Solver are not satisfied." (status 7).
//   GRG Nonlinear — the nonlinear search over the cells themselves.
//   Evolutionary — differential evolution, every variable bounded (unless
//     Require Bounds on Variables is off).
// int, bin and dif constraints run branch and bound over the method's
// relaxation (Ignore Integer Constraints drops them). The answer goes into
// the variable cells; nothing worth keeping (a non-linear model, an
// unbounded one, an error value) puts the cells' own content back.

#include "solver.h"

#include "sheet.h"
#include "solver_internal.h"
#include "solver_math.h"
#include "what_if_internal.h"
#include "workbook.h"

#include <QElapsedTimer>

#include <cmath>

namespace QindaOffice::Calc {

using namespace SolverDetail;
using namespace SolverMath;

namespace {

class Run {
public:
    Run(Workbook &book, qint32 sheet, const SolverModel &model, const Resolved &resolved, const SolverCallbacks &callbacks)
        : book(book)
        , sheet(sheet)
        , model(model)
        , resolved(resolved)
        , evaluator(book, sheet, resolved)
        , m_callbacks(callbacks)
    {
        m_clock.start();
        for (int i = 0; i < resolved.rows.size(); ++i) {
            if (!resolved.rows.at(i).bound) {
                general.append(i);
                (resolved.rows.at(i).relation == SolverRelation::Equal ? equalities : inequalities) += 1;
            }
        }
        if (model.goal == SolverGoal::ValueOf && resolved.objective.has_value()) {
            ++equalities;
        }
    }

    bool keepGoing()
    {
        if (m_stopped) {
            return false;
        }
        if (model.options.maxTime > 0 && double(m_clock.elapsed()) / 1000.0 >= model.options.maxTime) {
            m_timeUp = true;
            return false;
        }
        if (model.options.iterations > 0 && m_trials >= model.options.iterations) {
            m_capped = true;
            return false;
        }
        return true;
    }
    void trial(int subproblems)
    {
        ++m_trials;
        double objective = 0;
        static_cast<void>(evaluator.objective(&objective));
        if (m_callbacks.progress && !m_callbacks.progress({m_trials, subproblems, objective})) {
            m_stopped = true;
        }
    }
    [[nodiscard]] SolverStatus stopStatus() const
    {
        return m_timeUp ? SolverStatus::TimeLimit : (m_capped ? SolverStatus::IterationLimit : SolverStatus::Stopped);
    }
    [[nodiscard]] int trials() const { return m_trials; }
    [[nodiscard]] double seconds() const { return double(m_clock.elapsed()) / 1000.0; }
    [[nodiscard]] double sign() const { return model.goal == SolverGoal::Max ? -1.0 : 1.0; }
    [[nodiscard]] bool hasGoal() const { return resolved.objective.has_value() && model.goal != SolverGoal::ValueOf; }

    // The functions at x: the objective, then each general row's lhs − rhs.
    bool values(const QVector<double> &x, QVector<double> *out)
    {
        evaluator.set(x);
        out->clear();
        double objective = 0;
        if (!evaluator.objective(&objective)) {
            return false;
        }
        out->append(objective);
        for (const int i : std::as_const(general)) {
            double lhs = 0;
            double rhs = 0;
            if (!evaluator.row(i, &lhs, &rhs)) {
                return false;
            }
            out->append(lhs - rhs);
        }
        return true;
    }

    // The nonlinear problem's view of the same functions.
    bool nonlinear(const QVector<double> &x, double *f, QVector<double> *g, QVector<double> *h)
    {
        QVector<double> v;
        if (!values(x, &v)) {
            return false;
        }
        *f = hasGoal() ? sign() * v.at(0) : 0.0;
        for (int at = 0; at < general.size(); ++at) {
            switch (resolved.rows.at(general.at(at)).relation) {
            case SolverRelation::LessEqual: g->append(v.at(at + 1)); break;
            case SolverRelation::GreaterEqual: g->append(-v.at(at + 1)); break;
            default: h->append(v.at(at + 1)); break;
            }
        }
        if (model.goal == SolverGoal::ValueOf && resolved.objective.has_value()) {
            h->append(v.at(0) - model.valueOf);
        }
        for (const QVector<int> &group : allDifferent) {
            int repeats = 0; // Evolutionary only: dif as a count of equal pairs
            for (int a = 0; a < group.size(); ++a) {
                for (int b = a + 1; b < group.size(); ++b) {
                    repeats += std::round(x.at(group.at(a))) == std::round(x.at(group.at(b))) ? 1 : 0;
                }
            }
            h->append(repeats);
        }
        return true;
    }

    Workbook &book;
    qint32 sheet;
    const SolverModel &model;
    const Resolved &resolved;
    Evaluator evaluator;
    QList<int> general; // rows that are not bounds
    int inequalities = 0;
    int equalities = 0;
    QList<QVector<int>> allDifferent; // set for Evolutionary

private:
    const SolverCallbacks &m_callbacks;
    QElapsedTimer m_clock;
    int m_trials = 0;
    bool m_stopped = false;
    bool m_timeUp = false;
    bool m_capped = false;
};

struct Answer {
    SolverStatus status = SolverStatus::Infeasible;
    QVector<double> x;
    bool keep = false; // put x into the cells
    int subproblems = 0;
};

bool hasIntegers(const Resolved &r, const SolverModel &model)
{
    return !model.options.ignoreIntegers && (r.integer.contains(true) || !r.allDifferent.isEmpty());
}

IntegerSpec integerSpec(Run &run)
{
    IntegerSpec spec;
    spec.integer = run.resolved.integer;
    spec.allDifferent = run.resolved.allDifferent;
    spec.tolerance = qMax(0.0, run.model.options.integerOptimality);
    spec.maxSubproblems = run.model.options.maxSubproblems;
    spec.maxFeasible = run.model.options.maxFeasible;
    spec.keepGoing = [&run] { return run.keepGoing(); };
    spec.onSubproblem = [&run](int subproblems, double) { run.trial(subproblems); };
    return spec;
}

SolverStatus fromBnb(BnbStatus status, const Run &run)
{
    switch (status) {
    case BnbStatus::Optimal: return SolverStatus::Optimal;
    case BnbStatus::WithinTolerance: return SolverStatus::IntegerOptimal;
    case BnbStatus::Unbounded: return SolverStatus::NotConverging;
    case BnbStatus::Stopped: return run.stopStatus();
    case BnbStatus::SubproblemLimit: return SolverStatus::SubproblemLimit;
    case BnbStatus::FeasibleLimit: return SolverStatus::FeasibleLimit;
    case BnbStatus::Infeasible:
    case BnbStatus::Failed: break;
    }
    return SolverStatus::Infeasible;
}

bool close(double actual, double predicted, double size)
{
    return std::abs(actual - predicted) <= 1e-7 * (1.0 + std::abs(actual) + size);
}

Answer solveLinear(Run &run, const QVector<double> &x0)
{
    Answer answer;
    const int n = int(x0.size());
    QVector<double> f0;
    if (!run.values(x0, &f0)) {
        answer.status = SolverStatus::ErrorValue;
        return answer;
    }
    const int m = int(f0.size());
    QList<QVector<double>> jac(m, QVector<double>(n, 0.0)); // jac[function][variable]
    for (int k = 0; k < n; ++k) {
        QVector<double> x = x0;
        x[k] += 1.0;
        QVector<double> fk;
        if (!run.values(x, &fk)) {
            answer.status = SolverStatus::ErrorValue;
            return answer;
        }
        for (int i = 0; i < m; ++i) {
            jac[i][k] = fk.at(i) - f0.at(i);
        }
    }
    // The straight lines must hold elsewhere too, and at the answer.
    const auto linearAt = [&](const QVector<double> &x) {
        QVector<double> actual;
        if (!run.values(x, &actual)) {
            return false;
        }
        for (int i = 0; i < m; ++i) {
            double predicted = f0.at(i);
            double size = std::abs(f0.at(i));
            for (int k = 0; k < n; ++k) {
                predicted += jac.at(i).at(k) * (x.at(k) - x0.at(k));
                size += std::abs(jac.at(i).at(k) * (x.at(k) - x0.at(k)));
            }
            if (!close(actual.at(i), predicted, size)) {
                return false;
            }
        }
        return true;
    };
    QVector<double> probe = x0;
    QVector<double> back = x0;
    for (int k = 0; k < n; ++k) {
        const double d = 0.5 + 0.25 * double((k * 7 + 3) % 5);
        probe[k] += d;
        back[k] -= 1.5 * d;
    }
    if (!linearAt(probe) || !linearAt(back)) {
        answer.status = SolverStatus::NotLinear;
        return answer;
    }
    const auto program = [&](const QVector<double> &lower, const QVector<double> &upper, const QList<LinearRow> &cuts) {
        LinearProgram lp;
        lp.lower = lower;
        lp.upper = upper;
        lp.c = QVector<double>(n, 0.0);
        if (run.hasGoal()) {
            for (int k = 0; k < n; ++k) {
                lp.c[k] = run.sign() * jac.at(0).at(k);
            }
        }
        const auto rowFor = [&](int function, int sense, double target) {
            LinearRow row;
            row.a = jac.at(function);
            row.sense = sense;
            row.rhs = target - f0.at(function);
            for (int k = 0; k < n; ++k) {
                row.rhs += jac.at(function).at(k) * x0.at(k);
            }
            return row;
        };
        for (int at = 0; at < run.general.size(); ++at) {
            const SolverRelation relation = run.resolved.rows.at(run.general.at(at)).relation;
            lp.rows.append(rowFor(at + 1, relation == SolverRelation::LessEqual ? -1 : (relation == SolverRelation::GreaterEqual ? 1 : 0), 0.0));
        }
        if (run.model.goal == SolverGoal::ValueOf && run.resolved.objective.has_value()) {
            lp.rows.append(rowFor(0, 0, run.model.valueOf));
        }
        lp.rows += cuts;
        return lp;
    };
    const auto keepGoing = [&run] { return run.keepGoing(); };
    if (hasIntegers(run.resolved, run.model)) {
        const BnbResult result = branchAndBound(run.resolved.lower, run.resolved.upper, integerSpec(run),
                                                [&](const QVector<double> &lower, const QVector<double> &upper, const QList<LinearRow> &cuts) {
                                                    const LpResult lp = solveLinearProgram(program(lower, upper, cuts), keepGoing);
                                                    Relaxation relaxed;
                                                    relaxed.x = lp.x;
                                                    relaxed.objective = lp.objective;
                                                    relaxed.status = lp.status == LpStatus::Optimal ? Relaxation::Status::Solved
                                                        : lp.status == LpStatus::Infeasible     ? Relaxation::Status::Infeasible
                                                        : lp.status == LpStatus::Unbounded      ? Relaxation::Status::Unbounded
                                                                                                : Relaxation::Status::Stopped;
                                                    return relaxed;
                                                });
        answer.status = fromBnb(result.status, run);
        answer.x = result.x;
        answer.subproblems = result.subproblems;
    } else {
        const LpResult result = solveLinearProgram(program(run.resolved.lower, run.resolved.upper, {}), keepGoing);
        run.trial(0);
        answer.x = result.x;
        answer.status = result.status == LpStatus::Optimal ? SolverStatus::Optimal
            : result.status == LpStatus::Infeasible        ? SolverStatus::Infeasible
            : result.status == LpStatus::Unbounded         ? SolverStatus::NotConverging
                                                           : run.stopStatus();
    }
    if (answer.x.size() != n) {
        return answer; // nothing to keep
    }
    for (double &v : answer.x) {
        // The simplex's rounding: 4.9999999999999 is 5.
        if (std::abs(v - std::round(v)) <= 1e-9 * qMax(1.0, std::abs(v))) {
            v = std::round(v);
        }
    }
    if (!linearAt(answer.x)) {
        answer.status = SolverStatus::NotLinear;
        return answer;
    }
    answer.keep = true;
    return answer;
}

SolverStatus fromNlp(NlpStatus status, const Run &run)
{
    switch (status) {
    case NlpStatus::Optimal: return SolverStatus::Optimal;
    case NlpStatus::Converged: return SolverStatus::Converged;
    case NlpStatus::CannotImprove: return SolverStatus::CannotImprove;
    case NlpStatus::IterationLimit: return SolverStatus::IterationLimit;
    case NlpStatus::Unbounded: return SolverStatus::NotConverging;
    case NlpStatus::Stopped: return run.stopStatus();
    case NlpStatus::ErrorValue: return SolverStatus::ErrorValue;
    case NlpStatus::Infeasible: break;
    }
    return SolverStatus::Infeasible;
}

NonlinearProblem problemFor(Run &run)
{
    NonlinearProblem problem;
    problem.inequalities = run.inequalities;
    problem.equalities = run.equalities + int(run.allDifferent.size());
    problem.lower = run.resolved.lower;
    problem.upper = run.resolved.upper;
    problem.evaluate = [&run](const QVector<double> &x, double *f, QVector<double> *g, QVector<double> *h) {
        return run.nonlinear(x, f, g, h);
    };
    return problem;
}

Answer solveSmooth(Run &run, const QVector<double> &x0)
{
    Answer answer;
    NlpOptions options;
    options.precision = run.model.options.precision;
    options.convergence = run.model.options.convergence;
    options.central = run.model.options.centralDerivatives;
    options.scale = run.model.options.autoScale;
    options.maxIterations = run.model.options.iterations;
    options.keepGoing = [&run] { return run.keepGoing(); };
    options.onIteration = [&run](int, double) { run.trial(0); };
    const NonlinearProblem base = problemFor(run);
    if (hasIntegers(run.resolved, run.model)) {
        IntegerSpec spec = integerSpec(run);
        options.onIteration = {};
        const BnbResult result = branchAndBound(base.lower, base.upper, spec,
                                                [&](const QVector<double> &lower, const QVector<double> &upper, const QList<LinearRow> &cuts) {
                                                    NonlinearProblem node = base;
                                                    node.lower = lower;
                                                    node.upper = upper;
                                                    node.linear = cuts;
                                                    const NlpResult nlp = solveNonlinear(node, x0, options);
                                                    Relaxation relaxed;
                                                    relaxed.x = nlp.x;
                                                    relaxed.objective = nlp.f;
                                                    switch (nlp.status) {
                                                    case NlpStatus::Infeasible: relaxed.status = Relaxation::Status::Infeasible; break;
                                                    case NlpStatus::Unbounded: relaxed.status = Relaxation::Status::Unbounded; break;
                                                    case NlpStatus::Stopped: relaxed.status = Relaxation::Status::Stopped; break;
                                                    case NlpStatus::ErrorValue: relaxed.status = Relaxation::Status::Failed; break;
                                                    default: relaxed.status = Relaxation::Status::Solved; break;
                                                    }
                                                    return relaxed;
                                                });
        answer.status = fromBnb(result.status, run);
        answer.x = result.x;
        answer.subproblems = result.subproblems;
        answer.keep = answer.x.size() == x0.size();
        return answer;
    }
    const NlpResult result = solveNonlinear(base, x0, options);
    answer.status = fromNlp(result.status, run);
    answer.x = result.x;
    answer.keep = result.x.size() == x0.size() && result.status != NlpStatus::Unbounded
        && result.status != NlpStatus::ErrorValue;
    return answer;
}

Answer solveEvolving(Run &run, const QVector<double> &x0)
{
    Answer answer;
    NonlinearProblem problem;
    if (!run.model.options.ignoreIntegers) {
        run.allDifferent = run.resolved.allDifferent;
    }
    problem = problemFor(run);
    for (int k = 0; k < problem.lower.size(); ++k) {
        if (!std::isfinite(problem.lower.at(k)) || !std::isfinite(problem.upper.at(k))) {
            if (run.model.options.requireBounds) {
                answer.status = SolverStatus::NeedsBounds;
                return answer;
            }
            const double reach = 100.0 * qMax(1.0, std::abs(x0.at(k)));
            problem.lower[k] = std::isfinite(problem.lower.at(k)) ? problem.lower.at(k) : x0.at(k) - reach;
            problem.upper[k] = std::isfinite(problem.upper.at(k)) ? problem.upper.at(k) : x0.at(k) + reach;
        }
    }
    EvolveOptions options;
    options.population = run.model.options.populationSize;
    options.seed = run.model.options.randomSeed;
    options.mutationRate = run.model.options.mutationRate;
    options.convergence = run.model.options.convergence;
    options.precision = run.model.options.precision;
    options.maxSecondsWithoutImprovement = run.model.options.maxTimeWithoutImprovement;
    options.maxGenerations = run.model.options.iterations;
    options.integer = run.model.options.ignoreIntegers ? QVector<bool>() : run.resolved.integer;
    options.keepGoing = [&run] { return run.keepGoing(); };
    options.onIteration = [&run](int, double) { run.trial(0); };
    const EvolveResult result = solveEvolutionary(problem, x0, options);
    answer.x = result.x;
    switch (result.status) {
    case EvolveStatus::Converged: answer.status = SolverStatus::GlobalProbable; break;
    case EvolveStatus::NoImprovement: answer.status = SolverStatus::CannotImprove; break;
    case EvolveStatus::Stopped: answer.status = run.stopStatus(); break;
    case EvolveStatus::IterationLimit: answer.status = SolverStatus::IterationLimit; break;
    case EvolveStatus::ErrorValue: answer.status = SolverStatus::ErrorValue; break;
    case EvolveStatus::Infeasible: answer.status = SolverStatus::Infeasible; break;
    }
    answer.keep = result.x.size() == x0.size() && result.status != EvolveStatus::ErrorValue;
    return answer;
}

} // namespace

QString solverModelProblem(const Workbook &book, qint32 sheet, const SolverModel &model)
{
    Resolved resolved;
    return resolve(book, sheet, model, &resolved);
}

SolverOutcome solveModel(Workbook &book, qint32 sheet, const SolverModel &model, const SolverCallbacks &callbacks)
{
    SolverOutcome out;
    Resolved resolved;
    out.message = resolve(book, sheet, model, &resolved);
    if (!out.message.isEmpty()) {
        out.status = out.message.contains(QLatin1String("too large")) ? SolverStatus::TooLarge : SolverStatus::ModelError;
        return out;
    }
    out.started = true;
    out.variables = resolved.variables;
    out.objective = resolved.objective;
    for (int k = 0; k < resolved.variables.size(); ++k) {
        out.variableKinds.append(resolved.binary.at(k) ? 2 : (resolved.integer.at(k) ? 1 : 0));
    }
    QList<WhatIf::SavedCell> saved;
    QVector<double> x0;
    for (const CellAddr &cell : std::as_const(resolved.variables)) {
        saved.append(WhatIf::saveCell(book, sheet, cell));
        x0.append(numberIn(book.cellValue(sheet, cell.row, cell.col)).value_or(0.0));
    }
    out.originalValues = x0;
    if (resolved.objective.has_value()) {
        out.originalObjective = book.cellValue(sheet, resolved.objective->row, resolved.objective->col);
    }
    Run run(book, sheet, model, resolved, callbacks);
    Answer answer;
    bool conflict = false;
    for (int k = 0; k < resolved.lower.size(); ++k) {
        conflict = conflict
            || ((resolved.binary.at(k) || resolved.integer.at(k)) && resolved.lower.at(k) > resolved.upper.at(k) + 1e-9);
    }
    if (conflict && !model.options.ignoreIntegers) {
        answer.status = SolverStatus::BoundsConflict;
    } else if (model.method == SolverMethod::SimplexLp) {
        answer = solveLinear(run, x0);
    } else if (model.method == SolverMethod::Evolutionary) {
        answer = solveEvolving(run, x0);
    } else {
        answer = solveSmooth(run, x0);
    }
    if (answer.keep) {
        run.evaluator.set(answer.x);
        out.values = answer.x;
        out.changed = true;
    } else {
        QSet<CellKey> keys;
        for (const WhatIf::SavedCell &cell : std::as_const(saved)) {
            keys.insert(WhatIf::restoreCell(book, sheet, cell));
        }
        book.recalc(keys);
        out.values = x0;
    }
    out.status = answer.status;
    out.message = solverStatusMessage(answer.status);
    out.iterations = run.trials();
    out.subproblems = answer.subproblems;
    out.seconds = run.seconds();
    if (resolved.objective.has_value()) {
        out.finalObjective = book.cellValue(sheet, resolved.objective->row, resolved.objective->col);
    }
    out.constraints = reportRows(book, sheet, resolved, model.options.precision);
    return out;
}

} // namespace QindaOffice::Calc
