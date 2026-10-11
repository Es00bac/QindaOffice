// SPDX-License-Identifier: GPL-3.0-or-later
// Evolutionary (solver_math.h, M90, D-098): differential evolution
// (DE/rand/1/bin) inside the variables' bounds, as Excel's Evolutionary
// engine searches — a population, not a gradient, so it copes with IF,
// lookups and other non-smooth models, and it is slow and only "in
// probability" global.
//
// The first member is the starting point, the rest are drawn uniformly in
// the bounds. A trial takes, per variable, a + F·(b − c) of three other
// members with probability CR, else the member's own value; Excel's
// Mutation Rate is the chance a variable is instead drawn afresh. Integer
// variables are rounded. Selection is feasibility first: a feasible member
// beats an infeasible one, two feasible ones compare objectives, two
// infeasible ones their total violation. The search ends when every member
// is feasible and their objectives lie within Convergence of each other
// (Excel's "converged in probability"), after Maximum Time without
// improvement, at the generation cap, or when stopped.

#include "solver_math.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QtGlobal>

#include <cmath>
#include <random>

namespace QindaOffice::Calc::SolverMath {

namespace {

struct Member {
    QVector<double> x;
    double f = 0;
    double violation = 0;
    bool ok = false; // evaluated without an error value
};

constexpr double kWeight = 0.6;    // F
constexpr double kCrossover = 0.9; // CR

class Population {
public:
    Population(const NonlinearProblem &problem, const EvolveOptions &options)
        : m_problem(problem)
        , m_options(options)
        , m_rng(options.seed != 0 ? quint64(options.seed) : quint64(QDateTime::currentMSecsSinceEpoch()))
    {
    }

    [[nodiscard]] double draw(int k)
    {
        std::uniform_real_distribution<double> uniform(m_problem.lower.at(k), m_problem.upper.at(k));
        return uniform(m_rng);
    }
    [[nodiscard]] double chance()
    {
        std::uniform_real_distribution<double> uniform(0.0, 1.0);
        return uniform(m_rng);
    }
    [[nodiscard]] int pick(int size)
    {
        std::uniform_int_distribution<int> uniform(0, size - 1);
        return uniform(m_rng);
    }

    void tidy(QVector<double> &x) const
    {
        for (int k = 0; k < x.size(); ++k) {
            x[k] = qBound(m_problem.lower.at(k), x.at(k), m_problem.upper.at(k));
            if (k < m_options.integer.size() && m_options.integer.at(k)) {
                x[k] = qBound(std::ceil(m_problem.lower.at(k) - 1e-9), std::round(x.at(k)),
                              std::floor(m_problem.upper.at(k) + 1e-9));
            }
        }
    }

    Member evaluate(QVector<double> x)
    {
        tidy(x);
        Member member;
        member.x = x;
        QVector<double> g;
        QVector<double> h;
        member.ok = m_problem.evaluate(x, &member.f, &g, &h) && std::isfinite(member.f);
        if (!member.ok) {
            member.violation = kInfinity;
            return member;
        }
        for (const double v : std::as_const(g)) {
            member.violation += qMax(0.0, v);
        }
        for (const double v : std::as_const(h)) {
            member.violation += std::abs(v);
        }
        for (const LinearRow &row : m_problem.linear) {
            double value = -row.rhs;
            for (int k = 0; k < x.size() && k < row.a.size(); ++k) {
                value += row.a.at(k) * x.at(k);
            }
            member.violation += qMax(0.0, value);
        }
        return member;
    }

    [[nodiscard]] bool feasible(const Member &m) const { return m.ok && m.violation <= m_options.precision; }
    [[nodiscard]] bool better(const Member &a, const Member &b) const
    {
        if (feasible(a) && feasible(b)) {
            return a.f <= b.f;
        }
        if (feasible(a) != feasible(b)) {
            return feasible(a);
        }
        return a.violation <= b.violation;
    }

private:
    const NonlinearProblem &m_problem;
    const EvolveOptions &m_options;
    std::mt19937_64 m_rng;
};

} // namespace

EvolveResult solveEvolutionary(const NonlinearProblem &problem, const QVector<double> &start, const EvolveOptions &options)
{
    EvolveResult result;
    const int n = int(problem.lower.size());
    Population pop(problem, options);
    const int size = qBound(10, options.population, 400);
    QList<Member> members;
    members.append(pop.evaluate(start));
    if (!members.first().ok) {
        result.status = EvolveStatus::ErrorValue;
        result.x = members.first().x;
        return result;
    }
    while (members.size() < size) {
        QVector<double> x(n, 0.0);
        for (int k = 0; k < n; ++k) {
            x[k] = pop.draw(k);
        }
        members.append(pop.evaluate(x));
    }
    int bestIndex = 0;
    for (int i = 1; i < members.size(); ++i) {
        if (pop.better(members.at(i), members.at(bestIndex))) {
            bestIndex = i;
        }
    }
    QElapsedTimer sinceImprovement;
    sinceImprovement.start();
    const qint64 cap = options.maxGenerations > 0 ? options.maxGenerations : 5000;
    for (;;) {
        if (options.keepGoing && !options.keepGoing()) {
            result.status = EvolveStatus::Stopped;
            break;
        }
        if (result.generations >= cap) {
            result.status = EvolveStatus::IterationLimit;
            break;
        }
        ++result.generations;
        for (int i = 0; i < members.size(); ++i) {
            int a = 0;
            int b = 0;
            int c = 0;
            do { a = pop.pick(size); } while (a == i);
            do { b = pop.pick(size); } while (b == i || b == a);
            do { c = pop.pick(size); } while (c == i || c == a || c == b);
            const int forced = pop.pick(n);
            QVector<double> trial = members.at(i).x;
            for (int k = 0; k < n; ++k) {
                if (pop.chance() < options.mutationRate) {
                    trial[k] = pop.draw(k);
                } else if (k == forced || pop.chance() < kCrossover) {
                    trial[k] = members.at(a).x.at(k) + kWeight * (members.at(b).x.at(k) - members.at(c).x.at(k));
                }
            }
            const Member candidate = pop.evaluate(trial);
            if (!pop.better(candidate, members.at(i))) {
                continue;
            }
            const Member previousBest = members.at(bestIndex);
            members[i] = candidate;
            if (i == bestIndex || pop.better(candidate, previousBest)) {
                const bool improved = pop.feasible(candidate) != pop.feasible(previousBest)
                    || (pop.feasible(candidate)
                            ? previousBest.f - candidate.f > options.convergence * (1.0 + std::abs(candidate.f))
                            : previousBest.violation - candidate.violation > options.precision);
                bestIndex = i;
                if (improved) {
                    sinceImprovement.restart();
                }
            }
        }
        if (options.onIteration) {
            options.onIteration(result.generations, members.at(bestIndex).f);
        }
        bool allFeasible = true;
        double lo = kInfinity;
        double hi = -kInfinity;
        for (const Member &m : std::as_const(members)) {
            allFeasible = allFeasible && pop.feasible(m);
            lo = qMin(lo, m.f);
            hi = qMax(hi, m.f);
        }
        if (allFeasible && hi - lo <= options.convergence * (1.0 + std::abs(lo))) {
            result.status = EvolveStatus::Converged;
            break;
        }
        if (double(sinceImprovement.elapsed()) / 1000.0 >= options.maxSecondsWithoutImprovement) {
            result.status = EvolveStatus::NoImprovement;
            break;
        }
    }
    const Member &best = members.at(bestIndex);
    result.x = best.x;
    result.f = best.f;
    result.violation = best.violation;
    if (!pop.feasible(best) && result.status != EvolveStatus::Stopped) {
        result.status = EvolveStatus::Infeasible;
    }
    return result;
}

} // namespace QindaOffice::Calc::SolverMath
