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
};

struct Compiled {
    QString sql;
    std::optional<FieldType> type;
    bool aggregate = false;
    QString error;
    [[nodiscard]] bool ok() const { return error.isEmpty(); }
};

// A value expression ("[qty] * [price]", "Year([given])").
[[nodiscard]] Compiled expression(const QString &text, const Context &context);
// A criteria cell applied to a column ("> 100", "Like \"A*\"", "Is Null").
[[nodiscard]] Compiled criteria(const QString &text, const QString &columnSql, FieldType columnType,
                                const Context &context);
// A literal-or-expression for an update query's "Update to" cell.
[[nodiscard]] Compiled valueFor(const QString &text, FieldType targetType, const Context &context);

} // namespace QindaOffice::Base::Expr
