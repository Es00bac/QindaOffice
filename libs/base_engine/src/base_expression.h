// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Access-style expressions and criteria → SQLite SQL (used by the query
// compiler; internal to base_engine). See base_query.h for the safety
// contract: literals are converted by type and quoted, names resolve
// against the query's fields, unknown [names] become parameters.

#include "base_query.h"

#include <functional>
#include <optional>

namespace QindaOffice::Base::Expr {

struct FieldRef {
    QString sql;  // "\"donations\".\"amount\""
    FieldType type = FieldType::Text;
};

struct Context {
    // (table or "", field) → the field, or nullopt when unknown.
    std::function<std::optional<FieldRef>(const QString &, const QString &)> resolve;
    CurrencyFormat currency;
    QList<QueryParameter> *parameters = nullptr;
    // M86 (D-094): compiles a subquery's Access SQL ("SELECT [city] FROM
    // [donors] GROUP BY [city] HAVING Count(*) > 1") into SQLite SQL, or
    // sets `error`. Unset where a subquery means nothing (rules, macros,
    // calculated fields): a subquery there is an error.
    std::function<QString(const QString &accessSql, QString *error)> subquery;
};

struct Compiled {
    QString sql;
    std::optional<FieldType> type;
    bool aggregate = false;
    QString error;
    // valueFor (M86): the text was one literal, already converted to the
    // target type's storage ("100" on money is 10000 minor units).
    bool literal = false;
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

// A value expression ("[qty] * [price]", "Year([given])").
[[nodiscard]] Compiled expression(const QString &text, const Context &context);
// A criteria cell applied to a column ("> 100", "Like \"A*\"", "Is Null").
[[nodiscard]] Compiled criteria(const QString &text, const QString &columnSql, FieldType columnType,
                                const Context &context);
// A literal-or-expression for an update query's "Update to" cell.
[[nodiscard]] Compiled valueFor(const QString &text, FieldType targetType, const Context &context);
// The SQL parameter name for a prompt ("Start date" → "p_start_date"),
// unique among `existing`.
[[nodiscard]] QString parameterNameFor(const QString &prompt, const QList<QueryParameter> &existing);

} // namespace QindaOffice::Base::Expr
