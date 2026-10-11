// SPDX-License-Identifier: GPL-3.0-or-later
// Turning a formula tree back into text (nodeText) and moving a tree's
// relative references (shiftNode). Split out of parser.cpp in M61 (D-069),
// when the printer learned to put parentheses back.
//
// AGENT-NOTE: the parser folds grouping parentheses into the tree shape —
// "(A1+B1)*2" parses to Mul(Add(A1,B1),2) with no trace of the brackets. Every
// caller that stores printed text (shared-formula dependents on xlsx load,
// structural edits, sheet renames, fill) used to save "A1+B1*2": a different
// formula, written to the user's file. The printer now emits exactly the
// parentheses the parser's precedence needs, so print → parse is the
// identity on trees (tst_calc_parser printingKeepsGrouping).

#include "parser.h"

#include <algorithm>

namespace QindaOffice::Calc {

namespace {

// Binding strength, tightest first, mirroring parser.cpp: atoms, postfix %,
// prefix -/+, the ':' range operator (its operands are parsed as unary
// expressions), then the binary ladder ^, * /, + -, &, comparisons, and ','
// union (only ever inside non-call parentheses).
constexpr int kAtom = 100;
constexpr int kPercent = 90;
constexpr int kPrefix = 80;
constexpr int kRange = 70;

int binaryLevel(Op op)
{
    switch (op) {
    case Op::Range: return kRange;
    case Op::Pow: return 35;
    case Op::Mul:
    case Op::Div: return 30;
    case Op::Add:
    case Op::Sub: return 25;
    case Op::Concat: return 20;
    case Op::Eq:
    case Op::Ne:
    case Op::Lt:
    case Op::Le:
    case Op::Gt:
    case Op::Ge: return 10;
    case Op::Union: return 5;
    case Op::Neg:
    case Op::Pos:
    case Op::Percent: break;
    }
    return kAtom;
}

int levelOf(const NodePtr &node)
{
    if (!node) {
        return kAtom;
    }
    if (node->kind == Node::Kind::Binary) {
        return binaryLevel(node->op);
    }
    if (node->kind == Node::Kind::Unary) {
        return node->op == Op::Percent ? kPercent : kPrefix;
    }
    return kAtom;
}

QString binaryOpText(Op op)
{
    switch (op) {
    case Op::Range: return QStringLiteral(":");
    case Op::Union: return QStringLiteral(",");
    case Op::Add: return QStringLiteral("+");
    case Op::Sub: return QStringLiteral("-");
    case Op::Mul: return QStringLiteral("*");
    case Op::Div: return QStringLiteral("/");
    case Op::Pow: return QStringLiteral("^");
    case Op::Concat: return QStringLiteral("&");
    case Op::Eq: return QStringLiteral("=");
    case Op::Ne: return QStringLiteral("<>");
    case Op::Lt: return QStringLiteral("<");
    case Op::Le: return QStringLiteral("<=");
    case Op::Gt: return QStringLiteral(">");
    case Op::Ge: return QStringLiteral(">=");
    case Op::Neg:
    case Op::Pos:
    case Op::Percent: break;
    }
    return QStringLiteral("+");
}

QString wrapped(const NodePtr &node, bool parenthesize)
{
    const QString text = nodeText(node);
    return parenthesize ? QStringLiteral("(") + text + QStringLiteral(")") : text;
}

// A coordinate moved by `delta` around a sheet edge of `size` (Excel wraps
// a defined name's relative references, M69).
qint32 wrappedIndex(qint32 value, qint32 delta, qint32 size)
{
    return qint32(((qint64(value) + delta) % size + size) % size);
}

RangeRef shiftRef(const RangeRef &ref, qint32 dr, qint32 dc)
{
    RangeRef out = ref;
    // A whole column spans every row and a whole row every column: only the
    // axis the reference actually names can move (M61 — a filled-down
    // SUM(A:A) used to slide its rows past the end of the sheet).
    const qint32 rows = ref.wholeCols ? 0 : dr;
    const qint32 cols = ref.wholeRows ? 0 : dc;
    if (!ref.absRow0) out.range.topLeft.row += rows;
    if (!ref.absCol0) out.range.topLeft.col += cols;
    if (!ref.absRow1) out.range.bottomRight.row += rows;
    if (!ref.absCol1) out.range.bottomRight.col += cols;
    return out;
}

QString literalText(const Value &value)
{
    if (const auto *number = std::get_if<double>(&value)) {
        return QString::number(*number, 'g', 15);
    }
    if (const auto *text = std::get_if<QString>(&value)) {
        return QLatin1Char('"') + QString(*text).replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"');
    }
    if (const auto *flag = std::get_if<bool>(&value)) {
        return *flag ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    }
    if (const auto *err = std::get_if<Err>(&value)) {
        return errText(*err);
    }
    return QStringLiteral("0");
}

// {1,2;3,4}: ',' between columns, ';' between rows (Excel's file and en-US
// spelling; xlsx_formula keeps the ';' inside braces).
QString arrayConstantText(const ArrayConstant &array)
{
    QString out = QStringLiteral("{");
    for (qint32 r = 0; r < array.rows; ++r) {
        for (qint32 c = 0; c < array.cols; ++c) {
            if (c > 0) {
                out += QLatin1Char(',');
            }
            out += literalText(array.cells.value(r * array.cols + c));
        }
        if (r + 1 < array.rows) {
            out += QLatin1Char(';');
        }
    }
    return out + QLatin1Char('}');
}

} // namespace

QString nodeText(const NodePtr &node)
{
    if (!node) {
        return QString();
    }
    switch (node->kind) {
    case Node::Kind::Number:
        return QString::number(node->number, 'g', 15);
    case Node::Kind::Text:
        return QStringLiteral("\"") + QString(node->text).replace(QLatin1Char('"'), QStringLiteral("\"\""))
            + QStringLiteral("\"");
    case Node::Kind::Bool:
        return node->boolean ? QStringLiteral("TRUE") : QStringLiteral("FALSE");
    case Node::Kind::Error:
        return errText(node->error);
    case Node::Kind::Ref:
        return rangeRefText(node->ref);
    case Node::Kind::Unary:
        if (node->op == Op::Percent) {
            // Postfix binds tightest of all operators: anything looser than
            // an atom is bracketed ("(1+2)%", "(-5)%").
            return wrapped(node->left, levelOf(node->left) < kPercent) + QStringLiteral("%");
        }
        return (node->op == Op::Neg ? QStringLiteral("-") : QStringLiteral("+"))
            + wrapped(node->left, levelOf(node->left) < kPrefix);
    case Node::Kind::Binary: {
        if (node->op == Op::Union) {
            // ',' is an argument separator everywhere except inside a bare
            // pair of brackets, so a union always prints inside its own.
            return QStringLiteral("(") + nodeText(node->left) + QStringLiteral(",")
                + nodeText(node->right) + QStringLiteral(")");
        }
        const int level = binaryLevel(node->op);
        // Every binary operator is left-associative in the parser (Excel's
        // 2^3^2 is 64): an equally binding right operand needs brackets, an
        // equally binding left one does not. ':' takes unary operands only.
        const bool leftParens = node->op == Op::Range
            ? levelOf(node->left) < kRange
            : levelOf(node->left) < level;
        const bool rightParens = node->op == Op::Range
            ? levelOf(node->right) <= kRange
            : levelOf(node->right) <= level;
        return wrapped(node->left, leftParens) + binaryOpText(node->op)
            + wrapped(node->right, rightParens);
    }
    case Node::Kind::Call: {
        // ANCHORARRAY(A1) is the A1# spill reference (M66, D-075).
        if (node->text.compare(QLatin1String("ANCHORARRAY"), Qt::CaseInsensitive) == 0 && node->args.size() == 1
            && node->args.first() && node->args.first()->kind == Node::Kind::Ref) {
            return nodeText(node->args.first()) + QLatin1Char('#');
        }
        // SINGLE(x) is the '@' operator (M69, D-078); it takes the range
        // operand after it, anything looser goes in brackets.
        if (node->text.compare(QLatin1String("SINGLE"), Qt::CaseInsensitive) == 0 && node->args.size() == 1) {
            return QLatin1Char('@') + wrapped(node->args.first(), levelOf(node->args.first()) < kRange);
        }
        QString out = node->text + QStringLiteral("(");
        for (int i = 0; i < node->args.size(); ++i) {
            if (i > 0) {
                out += QStringLiteral(",");
            }
            out += nodeText(node->args.at(i));
        }
        return out + QStringLiteral(")");
    }
    case Node::Kind::Name:
        return node->text;
    case Node::Kind::StructRef:
        return node->structRef ? structRefText(*node->structRef, StructRefSpelling::Display) : errText(Err::Ref);
    case Node::Kind::Array:
        return node->array ? arrayConstantText(*node->array) : QStringLiteral("{}");
    case Node::Kind::Missing:
        return QString(); // an omitted argument prints as nothing between its commas
    }
    return QString();
}

RangeRef shiftRefWrapped(const RangeRef &ref, qint32 dr, qint32 dc)
{
    constexpr qint32 kRows = 1048576;
    constexpr qint32 kCols = 16384;
    RangeRef out = ref;
    if (!ref.wholeCols) {
        if (!ref.absRow0) out.range.topLeft.row = wrappedIndex(ref.range.topLeft.row, dr, kRows);
        if (!ref.absRow1) out.range.bottomRight.row = wrappedIndex(ref.range.bottomRight.row, dr, kRows);
    }
    if (!ref.wholeRows) {
        if (!ref.absCol0) out.range.topLeft.col = wrappedIndex(ref.range.topLeft.col, dc, kCols);
        if (!ref.absCol1) out.range.bottomRight.col = wrappedIndex(ref.range.bottomRight.col, dc, kCols);
    }
    // A range whose corners wrapped past each other reads the other way.
    if (out.range.topLeft.row > out.range.bottomRight.row) {
        std::swap(out.range.topLeft.row, out.range.bottomRight.row);
    }
    if (out.range.topLeft.col > out.range.bottomRight.col) {
        std::swap(out.range.topLeft.col, out.range.bottomRight.col);
    }
    return out;
}

namespace {

bool hasRelativeRef(const NodePtr &node)
{
    if (!node) {
        return false;
    }
    switch (node->kind) {
    case Node::Kind::Ref:
        return !(node->ref.absRow0 && node->ref.absRow1) || !(node->ref.absCol0 && node->ref.absCol1);
    case Node::Kind::Unary:
        return hasRelativeRef(node->left);
    case Node::Kind::Binary:
        return hasRelativeRef(node->left) || hasRelativeRef(node->right);
    case Node::Kind::Call:
        return std::any_of(node->args.begin(), node->args.end(), [](const NodePtr &arg) { return hasRelativeRef(arg); });
    default:
        return false;
    }
}

} // namespace

NodePtr shiftNodeWrapped(const NodePtr &node, qint32 dr, qint32 dc)
{
    if (!node || (dr == 0 && dc == 0) || !hasRelativeRef(node)) {
        return node;
    }
    switch (node->kind) {
    case Node::Kind::Ref:
        return Node::makeRef(shiftRefWrapped(node->ref, dr, dc));
    case Node::Kind::Unary:
        return Node::makeUnary(node->op, shiftNodeWrapped(node->left, dr, dc));
    case Node::Kind::Binary:
        return Node::makeBinary(node->op, shiftNodeWrapped(node->left, dr, dc), shiftNodeWrapped(node->right, dr, dc));
    case Node::Kind::Call: {
        QVector<NodePtr> args;
        args.reserve(node->args.size());
        for (const NodePtr &arg : node->args) {
            args.append(shiftNodeWrapped(arg, dr, dc));
        }
        return Node::makeCall(node->text, std::move(args));
    }
    default:
        return node;
    }
}

NodePtr shiftNode(const NodePtr &node, qint32 dr, qint32 dc)
{
    if (!node) {
        return node;
    }
    switch (node->kind) {
    case Node::Kind::Number:
    case Node::Kind::Text:
    case Node::Kind::Bool:
    case Node::Kind::Error:
    case Node::Kind::Name:
    case Node::Kind::StructRef: // names its table and columns: nothing moves
    case Node::Kind::Array:
    case Node::Kind::Missing:
        return node;
    case Node::Kind::Ref:
        return Node::makeRef(shiftRef(node->ref, dr, dc));
    case Node::Kind::Unary:
        return Node::makeUnary(node->op, shiftNode(node->left, dr, dc));
    case Node::Kind::Binary:
        return Node::makeBinary(node->op, shiftNode(node->left, dr, dc), shiftNode(node->right, dr, dc));
    case Node::Kind::Call: {
        QVector<NodePtr> args;
        args.reserve(node->args.size());
        for (const NodePtr &arg : node->args) {
            args.append(shiftNode(arg, dr, dc));
        }
        return Node::makeCall(node->text, std::move(args));
    }
    }
    return node;
}

QString nameBodyText(const NodePtr &node)
{
    // AGENT-NOTE (M90, D-098): Excel writes a multi-area name without
    // brackets ("Sheet1!$A$1:$B$2,Sheet1!$D$4"), and Excel's Solver reads
    // its solver_adj that way; nodeText brackets every union.
    if (node && node->kind == Node::Kind::Binary && node->op == Op::Union) {
        return nameBodyText(node->left) + QLatin1Char(',') + nameBodyText(node->right);
    }
    return nodeText(node);
}

} // namespace QindaOffice::Calc
