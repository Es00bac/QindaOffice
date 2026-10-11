// SPDX-License-Identifier: GPL-3.0-or-later
// The Solver model read against the workbook, and its trials (solver_internal.h,
// M90, D-098).
//
// AGENT-NOTE: Excel's rules, as its Solver dialog enforces them: the
// objective is one cell on the model's sheet (or nothing), the variable
// cells and every constraint's cells are on that sheet, variable cells hold
// values (no formulas), an int / bin / dif constraint names variable cells
// only, and a right side is a number, a cell, a range the size of the left
// side, or a formula. A <=, = or >= constraint on a variable cell whose right
// side cannot change while Solver runs (a number, a cell holding a number)
// is also a bound; Make Unconstrained Variables Non-Negative gives every
// variable left without a lower bound the bound 0.

#include "solver_internal.h"

#include "eval.h"
#include "sheet.h"
#include "solver_math.h"
#include "what_if_internal.h"
#include "workbook.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <cmath>

namespace QindaOffice::Calc::SolverDetail {

namespace {

constexpr qint64 kMaxVariables = 1000;
constexpr qint64 kMaxConstraintCells = 5000;

QString tr(const char *text)
{
    return QCoreApplication::translate("Solver", text);
}

QString stripped(const QString &text)
{
    QString out = text.trimmed();
    if (out.startsWith(QLatin1Char('='))) {
        out = out.mid(1).trimmed();
    }
    return out;
}

// A range typed for Solver on the model's sheet; nullopt when it is not one
// (`elsewhere`: it is on another sheet).
std::optional<CellRange> rangeOnSheet(const Workbook &book, qint32 sheet, const QString &text, bool *elsewhere)
{
    const ParseResult parsed = parseFormula(stripped(text));
    if (!parsed.error.isEmpty() || !parsed.root || parsed.root->kind != Node::Kind::Ref) {
        return std::nullopt;
    }
    const RangeRef &ref = parsed.root->ref;
    if (ref.wholeRows || ref.wholeCols || !ref.range.isValid()) {
        return std::nullopt;
    }
    if (!ref.sheet.isEmpty() && book.sheetIndex(unquotedSheetName(ref.sheet)) != sheet) {
        *elsewhere = true;
        return std::nullopt;
    }
    return ref.range;
}

QList<CellAddr> cellsOf(const CellRange &range)
{
    QList<CellAddr> cells;
    for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            cells.append({r, c});
        }
    }
    return cells;
}

// True when the formula reads no cell, name or function: a constant ("-5", "2*3").
bool constantFormula(const NodePtr &node)
{
    if (!node) {
        return true;
    }
    switch (node->kind) {
    case Node::Kind::Ref:
    case Node::Kind::Name:
    case Node::Kind::Call:
    case Node::Kind::StructRef:
        return false;
    case Node::Kind::Unary:
        return constantFormula(node->left);
    case Node::Kind::Binary:
        return constantFormula(node->left) && constantFormula(node->right);
    default:
        return true;
    }
}

QString rhsCellText(const Workbook &book, qint32 modelSheet, qint32 rhsSheet, const RangeRef &ref, const CellAddr &cell)
{
    QString text = addrText(cell, ref.absRow0, ref.absCol0);
    if (rhsSheet != modelSheet) {
        text = quotedSheetName(book.sheetName(rhsSheet)) + QLatin1Char('!') + text;
    }
    return text;
}

} // namespace

std::optional<double> numberIn(const Value &value)
{
    if (const auto *number = std::get_if<double>(&value)) {
        return std::isfinite(*number) ? std::optional<double>(*number) : std::nullopt;
    }
    if (isBlank(value)) {
        return 0.0;
    }
    return std::nullopt;
}

} // namespace QindaOffice::Calc::SolverDetail

namespace QindaOffice::Calc {

QString solverConstraintProblem(const Workbook &book, qint32 sheet, const SolverModel &model,
                                const SolverConstraint &constraint)
{
    using namespace SolverDetail;
    bool elsewhere = false;
    const std::optional<CellRange> lhs = rangeOnSheet(book, sheet, constraint.left, &elsewhere);
    if (!lhs.has_value()) {
        return elsewhere ? tr("Cell Reference must be on the active sheet.")
                         : tr("Cell Reference “%1” is not a valid reference.").arg(constraint.left.trimmed());
    }
    if (constraint.relation >= SolverRelation::Integer) {
        QSet<quint64> variables;
        for (const QString &part : solverRefParts(model.variables)) {
            bool other = false;
            if (const std::optional<CellRange> range = rangeOnSheet(book, sheet, part, &other)) {
                if (range->cellCount() > kMaxVariables) {
                    continue;
                }
                for (const CellAddr &cell : cellsOf(*range)) {
                    variables.insert(cell.key());
                }
            }
        }
        for (const CellAddr &cell : cellsOf(*lhs)) {
            if (!variables.contains(cell.key())) {
                return tr("Integer, binary and alldifferent constraints can only be applied to variable cells.");
            }
        }
        return {};
    }
    const QString message = tr("Constraint must be a number, a simple reference, or a formula with a numeric value.");
    const QString right = stripped(constraint.right);
    if (right.isEmpty()) {
        return message;
    }
    const ParseResult parsed = parseFormula(right);
    if (!parsed.error.isEmpty() || !parsed.root || parsed.root->kind == Node::Kind::Text
        || parsed.root->kind == Node::Kind::Bool || parsed.root->kind == Node::Kind::Error) {
        return message;
    }
    if (parsed.root->kind == Node::Kind::Ref) {
        const RangeRef &ref = parsed.root->ref;
        if (ref.wholeRows || ref.wholeCols || !ref.range.isValid()
            || (!ref.sheet.isEmpty() && book.sheetIndex(unquotedSheetName(ref.sheet)) < 0)) {
            return message;
        }
        const qint32 rows = ref.range.bottomRight.row - ref.range.topLeft.row;
        const qint32 cols = ref.range.bottomRight.col - ref.range.topLeft.col;
        if (ref.range.cellCount() != 1
            && (rows != lhs->bottomRight.row - lhs->topLeft.row || cols != lhs->bottomRight.col - lhs->topLeft.col)) {
            return tr("The Constraint must be one cell or a range the size of the Cell Reference.");
        }
    }
    return {};
}

} // namespace QindaOffice::Calc

namespace QindaOffice::Calc::SolverDetail {

QString resolve(const Workbook &book, qint32 sheet, const SolverModel &model, Resolved *out)
{
    *out = Resolved{};
    if (sheet < 0 || sheet >= book.sheetCount() || book.sheetAt(sheet).isChartSheet()) {
        return tr("Solver works on a worksheet.");
    }
    const Sheet &where = book.sheetAt(sheet);
    bool elsewhere = false;
    if (!model.objective.trimmed().isEmpty()) {
        const std::optional<CellRange> objective = rangeOnSheet(book, sheet, model.objective, &elsewhere);
        if (!objective.has_value() || objective->cellCount() != 1) {
            return tr("Objective Cell must be a single cell on the active sheet.");
        }
        out->objective = objective->topLeft;
    }
    QHash<quint64, int> index;
    for (const QString &part : solverRefParts(model.variables)) {
        const std::optional<CellRange> range = rangeOnSheet(book, sheet, part, &elsewhere);
        if (!range.has_value()) {
            return elsewhere ? tr("Variable Cells must be on the active sheet.")
                             : tr("By Changing Variable Cells: “%1” is not a valid reference.").arg(part);
        }
        if (range->cellCount() > kMaxVariables) {
            return tr("This problem is too large for Solver to handle.");
        }
        for (const CellAddr &cell : cellsOf(*range)) {
            if (!index.contains(cell.key())) {
                index.insert(cell.key(), int(out->variables.size()));
                out->variables.append(cell);
            }
        }
    }
    if (out->variables.isEmpty()) {
        return tr("Enter the cells Solver may change in By Changing Variable Cells.");
    }
    if (out->variables.size() > kMaxVariables) {
        return tr("This problem is too large for Solver to handle.");
    }
    for (const CellAddr &cell : std::as_const(out->variables)) {
        const Cell *held = where.cellAt(cell);
        if ((held != nullptr && (held->hasFormula() || held->isSpilled())) || book.arrayBlockAt(sheet, cell).has_value()) {
            return tr("Variable cells cannot contain formulas (%1).").arg(addrText(cell, true, true));
        }
    }
    const int n = int(out->variables.size());
    out->lower = QVector<double>(n, -SolverMath::kInfinity);
    out->upper = QVector<double>(n, SolverMath::kInfinity);
    out->integer = QVector<bool>(n, false);
    out->binary = QVector<bool>(n, false);
    qint64 constraintCells = 0;
    for (qsizetype i = 0; i < model.constraints.size(); ++i) {
        const SolverConstraint &c = model.constraints.at(i);
        const QString problem = solverConstraintProblem(book, sheet, model, c);
        if (!problem.isEmpty()) {
            return problem;
        }
        elsewhere = false;
        const CellRange lhs = *rangeOnSheet(book, sheet, c.left, &elsewhere);
        const QList<CellAddr> cells = cellsOf(lhs);
        constraintCells += cells.size();
        if (constraintCells > kMaxConstraintCells) {
            return tr("This problem is too large for Solver to handle.");
        }
        if (c.relation >= SolverRelation::Integer) {
            QVector<int> group;
            for (const CellAddr &cell : cells) {
                const int k = index.value(cell.key());
                out->integer[k] = true;
                out->binary[k] = out->binary.at(k) || c.relation == SolverRelation::Binary;
                group.append(k);
                out->integerCells.append({c.relation, cell});
            }
            if (c.relation == SolverRelation::AllDifferent) {
                out->allDifferent.append(group);
            }
            continue;
        }
        const ParseResult parsed = parseFormula(stripped(c.right));
        const NodePtr root = parsed.root;
        Row base;
        base.relation = c.relation;
        base.constraint = int(i);
        std::optional<RangeRef> rhsRef;
        if (constantFormula(root)) {
            EvalContext ctx;
            ctx.host = &book;
            ctx.sheet = sheet;
            ctx.here = lhs.topLeft;
            base.number = numberIn(evalNode(root, ctx));
            base.rhsText = stripped(c.right);
        } else if (root->kind == Node::Kind::Ref) {
            rhsRef = root->ref;
            base.rhsSheet = rhsRef->sheet.isEmpty() ? sheet : book.sheetIndex(unquotedSheetName(rhsRef->sheet));
        } else {
            base.formula = root;
            base.rhsText = stripped(c.right);
        }
        for (qsizetype at = 0; at < cells.size(); ++at) {
            Row row = base;
            row.cell = cells.at(at);
            if (rhsRef.has_value()) {
                const CellRange &range = rhsRef->range;
                row.rhsCell = range.cellCount() == 1
                    ? range.topLeft
                    : CellAddr{range.topLeft.row + (row.cell.row - lhs.topLeft.row), range.topLeft.col + (row.cell.col - lhs.topLeft.col)};
                row.rhsText = rhsCellText(book, sheet, row.rhsSheet, *rhsRef, row.rhsCell);
            }
            const int k = index.value(row.cell.key(), -1);
            std::optional<double> constant = row.number;
            if (!constant.has_value() && row.rhsCell.isValid() && row.rhsSheet >= 0
                && !(row.rhsSheet == sheet && index.contains(row.rhsCell.key()))) {
                const Cell *held = book.sheetAt(row.rhsSheet).cellAt(row.rhsCell);
                if (held == nullptr || !held->hasFormula()) {
                    constant = numberIn(book.cellValue(row.rhsSheet, row.rhsCell.row, row.rhsCell.col));
                }
            }
            if (k >= 0 && constant.has_value()) {
                row.bound = true;
                if (c.relation != SolverRelation::GreaterEqual) {
                    out->upper[k] = qMin(out->upper.at(k), *constant);
                }
                if (c.relation != SolverRelation::LessEqual) {
                    out->lower[k] = qMax(out->lower.at(k), *constant);
                }
            }
            out->rows.append(row);
        }
    }
    for (int k = 0; k < n; ++k) {
        if (out->binary.at(k)) {
            out->lower[k] = qMax(out->lower.at(k), 0.0);
            out->upper[k] = qMin(out->upper.at(k), 1.0);
        }
        if (model.nonNegative && !std::isfinite(out->lower.at(k))) {
            out->lower[k] = 0.0;
        }
    }
    return {};
}

Evaluator::Evaluator(Workbook &book, qint32 sheet, const Resolved &model)
    : m_book(book)
    , m_sheet(sheet)
    , m_model(model)
{
}

void Evaluator::set(const QVector<double> &x)
{
    QSet<CellKey> keys;
    for (int k = 0; k < m_model.variables.size() && k < x.size(); ++k) {
        keys.insert(WhatIf::putTrialValue(m_book, m_sheet, m_model.variables.at(k), Value(x.at(k))));
    }
    m_book.recalc(keys);
}

bool Evaluator::objective(double *value) const
{
    if (!m_model.objective.has_value()) {
        *value = 0;
        return true;
    }
    const std::optional<double> number =
        numberIn(m_book.cellValue(m_sheet, m_model.objective->row, m_model.objective->col));
    *value = number.value_or(0.0);
    return number.has_value();
}

bool Evaluator::row(int i, double *lhs, double *rhs) const
{
    const Row &row = m_model.rows.at(i);
    const std::optional<double> left = numberIn(m_book.cellValue(m_sheet, row.cell.row, row.cell.col));
    std::optional<double> right = row.number;
    if (row.rhsCell.isValid()) {
        right = row.rhsSheet >= 0 ? numberIn(m_book.cellValue(row.rhsSheet, row.rhsCell.row, row.rhsCell.col))
                                  : std::nullopt;
    } else if (row.formula) {
        EvalContext ctx;
        ctx.host = &m_book;
        ctx.sheet = m_sheet;
        ctx.here = row.cell;
        right = numberIn(evalNode(row.formula, ctx));
    }
    *lhs = left.value_or(0.0);
    *rhs = right.value_or(0.0);
    return left.has_value() && right.has_value();
}

QList<SolverReportConstraint> reportRows(const Workbook &book, qint32 sheet, const Resolved &model, double precision)
{
    QList<SolverReportConstraint> out;
    Evaluator evaluator(const_cast<Workbook &>(book), sheet, model);
    for (int i = 0; i < model.rows.size(); ++i) {
        const Row &row = model.rows.at(i);
        double lhs = 0;
        double rhs = 0;
        static_cast<void>(evaluator.row(i, &lhs, &rhs));
        SolverReportConstraint line;
        line.cell = row.cell;
        line.formula = addrText(row.cell, true, true) + solverRelationText(row.relation) + row.rhsText;
        line.value = book.cellValue(sheet, row.cell.row, row.cell.col);
        line.slack = row.relation == SolverRelation::LessEqual ? rhs - lhs
                   : row.relation == SolverRelation::GreaterEqual ? lhs - rhs
                                                                   : 0.0;
        line.binding = std::abs(line.slack) <= qMax(precision, 1e-9) * qMax(1.0, std::abs(rhs));
        if (line.binding) {
            line.slack = 0;
        }
        out.append(line);
    }
    for (const auto &[relation, cell] : model.integerCells) {
        SolverReportConstraint line;
        line.cell = cell;
        line.formula = addrText(cell, true, true) + QLatin1Char('=')
            + (relation == SolverRelation::Binary         ? QStringLiteral("binary")
               : relation == SolverRelation::AllDifferent ? QStringLiteral("AllDifferent")
                                                          : QStringLiteral("integer"));
        line.value = book.cellValue(sheet, cell.row, cell.col);
        out.append(line);
    }
    return out;
}

} // namespace QindaOffice::Calc::SolverDetail
