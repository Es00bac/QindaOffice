// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to Data ▸ Solver (M90, D-098): the model read against the
// workbook (solver_cells.cpp) — the variable cells, the objective cell, one
// row per constraint cell, the bounds the constraints and Make
// Unconstrained Variables Non-Negative put on the variables, the int / bin
// / dif sets — and the evaluator that puts a trial solution into the cells
// and reads the objective and constraints back. solver.cpp drives it.

#include "parser.h"
#include "solver.h"

#include <QVector>

namespace QindaOffice::Calc::SolverDetail {

// One constraint cell (a <=, = or >= constraint over a range is a row per
// cell). The right side is a number, a cell (its element of a range of the
// same size), or a formula evaluated at the left cell.
struct Row {
    CellAddr cell;
    SolverRelation relation = SolverRelation::LessEqual;
    std::optional<double> number;
    qint32 rhsSheet = -1;
    CellAddr rhsCell;    // valid: the right side is this cell
    NodePtr formula;     // set: the right side is this formula
    QString rhsText;     // as the report writes it ("$F$5", "10")
    int constraint = 0;  // index into SolverModel::constraints
    bool bound = false;  // a bound on a variable (it also sets lower / upper)
};

struct Resolved {
    QList<CellAddr> variables;
    std::optional<CellAddr> objective;
    QList<Row> rows;              // every <=, = and >= cell, bounds included
    QVector<double> lower;        // per variable
    QVector<double> upper;
    QVector<bool> integer;        // int, bin and dif variables
    QVector<bool> binary;
    QList<QVector<int>> allDifferent;
    // The report's lines for int / bin / dif constraints, in model order.
    QList<std::pair<SolverRelation, CellAddr>> integerCells;
};

// Reads `model` against the workbook: "" or Excel's message (and `out`
// then holds nothing useful).
[[nodiscard]] QString resolve(const Workbook &book, qint32 sheet, const SolverModel &model, Resolved *out);

// The value a cell holds as a number for Solver (a blank is 0); nullopt for
// text, booleans and errors.
[[nodiscard]] std::optional<double> numberIn(const Value &value);

class Evaluator {
public:
    Evaluator(Workbook &book, qint32 sheet, const Resolved &model);
    // Puts x into the variable cells and recalculates.
    void set(const QVector<double> &x);
    // The objective's value now (0 without an objective); false on an
    // error value or anything but a number.
    [[nodiscard]] bool objective(double *value) const;
    // Row i's left and right sides now; false when either is not a number.
    [[nodiscard]] bool row(int i, double *lhs, double *rhs) const;

private:
    Workbook &m_book;
    qint32 m_sheet;
    const Resolved &m_model;
};

// The Answer Report's Constraints lines at the workbook's current values.
[[nodiscard]] QList<SolverReportConstraint> reportRows(const Workbook &book, qint32 sheet, const Resolved &model,
                                                      double precision);

} // namespace QindaOffice::Calc::SolverDetail
