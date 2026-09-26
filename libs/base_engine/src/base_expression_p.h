// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal parts of the expression compiler shared by base_expression*.cpp.

#include "base_expression.h"

namespace QindaOffice::Base::Expr::detail {

struct Token {
    enum Kind { End, Number, String, Date, Bracket, Word, Op } kind = End;
    QString text;
};

QList<Token> tokenize(const QString &src, QString *error);
// Criteria made only of plain words become one text token.
QList<Token> bareWordsAsText(QList<Token> tokens, const QString &text);
// An Access Like pattern (* ? literal % _) as a quoted SQL LIKE pattern.
QString likePattern(const QString &access);

struct CallArg {
    QString sql;
    std::optional<FieldType> type;
    bool aggregate = false;
};

struct CallResult {
    CallResult() = default;
    CallResult(QString sqlText, std::optional<FieldType> valueType = std::nullopt, bool isAggregate = false,
               QString problem = {})
        : sql(std::move(sqlText)), type(valueType), aggregate(isAggregate), error(std::move(problem))
    {
    }
    QString sql;
    std::optional<FieldType> type;
    bool aggregate = false;
    QString error;
};

// An Access function call as SQL, or an error for an unknown function.
CallResult callSql(const QString &name, const QList<CallArg> &args);

} // namespace QindaOffice::Base::Expr::detail
