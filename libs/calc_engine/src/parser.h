// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Formula AST and the hand-written Pratt parser (plan section 7.2).
// Precedence high->low: ':' range, unary -/+, postfix %, ^, * /, + -, &,
// comparisons. Excel's rule -2^2 = 4 holds because unary binds tighter than
// ^. ';' is accepted as an argument separator on input (inside an array
// constant it separates rows). M69 (D-078): structured references, array
// constants and the implicit-intersection operator '@' (parsed as a call
// of SINGLE, printed back as '@', the way A1# is ANCHORARRAY).

#include "structured_ref.h"
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

// An array constant, {1,2;3,4} (M69, D-078): rows of literal values —
// numbers, text, booleans, errors — ',' between columns, ';' between rows.
struct ArrayConstant {
    qint32 rows = 0;
    qint32 cols = 0;
    QVector<Value> cells; // row-major, rows * cols
    bool operator==(const ArrayConstant &) const = default;
};

struct Node {
    // StructRef, Array and Missing (an omitted argument: IF(A1,,1),
    // VLOOKUP(…,2,)) since M69 (D-078).
    enum class Kind { Number, Text, Bool, Error, Ref, Unary, Binary, Call, Name, StructRef, Array, Missing };
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
    std::shared_ptr<const StructRef> structRef;   // StructRef
    std::shared_ptr<const ArrayConstant> array;   // Array

    static NodePtr makeNumber(double v);
    static NodePtr makeText(const QString &s);
    static NodePtr makeBool(bool b);
    static NodePtr makeError(Err e);
    static NodePtr makeRef(const RangeRef &r);
    static NodePtr makeUnary(Op op, NodePtr operand);
    static NodePtr makeBinary(Op op, NodePtr lhs, NodePtr rhs);
    static NodePtr makeCall(const QString &name, QVector<NodePtr> args);
    static NodePtr makeName(const QString &identifier);
    static NodePtr makeStructRef(const StructRef &ref);
    static NodePtr makeArray(const ArrayConstant &array);
    static NodePtr makeMissing();
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

// A defined name's body as Excel writes it: like nodeText, but a top-level
// union without its brackets ("A1:B2,D4") — M90, D-098.
[[nodiscard]] QString nameBodyText(const NodePtr &node);

// Relative references shifted by (dr, dc); absolute axes stay put. Used to
// reconstruct shared-formula dependents (plan section 7.2).
[[nodiscard]] NodePtr shiftNode(const NodePtr &node, qint32 dr, qint32 dc);

// The same around the sheet's edges (row 1048577 is row 1 again), for a
// defined name's relative references: Excel stores them as if the name
// were used in A1 and wraps them (M69, D-078). Returns `node` itself when
// it has no relative reference.
[[nodiscard]] NodePtr shiftNodeWrapped(const NodePtr &node, qint32 dr, qint32 dc);
[[nodiscard]] RangeRef shiftRefWrapped(const RangeRef &ref, qint32 dr, qint32 dc);

} // namespace QindaOffice::Calc
