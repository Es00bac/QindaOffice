// SPDX-License-Identifier: GPL-3.0-or-later
// Simplex LP (solver_math.h, M90, D-098): a dense two-phase primal simplex.
//
// The program's variables are first made non-negative: one with a finite
// lower bound is l + y, one with only an upper bound u − y, a free one
// y⁺ − y⁻; a finite upper bound of the first kind becomes a row y ≤ u − l.
// Rows are scaled to a largest coefficient of 1 and turned to a
// non-negative right side. Phase 1 minimizes the artificial variables of
// the ≥ and = rows (a positive optimum: no feasible solution); artificial
// variables still basic at zero are pivoted out where their row allows it
// (otherwise the row is redundant and stays inert); phase 2 minimizes the
// objective with the artificial columns barred. Dantzig's rule picks the
// entering column, the ratio test breaks ties by the lowest basic column,
// and after a run of degenerate pivots Bland's rule takes over so the
// method cannot cycle.

#include "solver_math.h"

#include <QtGlobal>

#include <cmath>

namespace QindaOffice::Calc::SolverMath {

namespace {

constexpr double kPivotTolerance = 1e-9;
constexpr double kCostTolerance = 1e-9;
constexpr int kBlandAfter = 30; // degenerate pivots in a row before Bland's rule

// x_j = offset + sign · y[col] (− y[col2] for a free variable).
struct Substitution {
    double offset = 0;
    double sign = 1;
    int col = -1;
    int col2 = -1;
};

// Rows 0 … rows−1 are the constraints, row `rows` the objective: reduced
// costs in its columns and minus the objective's value in its right side.
class Tableau {
public:
    Tableau(int rows, int cols)
        : m_rows(rows)
        , m_cols(cols)
        , m_cells(qsizetype(rows + 1) * qsizetype(cols + 1), 0.0)
        , m_basis(rows, -1)
    {
    }
    [[nodiscard]] int rows() const { return m_rows; }
    [[nodiscard]] int cols() const { return m_cols; }
    double &at(int r, int c) { return m_cells[qsizetype(r) * (m_cols + 1) + c]; }
    [[nodiscard]] double at(int r, int c) const { return m_cells[qsizetype(r) * (m_cols + 1) + c]; }
    double &rhs(int r) { return at(r, m_cols); }
    [[nodiscard]] double rhs(int r) const { return at(r, m_cols); }
    [[nodiscard]] int basis(int r) const { return m_basis[r]; }
    void setBasis(int r, int c) { m_basis[r] = c; }

    void pivot(int pr, int pc)
    {
        const double p = at(pr, pc);
        QVector<int> nonzero;
        for (int c = 0; c <= m_cols; ++c) {
            if (at(pr, c) != 0.0) {
                at(pr, c) /= p;
                nonzero.append(c);
            }
        }
        at(pr, pc) = 1.0;
        for (int r = 0; r <= m_rows; ++r) {
            const double factor = r == pr ? 0.0 : at(r, pc);
            if (factor == 0.0) {
                continue;
            }
            for (const int c : std::as_const(nonzero)) {
                at(r, c) -= factor * at(pr, c);
            }
            at(r, pc) = 0.0;
            if (r < m_rows && rhs(r) < 0.0 && rhs(r) > -1e-11) {
                rhs(r) = 0.0; // rounding below a bound that cannot be crossed
            }
        }
        m_basis[pr] = pc;
    }

private:
    int m_rows;
    int m_cols;
    QVector<double> m_cells;
    QVector<int> m_basis;
};

LpStatus runSimplex(Tableau &t, const QVector<bool> &allowed, int *pivots, const std::function<bool()> &keepGoing)
{
    const int m = t.rows();
    const int maxPivots = 50000 + 50 * (m + t.cols());
    int degenerate = 0;
    for (;;) {
        if (keepGoing && (*pivots % 16) == 0 && !keepGoing()) {
            return LpStatus::Stopped;
        }
        if (*pivots > maxPivots) {
            return LpStatus::Stopped;
        }
        const bool bland = degenerate > kBlandAfter;
        int enter = -1;
        double best = -kCostTolerance;
        for (int c = 0; c < t.cols(); ++c) {
            if (!allowed.at(c)) {
                continue;
            }
            const double d = t.at(m, c);
            if (d >= -kCostTolerance) {
                continue;
            }
            if (bland) {
                enter = c;
                break;
            }
            if (d < best) {
                best = d;
                enter = c;
            }
        }
        if (enter < 0) {
            return LpStatus::Optimal;
        }
        int leave = -1;
        double ratio = kInfinity;
        for (int r = 0; r < m; ++r) {
            const double a = t.at(r, enter);
            if (a <= kPivotTolerance) {
                continue;
            }
            const double q = t.rhs(r) / a;
            if (leave < 0 || q < ratio - 1e-12 || (q <= ratio + 1e-12 && t.basis(r) < t.basis(leave))) {
                ratio = q;
                leave = r;
            }
        }
        if (leave < 0) {
            return LpStatus::Unbounded;
        }
        degenerate = ratio <= 1e-12 ? degenerate + 1 : 0;
        t.pivot(leave, enter);
        ++*pivots;
    }
}

} // namespace

LpResult solveLinearProgram(const LinearProgram &lp, const std::function<bool()> &keepGoing)
{
    LpResult result;
    const int n = int(lp.c.size());
    QVector<Substitution> subs(n);
    QList<std::pair<int, double>> upperRows;
    int ycols = 0;
    for (int j = 0; j < n; ++j) {
        const double lo = j < lp.lower.size() ? lp.lower.at(j) : -kInfinity;
        const double hi = j < lp.upper.size() ? lp.upper.at(j) : kInfinity;
        if (lo > hi + 1e-9 * (1.0 + std::abs(hi))) {
            result.status = LpStatus::Infeasible;
            return result;
        }
        Substitution &s = subs[j];
        if (std::isfinite(lo)) {
            s = {lo, 1.0, ycols++, -1};
            if (std::isfinite(hi)) {
                upperRows.append({s.col, qMax(0.0, hi - lo)});
            }
        } else if (std::isfinite(hi)) {
            s = {hi, -1.0, ycols++, -1};
        } else {
            s.offset = 0;
            s.sign = 1.0;
            s.col = ycols++;
            s.col2 = ycols++;
        }
    }
    QList<LinearRow> rows;
    for (const LinearRow &row : lp.rows) {
        LinearRow out;
        out.a = QVector<double>(ycols, 0.0);
        out.sense = row.sense;
        out.rhs = row.rhs;
        for (int j = 0; j < n && j < row.a.size(); ++j) {
            const double a = row.a.at(j);
            if (a == 0.0) {
                continue;
            }
            out.rhs -= a * subs.at(j).offset;
            out.a[subs.at(j).col] += a * subs.at(j).sign;
            if (subs.at(j).col2 >= 0) {
                out.a[subs.at(j).col2] -= a;
            }
        }
        rows.append(out);
    }
    for (const auto &[col, limit] : std::as_const(upperRows)) {
        LinearRow out;
        out.a = QVector<double>(ycols, 0.0);
        out.a[col] = 1.0;
        out.sense = -1;
        out.rhs = limit;
        rows.append(out);
    }
    QVector<double> cy(ycols, 0.0);
    for (int j = 0; j < n; ++j) {
        cy[subs.at(j).col] += lp.c.at(j) * subs.at(j).sign;
        if (subs.at(j).col2 >= 0) {
            cy[subs.at(j).col2] -= lp.c.at(j);
        }
    }
    // Scale every row to a largest coefficient of 1, a right side ≥ 0.
    QList<LinearRow> kept;
    double rhsSize = 0;
    for (LinearRow row : std::as_const(rows)) {
        double scale = 0;
        for (const double a : std::as_const(row.a)) {
            scale = qMax(scale, std::abs(a));
        }
        if (scale <= 1e-14) {
            const double tol = 1e-9 * (1.0 + std::abs(row.rhs));
            const bool holds = row.sense < 0 ? row.rhs >= -tol : (row.sense > 0 ? row.rhs <= tol : std::abs(row.rhs) <= tol);
            if (!holds) {
                result.status = LpStatus::Infeasible;
                return result;
            }
            continue;
        }
        for (double &a : row.a) {
            a /= scale;
        }
        row.rhs /= scale;
        if (row.rhs < 0) {
            for (double &a : row.a) {
                a = -a;
            }
            row.rhs = -row.rhs;
            row.sense = -row.sense;
        }
        rhsSize += row.rhs;
        kept.append(row);
    }
    const int m = int(kept.size());
    int slackCount = 0;
    int artificialCount = 0;
    for (const LinearRow &row : std::as_const(kept)) {
        slackCount += row.sense != 0 ? 1 : 0;
        artificialCount += row.sense >= 0 ? 1 : 0;
    }
    const int cols = ycols + slackCount + artificialCount;
    Tableau t(m, cols);
    QVector<bool> artificial(cols, false);
    int slackCol = ycols;
    int artificialCol = ycols + slackCount;
    for (int r = 0; r < m; ++r) {
        const LinearRow &row = kept.at(r);
        for (int c = 0; c < ycols; ++c) {
            t.at(r, c) = row.a.at(c);
        }
        t.rhs(r) = row.rhs;
        if (row.sense < 0) {
            t.at(r, slackCol) = 1.0;
            t.setBasis(r, slackCol++);
            continue;
        }
        if (row.sense > 0) {
            t.at(r, slackCol++) = -1.0;
        }
        t.at(r, artificialCol) = 1.0;
        artificial[artificialCol] = true;
        t.setBasis(r, artificialCol++);
    }
    if (artificialCount > 0) {
        // Phase 1: minimize the sum of the artificial variables.
        for (int r = 0; r < m; ++r) {
            if (artificial.at(t.basis(r))) {
                for (int c = 0; c <= cols; ++c) {
                    t.at(m, c) -= t.at(r, c);
                }
            }
        }
        for (int c = 0; c < cols; ++c) {
            if (artificial.at(c)) {
                t.at(m, c) += 1.0;
            }
        }
        const QVector<bool> all(cols, true);
        if (runSimplex(t, all, &result.pivots, keepGoing) == LpStatus::Stopped) {
            result.status = LpStatus::Stopped;
            return result;
        }
        if (-t.rhs(m) > 1e-7 * (1.0 + rhsSize)) {
            result.status = LpStatus::Infeasible;
            return result;
        }
        for (int r = 0; r < m; ++r) {
            if (!artificial.at(t.basis(r))) {
                continue;
            }
            int best = -1;
            for (int c = 0; c < cols; ++c) {
                if (!artificial.at(c) && std::abs(t.at(r, c)) > kPivotTolerance
                    && (best < 0 || std::abs(t.at(r, c)) > std::abs(t.at(r, best)))) {
                    best = c;
                }
            }
            if (best >= 0) {
                t.pivot(r, best);
            }
        }
    }
    // Phase 2: the objective over the columns that are not artificial.
    for (int c = 0; c <= cols; ++c) {
        t.at(m, c) = c < ycols ? cy.at(c) : 0.0;
    }
    for (int r = 0; r < m; ++r) {
        const double cb = t.basis(r) < ycols ? cy.at(t.basis(r)) : 0.0;
        if (cb != 0.0) {
            for (int c = 0; c <= cols; ++c) {
                t.at(m, c) -= cb * t.at(r, c);
            }
        }
    }
    QVector<bool> allowed(cols, true);
    for (int c = 0; c < cols; ++c) {
        allowed[c] = !artificial.at(c);
    }
    const LpStatus status = runSimplex(t, allowed, &result.pivots, keepGoing);
    if (status != LpStatus::Optimal) {
        result.status = status;
        return result;
    }
    QVector<double> y(ycols, 0.0);
    for (int r = 0; r < m; ++r) {
        if (t.basis(r) < ycols) {
            y[t.basis(r)] = qMax(0.0, t.rhs(r));
        }
    }
    result.x = QVector<double>(n, 0.0);
    result.objective = 0;
    for (int j = 0; j < n; ++j) {
        const Substitution &s = subs.at(j);
        double x = s.offset + s.sign * y.at(s.col);
        if (s.col2 >= 0) {
            x -= y.at(s.col2);
        }
        result.x[j] = x;
        result.objective += lp.c.at(j) * x;
    }
    result.status = LpStatus::Optimal;
    return result;
}

} // namespace QindaOffice::Calc::SolverMath
