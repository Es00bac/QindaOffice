// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Scaled augmented-Lagrangian values for the bounded nonlinear search.
#include "solver_math.h"
#include <cmath>

namespace QindaOffice::Calc::SolverMath::NlpDetail {
struct Point {
    QVector<double> x;
    double f = 0;
    QVector<double> g; // inequalities, scaled (problem's, then the linear rows)
    QVector<double> h; // equalities, scaled
};

struct Jacobian {
    QVector<double> f;
    QList<QVector<double>> g;
    QList<QVector<double>> h;
};

struct Multipliers {
    QVector<double> mu;     // inequalities
    QVector<double> lambda; // equalities
    double rho = 10;
};

inline double lagrangian(const Point &p, const Multipliers &m)
{
    double value = p.f;
    for (int i = 0; i < p.g.size(); ++i) {
        const double t = qMax(0.0, m.mu.at(i) + m.rho * p.g.at(i));
        value += (t * t - m.mu.at(i) * m.mu.at(i)) / (2.0 * m.rho);
    }
    for (int j = 0; j < p.h.size(); ++j) {
        value += m.lambda.at(j) * p.h.at(j) + 0.5 * m.rho * p.h.at(j) * p.h.at(j);
    }
    return value;
}

inline QVector<double> gradient(const Point &p, const Jacobian &jac, const Multipliers &m)
{
    QVector<double> grad = jac.f;
    for (int i = 0; i < p.g.size(); ++i) {
        const double weight = qMax(0.0, m.mu.at(i) + m.rho * p.g.at(i));
        if (weight != 0.0) {
            for (int k = 0; k < grad.size(); ++k) {
                grad[k] += weight * jac.g.at(i).at(k);
            }
        }
    }
    for (int j = 0; j < p.h.size(); ++j) {
        const double weight = m.lambda.at(j) + m.rho * p.h.at(j);
        for (int k = 0; k < grad.size(); ++k) {
            grad[k] += weight * jac.h.at(j).at(k);
        }
    }
    return grad;
}

inline double scaledViolation(const Point &p)
{
    double worst = 0;
    for (const double g : p.g) {
        worst = qMax(worst, g);
    }
    for (const double h : p.h) {
        worst = qMax(worst, std::abs(h));
    }
    return worst;
}

} // namespace QindaOffice::Calc::SolverMath::NlpDetail
