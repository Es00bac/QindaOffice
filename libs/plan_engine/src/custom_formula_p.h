// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to custom_formula*.cpp (M83, D-092): Project's custom-field
// formulas in three parts — the parser (custom_formula_parse.cpp: the text
// into a small tree, so formulaProblem() refuses a bad formula before it is
// stored), the evaluator's core with the field references
// (custom_formula.cpp) and its function library
// (custom_formula_functions.cpp). custom_fields.h is the public face.
//
// AGENT-NOTE (Project's conventions, kept on purpose): durations and work are
// minutes ([Duration]/480 is days), dates are days since 30 Dec 1899 with
// "NA" as 4294967296 (ProjDateValue("NA")), costs are currency units, Yes/No
// fields are booleans (-1/0 in arithmetic, as in VBA), and "+" joins two
// texts. ProjDurConv returns the number of units, not a formatted duration.

#include "custom_fields.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <memory>

namespace QindaOffice::Plan::Formula {

inline constexpr double kNaDate = 4294967296.0;
inline constexpr double kEpochDays = 25569.0; // 1 Jan 1970 in days since 30 Dec 1899
inline constexpr int kMaxDepth = 8;           // formula fields reading formula fields

struct Node {
    enum Kind { Number, Text, Field, Call, Unary, Binary } kind = Number;
    double number = 0;
    QString text; // the text, field, function name or operator (lower case)
    QList<std::shared_ptr<Node>> args;
};
using NodePtr = std::shared_ptr<Node>;

// The tree of `source`, or nullptr with the reason in `error`.
[[nodiscard]] NodePtr parseFormula(const QString &source, QString *error);
// The functions and constants the evaluator knows (lower case).
[[nodiscard]] const QStringList &functionNames();

[[nodiscard]] double dateValue(Minutes m);
[[nodiscard]] Minutes minutesOfDate(double days);
[[nodiscard]] QString numberText(double v);

// One formula evaluated against one task. `error` is the first failure;
// `depth` counts formula fields reading formula fields (kMaxDepth).
class Evaluator {
public:
    Evaluator(const Project &project, int index, int depth)
        : m_p(project)
        , m_i(index)
        , m_depth(depth)
    {
    }
    QString error;

    QVariant eval(const NodePtr &n);

private:
    QVariant fail(const QString &why);
    double num(const QVariant &v);
    static bool isText(const QVariant &v) { return v.typeId() == QMetaType::QString; }
    QString text(const QVariant &v);
    bool truth(const QVariant &v);
    [[nodiscard]] double money(qint64 minor) const;
    QVariant binary(const QString &o, const QVariant &a, const QVariant &b);
    QVariant field(const QString &name);
    QVariant call(const NodePtr &n); // custom_formula_functions.cpp

    const Project &m_p;
    int m_i;
    int m_depth;
};

} // namespace QindaOffice::Plan::Formula
