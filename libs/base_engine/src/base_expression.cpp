// SPDX-License-Identifier: GPL-3.0-or-later
// Access-style expressions and criteria → SQL: a tokenizer and a recursive
// descent parser that emits SQL directly, tracking each value's type so
// literals can be converted by the field they meet (base_query.h contract).
#include "base_expression_parser.h"

#include <QHash>
#include <QRegularExpression>

namespace QindaOffice::Base::Expr {

using detail::Token;
using detail::likePattern;
using detail::tokenize;

using detail::finish;
using detail::Parser;
using detail::Value;

namespace detail {

Value Parser::call(const QString &name)
{
    QList<detail::CallArg> args;
    if (!op(")")) {
        if (name == QLatin1String("count") && op("*")) {
            expect(")");
            return {QStringLiteral("count(*)"), FieldType::Integer, true};
        }
        do {
            const Value v = parseOr();
            args.append({v.sql, v.type, v.aggregate});
        } while (op(",") && error.isEmpty());
        expect(")");
    }
    const detail::CallResult r = detail::callSql(name, args);
    if (!r.error.isEmpty() && error.isEmpty()) {
        error = r.error;
    }
    return {r.sql, r.type, r.aggregate};
}

Compiled finish(Parser &parser, const Value &value)
{
    Compiled out{value.sql, value.type, value.aggregate, parser.error};
    if (out.error.isEmpty() && !parser.atEnd()) {
        out.error = QStringLiteral("Unexpected text at the end.");
    }
    return out;
}

} // namespace

QString parameterNameFor(const QString &prompt, const QList<QueryParameter> &existing)
{
    QString name = prompt.toLower();
    name.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("_"));
    name = QStringLiteral("p_") + name.left(40);
    QString unique = name;
    for (int k = 2; std::any_of(existing.cbegin(), existing.cend(), [&](const QueryParameter &p) { return p.name == unique; });
         ++k) {
        unique = name + QString::number(k);
    }
    return unique;
}

Compiled expression(const QString &text, const Context &context)
{
    QString error;
    const QList<Token> tokens = tokenize(text, &error);
    if (!error.isEmpty()) {
        return {{}, {}, false, error};
    }
    Parser parser(tokens, context, false, text);
    const Value v = parser.parseOr();
    return finish(parser, v);
}

Compiled criteria(const QString &text, const QString &columnSql, FieldType columnType, const Context &context)
{
    QString error;
    const QList<Token> tokens = detail::bareWordsAsText(tokenize(text, &error), text);
    if (!error.isEmpty()) {
        return {{}, {}, false, error};
    }
    Parser parser(tokens, context, true, text);
    const Value v = parser.criteriaOr(Value{columnSql, columnType});
    return finish(parser, v);
}

Compiled valueFor(const QString &text, FieldType targetType, const Context &context)
{
    QString error;
    const QList<Token> tokens = tokenize(text, &error);
    if (!error.isEmpty()) {
        return {{}, {}, false, error};
    }
    Parser parser(tokens, context, false, text);
    Value v = parser.parseOr();
    Compiled out = finish(parser, v);
    if (out.ok()) {
        out.literal = v.literal && v.literalKind != Token::End;
        out.sql = parser.adopt(v, targetType);
        out.error = parser.error;
    }
    return out;
}

} // namespace QindaOffice::Base::Expr
