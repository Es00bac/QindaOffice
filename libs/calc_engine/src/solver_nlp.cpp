// SPDX-License-Identifier: GPL-3.0-or-later
// GRG Nonlinear (solver_math.h, M90, D-098): a local method for smooth
// problems with bounds and general constraints, on finite differences.
//
// Bounds are honoured exactly: every point tried is projected into them.
// General constraints are folded into an augmented Lagrangian (Powell–
// Hestenes–Rockafellar): equalities λh + ρh²/2, inequalities
// (max(0, μ + ρg)² − μ²)/2ρ. Each outer round minimizes that function over
// the bounds with a projected quasi-Newton active-set search — variables
// held at a bound the gradient pushes against are fixed, BFGS works on the
// rest, a projected Armijo backtracking line search takes the step — then
// moves the multipliers and, when the violation did not fall to a quarter,
// raises ρ. The gradients are forward (or central) differences of the
// workbook: one evaluation gives the objective and every constraint, so a
// gradient costs n (2n) evaluations whatever the number of constraints.
// "Automatic scaling" divides the objective and each constraint by its
// size at the start, so the precision is relative to it.
//
// AGENT-NOTE: Excel's GRG2 is a reduced-gradient method proper; this is
// the sound local method the M90 brief allows in its place. Like GRG it
// finds a local optimum, holds bounds exactly and constraints to the
// precision, and ends with Excel's statuses: KKT conditions met (0),
// converged by the objective's relative change (1), no further progress
// (2), the iteration cap (3), unbounded (4), infeasible (5), stopped (6).

#include "solver_math.h"
#include "solver_nlp_values.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Calc::SolverMath {

namespace {
using namespace NlpDetail;

constexpr int kMaxOuter = 40;
constexpr double kHuge = 1e30;

class Scaled {
public:
    Scaled(const NonlinearProblem &problem, const NlpOptions &options)
        : m_problem(problem)
        , m_options(options)
        , m_n(int(problem.lower.size()))
    {
    }
    [[nodiscard]] int n() const { return m_n; }
    [[nodiscard]] int inequalities() const { return m_problem.inequalities + int(m_problem.linear.size()); }
    [[nodiscard]] int equalities() const { return m_problem.equalities; }
    [[nodiscard]] double lower(int k) const { return m_problem.lower.at(k); }
    [[nodiscard]] double upper(int k) const { return m_problem.upper.at(k); }
    [[nodiscard]] int evaluations() const { return m_evaluations; }

    [[nodiscard]] QVector<double> clamp(QVector<double> x) const
    {
        for (int k = 0; k < m_n; ++k) {
            x[k] = qBound(lower(k), x.at(k), upper(k));
        }
        return x;
    }

    // Unscaled values; false on an error value (or a non-finite one).
    bool raw(const QVector<double> &x, double *f, QVector<double> *g, QVector<double> *h)
    {
        ++m_evaluations;
        g->clear();
        h->clear();
        if (!m_problem.evaluate(x, f, g, h) || !std::isfinite(*f)) {
            return false;
        }
        g->resize(m_problem.inequalities);
        h->resize(m_problem.equalities);
        for (const LinearRow &row : m_problem.linear) {
            double value = -row.rhs;
            for (int k = 0; k < m_n && k < row.a.size(); ++k) {
                value += row.a.at(k) * x.at(k);
            }
            g->append(value);
        }
        for (const double v : std::as_const(*g)) {
            if (!std::isfinite(v)) {
                return false;
            }
        }
        for (const double v : std::as_const(*h)) {
            if (!std::isfinite(v)) {
                return false;
            }
        }
        return true;
    }

    void setScales(const Point &start)
    {
        m_fScale = m_options.scale ? qMax(1.0, std::abs(start.f)) : 1.0;
        m_gScale = QVector<double>(start.g.size(), 1.0);
        m_hScale = QVector<double>(start.h.size(), 1.0);
        if (m_options.scale) {
            for (int i = 0; i < start.g.size(); ++i) {
                m_gScale[i] = qMax(1.0, std::abs(start.g.at(i)));
            }
            for (int j = 0; j < start.h.size(); ++j) {
                m_hScale[j] = qMax(1.0, std::abs(start.h.at(j)));
            }
        }
    }

    bool evaluate(const QVector<double> &x, Point *p)
    {
        p->x = x;
        if (!raw(x, &p->f, &p->g, &p->h)) {
            return false;
        }
        p->f /= m_fScale;
        for (int i = 0; i < p->g.size() && i < m_gScale.size(); ++i) {
            p->g[i] /= m_gScale.at(i);
        }
        for (int j = 0; j < p->h.size() && j < m_hScale.size(); ++j) {
            p->h[j] /= m_hScale.at(j);
        }
        return true;
    }

    [[nodiscard]] double unscaledF(const Point &p) const { return p.f * m_fScale; }
    [[nodiscard]] double unscaledViolation(const Point &p) const
    {
        double worst = 0;
        for (int i = 0; i < p.g.size(); ++i) {
            worst = qMax(worst, p.g.at(i) * m_gScale.value(i, 1.0));
        }
        for (int j = 0; j < p.h.size(); ++j) {
            worst = qMax(worst, std::abs(p.h.at(j) * m_hScale.value(j, 1.0)));
        }
        return worst;
    }

    bool jacobian(const Point &base, Jacobian *jac)
    {
        jac->f = QVector<double>(m_n, 0.0);
        jac->g = QList<QVector<double>>(base.g.size(), QVector<double>(m_n, 0.0));
        jac->h = QList<QVector<double>>(base.h.size(), QVector<double>(m_n, 0.0));
        const double eps = m_options.central ? 6.06e-6 : 1.49e-8;
        for (int k = 0; k < m_n; ++k) {
            const double x = base.x.at(k);
            const double step = eps * qMax(1.0, std::abs(x));
            const bool upRoom = x + step <= upper(k);
            const bool downRoom = x - step >= lower(k);
            Point plus;
            Point minus;
            bool havePlus = false;
            bool haveMinus = false;
            if (upRoom) {
                QVector<double> moved = base.x;
                moved[k] = x + step;
                havePlus = evaluate(moved, &plus);
            }
            if ((m_options.central || !havePlus) && downRoom) {
                QVector<double> moved = base.x;
                moved[k] = x - step;
                haveMinus = evaluate(moved, &minus);
            }
            const auto column = [&](auto pick) {
                if (havePlus && haveMinus) {
                    return (pick(plus) - pick(minus)) / (2.0 * step);
                }
                return havePlus ? (pick(plus) - pick(base)) / step : (pick(base) - pick(minus)) / step;
            };
            if (!havePlus && !haveMinus) {
                if (upRoom || downRoom) {
                    return false; // an error value on both sides
                }
                continue;         // a fixed variable: no derivative needed
            }
            jac->f[k] = column([](const Point &p) { return p.f; });
            for (int i = 0; i < base.g.size(); ++i) {
                jac->g[i][k] = column([i](const Point &p) { return p.g.at(i); });
            }
            for (int j = 0; j < base.h.size(); ++j) {
                jac->h[j][k] = column([j](const Point &p) { return p.h.at(j); });
            }
        }
        return true;
    }

private:
    const NonlinearProblem &m_problem;
    const NlpOptions &m_options;
    int m_n;
    int m_evaluations = 0;
    double m_fScale = 1;
    QVector<double> m_gScale;
    QVector<double> m_hScale;
};

enum class Inner { Kkt, Stalled, NoProgress, Limit, Stopped, Unbounded, Error };

class Search {
public:
    Search(Scaled &problem, const NlpOptions &options)
        : m_p(problem)
        , m_options(options)
    {
        m_limit = options.maxIterations > 0 ? options.maxIterations : 2000 + 200 * qint64(problem.n());
    }
    [[nodiscard]] int iterations() const { return m_iterations; }

    // Minimizes the augmented Lagrangian over the bounds from `point`.
    Inner minimize(Point &point, const Multipliers &m)
    {
        const int n = m_p.n();
        Jacobian jac;
        if (!m_p.jacobian(point, &jac)) {
            return Inner::Error;
        }
        QVector<double> grad = gradient(point, jac, m);
        double value = lagrangian(point, m);
        // The first steps are steepest descent of length about 1 at most.
        double hScale = 1.0 / qMax(1.0, norm(grad));
        QVector<double> H = identity(n, hScale);
        QVector<bool> lastFree;
        int stall = 0;
        const double relTol = qMin(m_options.convergence, 1e-4) * 1e-4;
        for (;;) {
            if (m_options.keepGoing && !m_options.keepGoing()) {
                return Inner::Stopped;
            }
            QVector<bool> free(n, true);
            double pg = 0;
            for (int k = 0; k < n; ++k) {
                const double x = point.x.at(k);
                const bool atLower = x <= m_p.lower(k) + 1e-12 * (1.0 + std::abs(m_p.lower(k)));
                const bool atUpper = x >= m_p.upper(k) - 1e-12 * (1.0 + std::abs(m_p.upper(k)));
                if ((atLower && grad.at(k) > 0) || (atUpper && grad.at(k) < 0) || (atLower && atUpper)) {
                    free[k] = false;
                    continue;
                }
                pg = qMax(pg, std::abs(grad.at(k)));
            }
            const double gtol = (m_options.central ? 1e-8 : 1e-6) * (1.0 + std::abs(value));
            if (pg <= gtol) {
                return Inner::Kkt;
            }
            if (m_iterations >= m_limit) {
                return Inner::Limit;
            }
            if (free != lastFree) {
                H = identity(n, hScale);
                lastFree = free;
            }
            QVector<double> d = direction(H, grad, free);
            double slope = dot(grad, d);
            if (!(slope < 0)) {
                H = identity(n, hScale);
                d = direction(H, grad, free);
                slope = dot(grad, d);
            }
            Point trial;
            double trialValue = 0;
            if (!lineSearch(point, value, grad, d, m, &trial, &trialValue)) {
                // A worn-out quasi-Newton matrix: once more downhill.
                H = identity(n, hScale);
                d = direction(H, grad, free);
                if (!lineSearch(point, value, grad, d, m, &trial, &trialValue)) {
                    return Inner::NoProgress;
                }
            }
            ++m_iterations;
            Jacobian trialJac;
            if (!m_p.jacobian(trial, &trialJac)) {
                return Inner::Error;
            }
            const QVector<double> trialGrad = gradient(trial, trialJac, m);
            QVector<double> s(n, 0.0);
            QVector<double> y(n, 0.0);
            for (int k = 0; k < n; ++k) {
                if (free.at(k)) {
                    s[k] = trial.x.at(k) - point.x.at(k);
                    y[k] = trialGrad.at(k) - grad.at(k);
                }
            }
            const double sy = dot(s, y);
            const double yy = dot(y, y);
            if (sy > 1e-12 * std::sqrt(dot(s, s) * yy)) {
                bfgs(H, s, y, sy);
                hScale = sy / yy;
            }
            stall = std::abs(value - trialValue) <= relTol * (1.0 + std::abs(trialValue)) ? stall + 1 : 0;
            point = trial;
            value = trialValue;
            grad = trialGrad;
            if (m_options.onIteration) {
                m_options.onIteration(m_iterations, m_p.unscaledF(point));
            }
            if (std::abs(m_p.unscaledF(point)) > kHuge || norm(point.x) > kHuge) {
                return Inner::Unbounded;
            }
            if (stall >= 5) {
                return Inner::Stalled;
            }
        }
    }

private:
    static QVector<double> identity(int n, double scale)
    {
        QVector<double> h(n * n, 0.0);
        for (int k = 0; k < n; ++k) {
            h[k * n + k] = scale;
        }
        return h;
    }
    static double dot(const QVector<double> &a, const QVector<double> &b)
    {
        double sum = 0;
        for (int k = 0; k < a.size(); ++k) {
            sum += a.at(k) * b.at(k);
        }
        return sum;
    }
    static double norm(const QVector<double> &a)
    {
        double worst = 0;
        for (const double v : a) {
            worst = qMax(worst, std::abs(v));
        }
        return worst;
    }
    static QVector<double> direction(const QVector<double> &H, const QVector<double> &grad, const QVector<bool> &free)
    {
        const int n = int(grad.size());
        QVector<double> d(n, 0.0);
        for (int r = 0; r < n; ++r) {
            if (!free.at(r)) {
                continue;
            }
            double sum = 0;
            for (int c = 0; c < n; ++c) {
                if (free.at(c)) {
                    sum += H.at(r * n + c) * grad.at(c);
                }
            }
            d[r] = -sum;
        }
        return d;
    }
    static void bfgs(QVector<double> &H, const QVector<double> &s, const QVector<double> &y, double sy)
    {
        // H ← (I − ρsyᵀ) H (I − ρysᵀ) + ρssᵀ, ρ = 1/sᵀy.
        const int n = int(s.size());
        const double rho = 1.0 / sy;
        QVector<double> Hy(n, 0.0);
        for (int r = 0; r < n; ++r) {
            for (int c = 0; c < n; ++c) {
                Hy[r] += H.at(r * n + c) * y.at(c);
            }
        }
        const double yHy = dot(y, Hy);
        for (int r = 0; r < n; ++r) {
            for (int c = 0; c < n; ++c) {
                H[r * n + c] += -rho * (s.at(r) * Hy.at(c) + Hy.at(r) * s.at(c)) + (rho * rho * yHy + rho) * s.at(r) * s.at(c);
            }
        }
    }

    // Projected Armijo backtracking along d; the first step is capped so a
    // trial never lands absurdly far away.
    bool lineSearch(const Point &point, double value, const QVector<double> &grad, const QVector<double> &d,
                    const Multipliers &m, Point *out, double *outValue)
    {
        const double length = norm(d);
        if (length == 0.0) {
            return false;
        }
        double alpha = qMin(1.0, 1e3 * qMax(1.0, norm(point.x)) / length);
        for (int attempt = 0; attempt < 40; ++attempt) {
            QVector<double> moved = point.x;
            for (int k = 0; k < moved.size(); ++k) {
                moved[k] += alpha * d.at(k);
            }
            moved = m_p.clamp(moved);
            Point trial;
            if (m_p.evaluate(moved, &trial)) {
                const double trialValue = lagrangian(trial, m);
                double decrease = 0;
                for (int k = 0; k < moved.size(); ++k) {
                    decrease += grad.at(k) * (moved.at(k) - point.x.at(k));
                }
                if (decrease < 0 && trialValue <= value + 1e-4 * decrease) {
                    *out = trial;
                    *outValue = trialValue;
                    return true;
                }
            }
            alpha *= 0.5;
        }
        return false;
    }

    Scaled &m_p;
    const NlpOptions &m_options;
    qint64 m_limit = 0;
    int m_iterations = 0;
};

} // namespace

NlpResult solveNonlinear(const NonlinearProblem &problem, const QVector<double> &start, const NlpOptions &options)
{
    NlpResult result;
    Scaled p(problem, options);
    Point point;
    if (!p.evaluate(p.clamp(start), &point)) {
        result.status = NlpStatus::ErrorValue;
        result.x = p.clamp(start);
        return result;
    }
    p.setScales(point);
    static_cast<void>(p.evaluate(point.x, &point)); // the same point, scaled
    Multipliers m;
    m.mu = QVector<double>(point.g.size(), 0.0);
    m.lambda = QVector<double>(point.h.size(), 0.0);
    Search search(p, options);
    const bool constrained = !point.g.isEmpty() || !point.h.isEmpty();
    // σ measures feasibility and complementarity together at the
    // multipliers just used — |max(g, −μ/ρ)| per inequality, |h| per
    // equality: a solution has σ = 0, and a constraint still holding a
    // multiplier while it has slack keeps σ above zero.
    double previous = kInfinity;
    Inner last = Inner::Kkt;
    int stuck = 0; // outer rounds at a large ρ that did not shrink σ
    for (int outer = 0; outer < kMaxOuter && stuck < 4; ++outer) {
        last = search.minimize(point, m);
        if (last == Inner::Stopped || last == Inner::Unbounded || last == Inner::Error || last == Inner::Limit) {
            break;
        }
        if (!constrained) {
            break;
        }
        double sigma = 0;
        for (int i = 0; i < point.g.size(); ++i) {
            sigma = qMax(sigma, std::abs(qMax(point.g.at(i), -m.mu.at(i) / m.rho)));
            m.mu[i] = qMax(0.0, m.mu.at(i) + m.rho * point.g.at(i));
        }
        for (int j = 0; j < point.h.size(); ++j) {
            sigma = qMax(sigma, std::abs(point.h.at(j)));
            m.lambda[j] += m.rho * point.h.at(j);
        }
        if (sigma <= options.precision) {
            break;
        }
        if (sigma > 0.25 * previous) {
            m.rho = qMin(m.rho * 10.0, 1e10);
        }
        stuck = m.rho >= 1e6 && sigma >= 0.9 * previous ? stuck + 1 : 0;
        previous = sigma;
    }
    result.x = point.x;
    result.f = p.unscaledF(point);
    result.violation = p.unscaledViolation(point);
    result.iterations = search.iterations();
    switch (last) {
    case Inner::Stopped: result.status = NlpStatus::Stopped; return result;
    case Inner::Unbounded: result.status = NlpStatus::Unbounded; return result;
    case Inner::Error: result.status = NlpStatus::ErrorValue; return result;
    default: break;
    }
    if (scaledViolation(point) > options.precision) {
        result.status = NlpStatus::Infeasible;
    } else if (last == Inner::Limit) {
        result.status = NlpStatus::IterationLimit;
    } else if (last == Inner::Kkt) {
        result.status = NlpStatus::Optimal;
    } else if (last == Inner::Stalled) {
        result.status = NlpStatus::Converged;
    } else {
        result.status = NlpStatus::CannotImprove;
    }
    return result;
}

} // namespace QindaOffice::Calc::SolverMath
