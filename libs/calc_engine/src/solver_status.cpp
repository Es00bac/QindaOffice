// SPDX-License-Identifier: GPL-3.0-or-later
// User-facing Solver results shared by the engine, report and dialog (D-098).
#include "solver.h"
#include <QCoreApplication>

namespace QindaOffice::Calc {
namespace {
QString tr(const char *text) { return QCoreApplication::translate("Solver", text); }
}

QString solverStatusMessage(SolverStatus status)
{
    switch (status) {
    case SolverStatus::Optimal: return tr("Solver found a solution.  All Constraints and optimality conditions are satisfied.");
    case SolverStatus::Converged: return tr("Solver has converged to the current solution.  All Constraints are satisfied.");
    case SolverStatus::CannotImprove: return tr("Solver cannot improve the current solution.  All Constraints are satisfied.");
    case SolverStatus::IterationLimit: return tr("Stop chosen when the maximum iteration limit was reached.");
    case SolverStatus::NotConverging: return tr("The Objective Cell values do not converge.");
    case SolverStatus::Infeasible: return tr("Solver could not find a feasible solution.");
    case SolverStatus::Stopped: return tr("Solver stopped at user's request.");
    case SolverStatus::NotLinear: return tr("The linearity conditions required by this LP Solver are not satisfied.");
    case SolverStatus::TooLarge: return tr("The problem is too large for Solver to handle.");
    case SolverStatus::ErrorValue: return tr("Solver encountered an error value in the Objective Cell or a Constraint cell.");
    case SolverStatus::TimeLimit: return tr("Stop chosen when the maximum time limit was reached.");
    case SolverStatus::ModelError: return tr("Error in model. Please verify that all cells and Constraints are valid.");
    case SolverStatus::IntegerOptimal: return tr("Solver found an integer solution within tolerance.  All Constraints are satisfied.");
    case SolverStatus::FeasibleLimit: return tr("Stop chosen when the maximum number of feasible [integer] solutions was reached.");
    case SolverStatus::SubproblemLimit: return tr("Stop chosen when the maximum number of feasible [integer] subproblems was reached.");
    case SolverStatus::GlobalProbable: return tr("Solver converged in probability to a global solution.  All Constraints are satisfied.");
    case SolverStatus::NeedsBounds: return tr("All variables must have both upper and lower bounds.");
    case SolverStatus::BoundsConflict: return tr("Variable bounds conflict in binary or alldifferent constraint.");
    }
    return {};
}

bool solverStatusFeasible(SolverStatus status)
{
    switch (status) {
    case SolverStatus::Optimal:
    case SolverStatus::Converged:
    case SolverStatus::CannotImprove:
    case SolverStatus::IntegerOptimal:
    case SolverStatus::GlobalProbable:
        return true;
    default:
        return false;
    }
}

} // namespace QindaOffice::Calc
