// SPDX-License-Identifier: GPL-3.0-or-later
// Access-style expressions and criteria → SQL: a tokenizer and a recursive
// descent parser that emits SQL directly, tracking each value's type so
// literals can be converted by the field they meet (base_query.h contract).
#include "base_expression_p.h"

#include <QHash>
#include <QRegularExpression>

namespace QindaOffice::Base::Expr {

using detail::Token;
using detail::likePattern;
using detail::tokenize;

namespace {

struct Value {
    Value() = default;
    Value(QString sqlText, std::optional<FieldType> valueType = std::nullopt, bool isAggregate = false)
        : sql(std::move(sqlText)), type(valueType), aggregate(isAggregate)
    {
    }
    QString sql;
    std::optional<FieldType> type;
    bool aggregate = false;
    bool literal = false;
    Token::Kind literalKind = Token::End;
    QString literalText;
    int parameter = -1;
};

bool integral(std::optional<FieldType> t)
{
    return t && (*t == FieldType::Integer || *t == FieldType::AutoNumber || *t == FieldType::YesNo);
}

class Parser {
public:
    Parser(const QList<Token> &tokens, const Context &context, bool criteriaMode)
        : m_t(tokens), m_ctx(context), m_criteria(criteriaMode)
    {
    }
    QString error;

    Value parseOr()
    {
        Value left = parseAnd();
        while (word(u"or")) {
            const Value right = parseAnd();
            left = logical(left, right, QStringLiteral("OR"));
        }
        return left;
    }

    // criteria: the column is the implicit left operand of every atom.
    Value criteriaOr(const Value &column)
    {
        Value left = criteriaAnd(column);
        while (word(u"or")) {
            left = logical(left, criteriaAnd(column), QStringLiteral("OR"));
        }
        return left;
    }

    [[nodiscard]] bool atEnd() const { return m_t.at(m_pos).kind == Token::End; }

    // Converts a literal (or types a parameter) for a value of `type`.
    QString adopt(Value &v, std::optional<FieldType> type)
    {
        if (v.parameter >= 0 && type && m_ctx.parameters != nullptr && !m_ctx.parameters->at(v.parameter).declared) {
            (*m_ctx.parameters)[v.parameter].type = *type;
        }
        if (!v.literal || !type || v.literalKind == Token::End) {
            return v.sql;
        }
        const FieldType t = *type;
        QString text = v.literalText;
        if (v.literalKind == Token::Word) {
            const QString lower = text.toLower();
            if (t == FieldType::YesNo || isNumeric(t)) {
                text = lower == QLatin1String("true") || lower == QLatin1String("yes") ? QStringLiteral("1")
                                                                                     : QStringLiteral("0");
            }
        }
        const Parsed parsed = parseInput(t, text, m_ctx.currency);
        if (!parsed.ok) {
            error = QStringLiteral("\"%1\" %2.").arg(v.literalText, parsed.error);
            return v.sql;
        }
        v.type = t;
        return sqlLiteral(parsed.value);
    }

private:
    const Token &peek(int ahead = 0) const { return m_t.at(qMin(m_pos + ahead, int(m_t.size()) - 1)); }
    bool op(const char *text)
    {
        if (peek().kind == Token::Op && peek().text == QLatin1String(text)) {
            ++m_pos;
            return true;
        }
        return false;
    }
    bool isWord(const char16_t *w, int ahead = 0) const
    {
        return peek(ahead).kind == Token::Word && peek(ahead).text.compare(QStringView(w), Qt::CaseInsensitive) == 0;
    }
    bool word(const char16_t *w)
    {
        if (isWord(w)) {
            ++m_pos;
            return true;
        }
        return false;
    }
    void expect(const char *text)
    {
        if (!op(text) && error.isEmpty()) {
            error = QStringLiteral("Expected %1 near \"%2\".").arg(QLatin1String(text), peek().text);
        }
    }
    Value logical(const Value &a, const Value &b, const QString &keyword)
    {
        return {QStringLiteral("(%1 %2 %3)").arg(a.sql, keyword, b.sql), FieldType::YesNo, a.aggregate || b.aggregate};
    }

    Value criteriaAnd(const Value &column)
    {
        Value left = criteriaNot(column);
        while (word(u"and")) {
            left = logical(left, criteriaNot(column), QStringLiteral("AND"));
        }
        return left;
    }
    Value criteriaNot(const Value &column)
    {
        if (isWord(u"not") && !isWord(u"like", 1) && !isWord(u"in", 1)) {
            ++m_pos;
            const Value inner = criteriaNot(column);
            return {QStringLiteral("NOT %1").arg(inner.sql), FieldType::YesNo, inner.aggregate};
        }
        Value col = column;
        if (peek().kind == Token::Op
            && QStringList{QStringLiteral("="), QStringLiteral("<>"), QStringLiteral("<"), QStringLiteral("<="),
                           QStringLiteral(">"), QStringLiteral(">=")}
                   .contains(peek().text)) {
            return comparisonTail(col);
        }
        if (isWord(u"like") || isWord(u"between") || isWord(u"in") || isWord(u"is")
            || (isWord(u"not") && (isWord(u"like", 1) || isWord(u"in", 1)))) {
            return comparisonTail(col);
        }
        Value value = parseConcat();
        const QString rhs = adopt(value, column.type);
        if (value.literal && value.literalKind == Token::End) { // Null
            return {QStringLiteral("(%1 IS NULL)").arg(column.sql), FieldType::YesNo};
        }
        return {QStringLiteral("(%1 = %2)").arg(column.sql, rhs), FieldType::YesNo, value.aggregate};
    }

    Value parseAnd()
    {
        Value left = parseNot();
        while (word(u"and")) {
            left = logical(left, parseNot(), QStringLiteral("AND"));
        }
        return left;
    }
    Value parseNot()
    {
        if (word(u"not")) {
            const Value inner = parseNot();
            return {QStringLiteral("NOT %1").arg(inner.sql), FieldType::YesNo, inner.aggregate};
        }
        Value left = parseConcat();
        return comparisonTail(left);
    }
    // After a left value: an optional comparison.
    Value comparisonTail(Value &left)
    {
        static const QStringList cmp{QStringLiteral("="), QStringLiteral("<>"), QStringLiteral("<"),
                                     QStringLiteral("<="), QStringLiteral(">"), QStringLiteral(">=")};
        if (peek().kind == Token::Op && cmp.contains(peek().text)) {
            const QString o = peek().text;
            ++m_pos;
            Value right = parseConcat();
            const QString r = adopt(right, left.type);
            const QString l = adopt(left, right.type);
            return {QStringLiteral("(%1 %2 %3)").arg(l, o, r), FieldType::YesNo, left.aggregate || right.aggregate};
        }
        const bool negate = isWord(u"not") && (isWord(u"like", 1) || isWord(u"in", 1));
        if (negate) {
            ++m_pos;
        }
        const QString notText = negate ? QStringLiteral("NOT ") : QString();
        if (word(u"like")) {
            Value pattern = parseConcat();
            const QString p = pattern.literal && pattern.literalKind == Token::String ? likePattern(pattern.literalText)
                                                                                      : pattern.sql;
            return {QStringLiteral("(%1 %2LIKE %3 ESCAPE '\\')").arg(left.sql, notText, p), FieldType::YesNo};
        }
        if (word(u"between")) {
            Value low = parseConcat();
            if (!word(u"and") && error.isEmpty()) {
                error = QStringLiteral("Between needs \"And\".");
            }
            Value high = parseConcat();
            const QString l = adopt(low, left.type);
            const QString h = adopt(high, left.type);
            return {QStringLiteral("(%1 BETWEEN %2 AND %3)").arg(left.sql, l, h), FieldType::YesNo, left.aggregate};
        }
        if (word(u"in")) {
            expect("(");
            QStringList items;
            do {
                Value item = parseConcat();
                items.append(adopt(item, left.type));
            } while (op(",") && error.isEmpty());
            expect(")");
            return {QStringLiteral("(%1 %2IN (%3))").arg(left.sql, notText, items.join(QStringLiteral(", "))),
                    FieldType::YesNo, left.aggregate};
        }
        if (word(u"is")) {
            const bool isNot = word(u"not");
            if (!word(u"null") && error.isEmpty()) {
                error = QStringLiteral("\"Is\" must be followed by Null or Not Null.");
            }
            return {QStringLiteral("(%1 IS %2NULL)").arg(left.sql, isNot ? QStringLiteral("NOT ") : QString()),
                    FieldType::YesNo, left.aggregate};
        }
        return left;
    }
    Value parseConcat()
    {
        Value left = parseAdditive();
        while (op("&")) {
            const Value right = parseAdditive();
            left = {QStringLiteral("(coalesce(%1, '') || coalesce(%2, ''))").arg(left.sql, right.sql), FieldType::Text,
                    left.aggregate || right.aggregate};
        }
        return left;
    }
    Value parseAdditive()
    {
        Value left = parseMultiplicative();
        while (peek().kind == Token::Op && (peek().text == QLatin1String("+") || peek().text == QLatin1String("-"))) {
            const QString o = peek().text;
            ++m_pos;
            Value right = parseMultiplicative();
            const bool dateLeft = left.type == FieldType::Date || left.type == FieldType::DateTime;
            if (dateLeft && right.literal && right.literalKind == Token::Number) {
                // Date arithmetic in days: [given] + 30, Date() - 7.
                const QString fn = left.type == FieldType::Date ? QStringLiteral("date") : QStringLiteral("datetime");
                left = {QStringLiteral("%1(%2, '%3%4 days')").arg(fn, left.sql, o, right.literalText), left.type,
                        left.aggregate};
                continue;
            }
            const std::optional<FieldType> money =
                left.type == FieldType::Currency || right.type == FieldType::Currency
                    ? std::optional<FieldType>(FieldType::Currency)
                    : std::nullopt;
            const QString l = money ? adopt(left, money) : left.sql;
            const QString r = money ? adopt(right, money) : right.sql;
            const std::optional<FieldType> type =
                money ? money
                      : (integral(left.type) && integral(right.type) ? FieldType::Integer : FieldType::Number);
            left = {QStringLiteral("(%1 %2 %3)").arg(l, o, r), type, left.aggregate || right.aggregate};
        }
        return left;
    }
    Value parseMultiplicative()
    {
        Value left = parseUnary();
        while ((peek().kind == Token::Op && (peek().text == QLatin1String("*") || peek().text == QLatin1String("/")))
               || isWord(u"mod")) {
            const QString o = peek().kind == Token::Word ? QStringLiteral("%") : peek().text;
            ++m_pos;
            const Value right = parseUnary();
            const bool leftMoney = left.type == FieldType::Currency;
            const bool rightMoney = right.type == FieldType::Currency;
            std::optional<FieldType> type = FieldType::Number;
            if ((leftMoney != rightMoney) && o != QLatin1String("%")) {
                type = FieldType::Currency;
            } else if (integral(left.type) && integral(right.type) && o != QLatin1String("/")) {
                type = FieldType::Integer;
            }
            const QString sql = o == QLatin1String("/") ? QStringLiteral("(%1 * 1.0 / %2)").arg(left.sql, right.sql)
                                                        : QStringLiteral("(%1 %2 %3)").arg(left.sql, o, right.sql);
            left = {sql, type, left.aggregate || right.aggregate};
        }
        return left;
    }
    Value parseUnary()
    {
        if (op("-")) {
            Value inner = parseUnary();
            if (inner.literal && inner.literalKind == Token::Number) {
                inner.literalText.prepend(QLatin1Char('-'));
                inner.sql.prepend(QLatin1Char('-'));
                return inner;
            }
            return {QStringLiteral("(-%1)").arg(inner.sql), inner.type, inner.aggregate};
        }
        return parsePrimary();
    }
    Value literal(Token::Kind kind, const QString &text, const QString &sql, std::optional<FieldType> type)
    {
        Value v{sql, type};
        v.literal = true;
        v.literalKind = kind;
        v.literalText = text;
        return v;
    }
    Value parameter(const QString &prompt)
    {
        if (m_ctx.parameters == nullptr) {
            error = QStringLiteral("There is no field named %1.").arg(prompt);
            return {};
        }
        for (int i = 0; i < m_ctx.parameters->size(); ++i) {
            if (m_ctx.parameters->at(i).prompt.compare(prompt, Qt::CaseInsensitive) == 0) {
                Value v{QLatin1Char(':') + m_ctx.parameters->at(i).name, std::nullopt};
                v.parameter = i;
                return v;
            }
        }
        const QString unique = parameterNameFor(prompt, *m_ctx.parameters);
        m_ctx.parameters->append({unique, prompt, FieldType::Text});
        Value v{QLatin1Char(':') + unique, std::nullopt};
        v.parameter = int(m_ctx.parameters->size()) - 1;
        return v;
    }
    Value field(const QString &table, const QString &name, bool bracketed)
    {
        if (const std::optional<FieldRef> ref = m_ctx.resolve(table, name)) {
            return {ref->sql, ref->type};
        }
        if (table.isEmpty() && bracketed) {
            return parameter(name);
        }
        if (table.isEmpty() && m_criteria) {
            return literal(Token::String, name, quoteLiteral(name), FieldType::Text); // a bare word is text
        }
        error = table.isEmpty() ? QStringLiteral("Unknown name %1 (put text in quotes and fields in [brackets]).").arg(name)
                                : QStringLiteral("There is no field %1.%2.").arg(table, name);
        return {};
    }
    Value call(const QString &name);
    Value parsePrimary()
    {
        const Token t = peek();
        switch (t.kind) {
        case Token::Number:
            ++m_pos;
            return literal(Token::Number, t.text, t.text.contains(QLatin1Char('.')) ? t.text : t.text,
                           t.text.contains(QLatin1Char('.')) ? FieldType::Number : FieldType::Integer);
        case Token::String:
            ++m_pos;
            return literal(Token::String, t.text, quoteLiteral(t.text), std::nullopt);
        case Token::Date: {
            ++m_pos;
            const Parsed d = parseInput(FieldType::Date, t.text);
            if (!d.ok) {
                error = QStringLiteral("#%1# is not a date.").arg(t.text);
                return {};
            }
            return literal(Token::Date, t.text, quoteLiteral(d.value.toString()), FieldType::Date);
        }
        case Token::Bracket: {
            ++m_pos;
            if (peek().kind == Token::Op && peek().text == QLatin1String(".") && peek(1).kind == Token::Bracket) {
                const QString f = peek(1).text;
                m_pos += 2;
                return field(t.text, f, true);
            }
            return field(QString(), t.text, true);
        }
        case Token::Word: {
            ++m_pos;
            const QString lower = t.text.toLower();
            if (lower == QLatin1String("null")) {
                return literal(Token::End, t.text, QStringLiteral("NULL"), std::nullopt);
            }
            if (lower == QLatin1String("true") || lower == QLatin1String("yes") || lower == QLatin1String("false")
                || lower == QLatin1String("no")) {
                const bool on = lower == QLatin1String("true") || lower == QLatin1String("yes");
                return literal(Token::Word, t.text, on ? QStringLiteral("1") : QStringLiteral("0"), FieldType::YesNo);
            }
            if (op("(")) {
                return call(lower);
            }
            if (peek().kind == Token::Op && peek().text == QLatin1String(".")
                && (peek(1).kind == Token::Word || peek(1).kind == Token::Bracket)) {
                const QString f = peek(1).text;
                m_pos += 2;
                return field(t.text, f, false);
            }
            return field(QString(), t.text, false);
        }
        case Token::Op:
            if (op("(")) {
                Value inner = parseOr();
                expect(")");
                inner.sql = QLatin1Char('(') + inner.sql + QLatin1Char(')');
                return inner;
            }
            break;
        case Token::End:
            break;
        }
        if (error.isEmpty()) {
            error = t.kind == Token::End ? QStringLiteral("The expression ends too early.")
                                         : QStringLiteral("Unexpected \"%1\".").arg(t.text);
        }
        ++m_pos;
        return {};
    }

    const QList<Token> &m_t;
    const Context &m_ctx;
    bool m_criteria = false;
    int m_pos = 0;
};

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
    Parser parser(tokens, context, false);
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
    Parser parser(tokens, context, true);
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
    Parser parser(tokens, context, false);
    Value v = parser.parseOr();
    Compiled out = finish(parser, v);
    if (out.ok()) {
        out.sql = parser.adopt(v, targetType);
        out.error = parser.error;
    }
    return out;
}

} // namespace QindaOffice::Base::Expr
