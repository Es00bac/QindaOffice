// SPDX-License-Identifier: GPL-3.0-or-later
// Branch and bound for Solver's int, bin and dif constraints (solver_math.h,
// M90, D-098), over whichever relaxation the method has (the LP, the
// nonlinear search).
//
// Depth first: a node's relaxation is solved; a fractional integer variable
// splits it in two (x ≤ ⌊v⌋, x ≥ ⌈v⌉), the nearer side explored first; an
// all-different group whose integer solution repeats a value splits on the
// pair (xᵢ − xⱼ ≤ −1 or xⱼ − xᵢ ≤ −1), with Σx = n(n+1)/2 added at the root.
// A node whose relaxation cannot beat the best integer solution by more
// than Integer Optimality (Excel's "integer tolerance", a fraction of the
// best objective) is dropped: with a tolerance above zero the answer is
// "an integer solution within tolerance", as Excel says it.

#include "solver_math.h"

#include <QtGlobal>

#include <cmath>
#include <optional>
#include <vector>

namespace QindaOffice::Calc::SolverMath {

namespace {

struct Node {
    QVector<double> lower;
    QVector<double> upper;
    QList<LinearRow> cuts;
    double bound = -kInfinity; // the parent's relaxed objective
};

bool integral(double v)
{
    return std::abs(v - std::round(v)) <= 1e-6 * qMax(1.0, std::abs(v));
}

LinearRow pairCut(int n, int first, int second)
{
    LinearRow row;
    row.a = QVector<double>(n, 0.0);
    row.a[first] = 1.0;
    row.a[second] = -1.0;
    row.sense = -1;
    row.rhs = -1.0; // x_first − x_second ≤ −1
    return row;
}

} // namespace

BnbResult branchAndBound(const QVector<double> &lower, const QVector<double> &upper, const IntegerSpec &spec,
                         const RelaxationSolver &relax)
{
    const int n = int(lower.size());
    BnbResult result;
    Node root;
    root.lower = lower;
    root.upper = upper;
    QVector<bool> integer = spec.integer;
    integer.resize(n);
    for (const QVector<int> &group : spec.allDifferent) {
        const double size = double(group.size());
        LinearRow sum;
        sum.a = QVector<double>(n, 0.0);
        for (const int k : group) {
            integer[k] = true;
            root.lower[k] = qMax(root.lower.at(k), 1.0);
            root.upper[k] = qMin(root.upper.at(k), size);
            sum.a[k] = 1.0;
        }
        sum.rhs = size * (size + 1.0) / 2.0;
        root.cuts.append(sum);
        for (double &a : sum.a) {
            a = -a;
        }
        sum.rhs = -sum.rhs;
        root.cuts.append(sum);
    }
    for (int k = 0; k < n; ++k) {
        if (integer.at(k)) {
            if (std::isfinite(root.lower.at(k))) {
                root.lower[k] = std::ceil(root.lower.at(k) - 1e-9);
            }
            if (std::isfinite(root.upper.at(k))) {
                root.upper[k] = std::floor(root.upper.at(k) + 1e-9);
            }
            if (root.lower.at(k) > root.upper.at(k)) {
                result.status = BnbStatus::Infeasible;
                return result;
            }
        }
    }
    std::vector<Node> stack{root};
    std::optional<Relaxation> best;
    qint64 feasible = 0;
    bool pruneByTolerance = false;
    const auto cutoff = [&spec](double incumbent) {
        return incumbent - qMax(spec.tolerance * std::abs(incumbent), 1e-9 * (1.0 + std::abs(incumbent)));
    };
    const auto finish = [&](BnbStatus status) {
        result.status = status;
        if (best.has_value()) {
            result.x = best->x;
            result.objective = best->objective;
        }
        return result;
    };
    while (!stack.empty()) {
        if (spec.keepGoing && !spec.keepGoing()) {
            return finish(BnbStatus::Stopped);
        }
        if (spec.maxSubproblems > 0 && result.subproblems >= spec.maxSubproblems) {
            return finish(best.has_value() ? BnbStatus::SubproblemLimit : BnbStatus::Infeasible);
        }
        Node node = std::move(stack.back());
        stack.pop_back();
        if (best.has_value() && node.bound >= cutoff(best->objective)) {
            pruneByTolerance = pruneByTolerance || node.bound < best->objective - 1e-9 * (1.0 + std::abs(best->objective));
            continue;
        }
        const Relaxation relaxed = relax(node.lower, node.upper, node.cuts);
        ++result.subproblems;
        if (spec.onSubproblem) {
            spec.onSubproblem(result.subproblems, best.has_value() ? best->objective : relaxed.objective);
        }
        switch (relaxed.status) {
        case Relaxation::Status::Infeasible:
        case Relaxation::Status::Failed:
            continue;
        case Relaxation::Status::Unbounded:
            return finish(BnbStatus::Unbounded);
        case Relaxation::Status::Stopped:
            return finish(BnbStatus::Stopped);
        case Relaxation::Status::Solved:
            break;
        }
        if (best.has_value() && relaxed.objective >= cutoff(best->objective)) {
            pruneByTolerance = pruneByTolerance || relaxed.objective < best->objective - 1e-9 * (1.0 + std::abs(best->objective));
            continue;
        }
        // The most fractional integer variable.
        int branch = -1;
        double distance = 0;
        for (int k = 0; k < n; ++k) {
            if (!integer.at(k) || integral(relaxed.x.at(k))) {
                continue;
            }
            const double v = relaxed.x.at(k);
            const double d = std::min(v - std::floor(v), std::ceil(v) - v);
            if (d > distance) {
                distance = d;
                branch = k;
            }
        }
        if (branch >= 0) {
            const double v = relaxed.x.at(branch);
            Node down = node;
            down.upper[branch] = std::floor(v);
            down.bound = relaxed.objective;
            Node up = node;
            up.lower[branch] = std::ceil(v);
            up.bound = relaxed.objective;
            // The nearer side on top of the stack.
            if (v - std::floor(v) < 0.5) {
                stack.push_back(std::move(up));
                stack.push_back(std::move(down));
            } else {
                stack.push_back(std::move(down));
                stack.push_back(std::move(up));
            }
            continue;
        }
        QVector<double> rounded = relaxed.x;
        for (int k = 0; k < n; ++k) {
            if (integer.at(k)) {
                rounded[k] = std::round(rounded.at(k));
            }
        }
        // An all-different group with a value twice splits on that pair.
        std::optional<std::pair<int, int>> repeat;
        for (const QVector<int> &group : spec.allDifferent) {
            for (int a = 0; a < group.size() && !repeat; ++a) {
                for (int b = a + 1; b < group.size() && !repeat; ++b) {
                    if (rounded.at(group.at(a)) == rounded.at(group.at(b))) {
                        repeat = std::make_pair(group.at(a), group.at(b));
                    }
                }
            }
        }
        if (repeat.has_value()) {
            Node left = node;
            left.cuts.append(pairCut(n, repeat->first, repeat->second));
            left.bound = relaxed.objective;
            Node right = node;
            right.cuts.append(pairCut(n, repeat->second, repeat->first));
            right.bound = relaxed.objective;
            stack.push_back(std::move(right));
            stack.push_back(std::move(left));
            continue;
        }
        Relaxation incumbent = relaxed;
        incumbent.x = rounded;
        best = incumbent;
        ++feasible;
        if (spec.maxFeasible > 0 && feasible >= spec.maxFeasible) {
            return finish(BnbStatus::FeasibleLimit);
        }
    }
    if (!best.has_value()) {
        return finish(BnbStatus::Infeasible);
    }
    return finish(pruneByTolerance ? BnbStatus::WithinTolerance : BnbStatus::Optimal);
}

} // namespace QindaOffice::Calc::SolverMath
