// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Formula AST and the hand-written Pratt parser (plan section 7.2).
// Precedence high->low: ':' range, unary -/+, postfix %, ^, * /, + -, &,
// comparisons. Excel's rule -2^2 = 4 holds because unary binds tighter than
// ^. ';' is accepted as an argument separator on input.

#include "value.h"

#include <memory>

namespace QindaOffice::Calc {

struct Node;
using NodePtr = std::shared_ptr<const Node>;

enum class Op {
    Range,      // ':'
    Union,      // ',' inside non-call parentheses
    Add, Sub, Mul, Div, Pow,
    Concat,     // '&'
    Eq, Ne, Lt, Le, Gt, Ge,
    Neg, Pos,   // unary
    Percent,    // postfix
};

struct Node {
    enum class Kind { Number, Text, Bool, Error, Ref, Unary, Binary, Call, Name };
    Kind kind = Kind::Number;
    double number = 0;
    QString text;        // Text value; Call: function name; Name: identifier
    bool boolean = false;
    Err error = Err::NA;
    RangeRef ref;        // Ref (cell or range)
    Op op = Op::Add;     // Unary/Binary operator
    NodePtr left;        // Unary operand / Binary lhs
    NodePtr right;       // Binary rhs
    QVector<NodePtr> args; // Call arguments

    static NodePtr makeNumber(double v);
    static NodePtr makeText(const QString &s);
    static NodePtr makeBool(bool b);
    static NodePtr makeError(Err e);
    static NodePtr makeRef(const RangeRef &r);
    static NodePtr makeUnary(Op op, NodePtr operand);
    static NodePtr makeBinary(Op op, NodePtr lhs, NodePtr rhs);
    static NodePtr makeCall(const QString &name, QVector<NodePtr> args);
    static NodePtr makeName(const QString &identifier);
};

struct ParseResult {
    NodePtr root;
    QString error; // empty on success
    int errorPos = -1;
};

[[nodiscard]] ParseResult parseFormula(const QString &input);

// Canonical text for the AST (parser round-trip tests, shared-formula
// reconstruction). Storages form: ',' separators, '.' decimals.
[[nodiscard]] QString nodeText(const NodePtr &node);

// Relative references shifted by (dr, dc); absolute axes stay put. Used to
// reconstruct shared-formula dependents (plan section 7.2).
[[nodiscard]] NodePtr shiftNode(const NodePtr &node, qint32 dr, qint32 dc);

} // namespace QindaOffice::Calc
