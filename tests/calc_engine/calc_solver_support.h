// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// M90 (D-098) Data ▸ Solver in the engine: textbook linear programs with
// known optima (product mix, transportation with range constraints, diet),
// an integer (binary) knapsack and an all-different assignment through
// branch and bound, GRG Nonlinear on Rosenbrock's function with bounds (an
// interior and a boundary optimum) and on constrained quadratics, the
// Evolutionary method, Excel's refusals and statuses — an infeasible
// model, an unbounded one, a nonlinear model sent to Simplex LP, a stop —
// the model kept as Excel's solver_* names (written, read back, moved by
// structural edits) and the Answer Report sheet.

#include "sheet.h"
#include "solver.h"
#include "structure.h"
#include "workbook.h"

#include <QtTest>

#include <cmath>

using namespace QindaOffice::Calc;

namespace {

inline CellAddr at(const char *a1)
{
    const QString text = QString::fromLatin1(a1);
    return {text.mid(1).toInt() - 1, text.at(0).unicode() - 'A'};
}

inline double number(const Workbook &wb, const char *a1, qint32 sheet = 0)
{
    const CellAddr cell = at(a1);
    return toNumber(wb.cellValue(sheet, cell.row, cell.col)).value_or(std::nan(""));
}

inline void put(Workbook &wb, const char *a1, const QString &input, qint32 sheet = 0)
{
    QVERIFY2(wb.setCellInput(sheet, at(a1), input), a1);
}

inline QString text(const Workbook &wb, qint32 sheet, qint32 row, qint32 col)
{
    return toText(wb.cellValue(sheet, row, col));
}

inline SolverConstraint rule(const char *left, SolverRelation relation, const char *right = "")
{
    return {QString::fromLatin1(left), relation, QString::fromLatin1(right)};
}

// Maximize 3x + 5y subject to x <= 4, 2y <= 12, 3x + 2y <= 18 (x = 2, y = 6, 36).
inline SolverModel productMix(Workbook &wb)
{
    wb.addSheet(QStringLiteral("Mix"));
    put(wb, "A2", QStringLiteral("Units"));
    put(wb, "B1", QStringLiteral("Doors"));
    put(wb, "C1", QStringLiteral("Windows"));
    put(wb, "A4", QStringLiteral("Plant 1"));
    put(wb, "A5", QStringLiteral("Plant 2"));
    put(wb, "A6", QStringLiteral("Plant 3"));
    put(wb, "D3", QStringLiteral("Hours"));
    put(wb, "D4", QStringLiteral("=B2"));
    put(wb, "D5", QStringLiteral("=2*C2"));
    put(wb, "D6", QStringLiteral("=3*B2+2*C2"));
    put(wb, "E4", QStringLiteral("4"));
    put(wb, "E5", QStringLiteral("12"));
    put(wb, "E6", QStringLiteral("18"));
    put(wb, "E1", QStringLiteral("Profit"));
    put(wb, "E2", QStringLiteral("=3*B2+5*C2"));
    SolverModel model;
    model.objective = QStringLiteral("Mix!$E$2");
    model.goal = SolverGoal::Max;
    model.variables = QStringLiteral("Mix!$B$2:$C$2");
    model.constraints = {rule("Mix!$D$4:$D$6", SolverRelation::LessEqual, "Mix!$E$4:$E$6")};
    model.method = SolverMethod::SimplexLp;
    return model;
}

} // namespace
