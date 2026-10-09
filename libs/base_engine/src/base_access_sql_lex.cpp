// SPDX-License-Identifier: GPL-3.0-or-later
// Access SQL view: the tokenizer that keeps positions, the boolean
// structure of WHERE/HAVING, comparison terms and the shared spellings
// (base_access_sql_p.h).
#include "base_access_sql_p.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace QindaOffice::Base::AccessSql {

bool Token::is(const char *word) const
{
    return kind == Word && text.compare(QLatin1String(word), Qt::CaseInsensitive) == 0;
}

bool Token::op(const char *chars) const
{
    return kind == Op && text == QLatin1String(chars);
}

QList<Token> tokenize(const QString &src, QString *error)
{
    QList<Token> out;
    const int n = int(src.size());
    int i = 0;
    while (i < n) {
        const QChar c = src.at(i);
        if (c.isSpace()) {
            ++i;
        } else if (c.isDigit() || (c == QLatin1Char('.') && i + 1 < n && src.at(i + 1).isDigit()
                                   && (out.isEmpty() || out.last().kind == Token::Op))) {
            int j = i;
            while (j < n && (src.at(j).isDigit() || src.at(j) == QLatin1Char('.'))) {
                ++j;
            }
            out.append({Token::Number, src.mid(i, j - i), i, j});
            i = j;
        } else if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            QString text;
            int j = i + 1;
            for (; j < n; ++j) {
                if (src.at(j) == c) {
                    if (j + 1 < n && src.at(j + 1) == c) {
                        text.append(c);
                        ++j;
                        continue;
                    }
                    break;
                }
                text.append(src.at(j));
            }
            if (j >= n) {
                *error = QStringLiteral("A quoted text is not closed.");
                return {};
            }
            out.append({Token::String, text, i, j + 1});
            i = j + 1;
        } else if (c == QLatin1Char('#') || c == QLatin1Char('[')) {
            const QChar close = c == QLatin1Char('#') ? QLatin1Char('#') : QLatin1Char(']');
            const int j = int(src.indexOf(close, i + 1));
            if (j < 0) {
                *error = c == QLatin1Char('#') ? QStringLiteral("A #date# is not closed.")
                                               : QStringLiteral("A [name] is not closed.");
                return {};
            }
            out.append({c == QLatin1Char('#') ? Token::Date : Token::Bracket, src.mid(i + 1, j - i - 1).trimmed(), i, j + 1});
            i = j + 1;
        } else if (c.isLetter() || c == QLatin1Char('_')) {
            int j = i;
            while (j < n && (src.at(j).isLetterOrNumber() || src.at(j) == QLatin1Char('_'))) {
                ++j;
            }
            out.append({Token::Word, src.mid(i, j - i), i, j});
            i = j;
        } else {
            const QString pair = src.mid(i, 2);
            if (pair == QLatin1String("<>") || pair == QLatin1String("<=") || pair == QLatin1String(">=")
                || pair == QLatin1String("!=")) {
                out.append({Token::Op, pair == QLatin1String("!=") ? QStringLiteral("<>") : pair, i, i + 2});
                i += 2;
            } else if (QStringLiteral("=<>+-*/\\&^(),.;!").contains(c)) {
                out.append({Token::Op, QString(c), i, i + 1});
                ++i;
            } else {
                *error = QStringLiteral("Unexpected character %1.").arg(c);
                return {};
            }
        }
    }
    out.append({Token::End, {}, n, n});
    return out;
}

QString textOf(const QString &src, const QList<Token> &tokens, int first, int last)
{
    if (first >= last) {
        return {};
    }
    return src.mid(tokens.at(first).begin, tokens.at(last - 1).end - tokens.at(first).begin).trimmed();
}

int closing(const QList<Token> &tokens, int open)
{
    int depth = 0;
    for (int i = open; i < tokens.size(); ++i) {
        if (tokens.at(i).op("(")) {
            ++depth;
        } else if (tokens.at(i).op(")") && --depth == 0) {
            return i;
        }
    }
    return -1;
}

namespace {

class BoolParser {
public:
    BoolParser(const QList<Token> &tokens, int last)
        : m_t(tokens), m_last(last)
    {
    }
    QString error;
    int pos = 0;

    BoolNode parseOr()
    {
        BoolNode left = parseAnd();
        if (!at("or")) {
            return left;
        }
        BoolNode node{BoolNode::Or, {left}, 0, 0};
        while (at("or") && error.isEmpty()) {
            ++pos;
            node.children.append(parseAnd());
        }
        return node;
    }

private:
    [[nodiscard]] bool at(const char *word) const { return pos < m_last && m_t.at(pos).is(word); }

    BoolNode parseAnd()
    {
        BoolNode left = parseNot();
        if (!at("and")) {
            return left;
        }
        BoolNode node{BoolNode::And, {left}, 0, 0};
        while (at("and") && error.isEmpty()) {
            ++pos;
            node.children.append(parseNot());
        }
        return node;
    }
    BoolNode parseNot()
    {
        if (at("not") && !(pos + 1 < m_last && (m_t.at(pos + 1).is("like") || m_t.at(pos + 1).is("in")
                                                || m_t.at(pos + 1).is("between")))) {
            ++pos;
            return BoolNode{BoolNode::Not, {parseNot()}, 0, 0};
        }
        return parsePrimary();
    }
    BoolNode parsePrimary()
    {
        if (pos < m_last && m_t.at(pos).op("(")) {
            const int close = closing(m_t, pos);
            const int after = close + 1;
            if (close > 0 && close < m_last
                && (after >= m_last || m_t.at(after).is("and") || m_t.at(after).is("or") || m_t.at(after).op(")"))) {
                const int outer = m_last;
                m_last = close;
                ++pos;
                BoolNode inner = parseOr();
                if (pos != close && error.isEmpty()) {
                    error = QStringLiteral("Unexpected \"%1\".").arg(m_t.at(pos).text);
                }
                m_last = outer;
                pos = after;
                return BoolNode{BoolNode::Group, {inner}, 0, 0};
            }
        }
        // A term: up to an AND (not Between's), an OR or the end.
        const int start = pos;
        int depth = 0;
        bool between = false;
        while (pos < m_last) {
            const Token &t = m_t.at(pos);
            if (t.op("(")) {
                ++depth;
            } else if (t.op(")")) {
                if (depth == 0) {
                    break;
                }
                --depth;
            } else if (depth == 0) {
                if (t.is("between")) {
                    between = true;
                } else if (t.is("and") && between) {
                    between = false;
                } else if (t.is("and") || t.is("or")) {
                    break;
                }
            }
            ++pos;
        }
        if (start == pos && error.isEmpty()) {
            error = QStringLiteral("A condition is missing.");
        }
        return BoolNode{BoolNode::Term, {}, start, pos};
    }

    const QList<Token> &m_t;
    int m_last = 0;
};

} // namespace

bool parseBool(const QList<Token> &tokens, int first, int last, BoolNode *out, QString *error)
{
    BoolParser parser(tokens, last);
    parser.pos = first;
    *out = parser.parseOr();
    if (parser.error.isEmpty() && parser.pos != last) {
        parser.error = QStringLiteral("Unexpected \"%1\".").arg(tokens.at(parser.pos).text);
    }
    *error = parser.error;
    return parser.error.isEmpty();
}

bool splitTerm(const QString &src, const QList<Token> &tokens, int first, int last, SplitTerm *out)
{
    static const QSet<QString> comparisons{QStringLiteral("="), QStringLiteral("<>"), QStringLiteral("<"),
                                           QStringLiteral(">"), QStringLiteral("<="), QStringLiteral(">=")};
    int depth = 0;
    for (int i = first; i < last; ++i) {
        const Token &t = tokens.at(i);
        if (t.op("(")) {
            ++depth;
        } else if (t.op(")")) {
            --depth;
        }
        if (depth != 0 || i == first) {
            continue;
        }
        const bool keyword = t.is("like") || t.is("between") || t.is("in") || t.is("is")
                             || (t.is("not") && i + 1 < last
                                 && (tokens.at(i + 1).is("like") || tokens.at(i + 1).is("in")
                                     || tokens.at(i + 1).is("between")));
        if ((t.kind == Token::Op && comparisons.contains(t.text)) || keyword) {
            out->first = first;
            out->opAt = i;
            out->criteria = t.op("=") ? textOf(src, tokens, i + 1, last) : textOf(src, tokens, i, last);
            return !out->criteria.isEmpty() && i + 1 < last;
        }
    }
    return false;
}

bool isKeyword(const QString &word)
{
    static const QSet<QString> words{
        QStringLiteral("select"), QStringLiteral("from"),     QStringLiteral("where"),      QStringLiteral("group"),
        QStringLiteral("by"),     QStringLiteral("having"),   QStringLiteral("order"),      QStringLiteral("as"),
        QStringLiteral("and"),    QStringLiteral("or"),       QStringLiteral("not"),        QStringLiteral("in"),
        QStringLiteral("is"),     QStringLiteral("null"),     QStringLiteral("like"),       QStringLiteral("between"),
        QStringLiteral("on"),     QStringLiteral("join"),     QStringLiteral("inner"),      QStringLiteral("left"),
        QStringLiteral("right"),  QStringLiteral("outer"),    QStringLiteral("distinct"),   QStringLiteral("distinctrow"),
        QStringLiteral("top"),    QStringLiteral("percent"),  QStringLiteral("into"),       QStringLiteral("insert"),
        QStringLiteral("update"), QStringLiteral("set"),      QStringLiteral("delete"),     QStringLiteral("values"),
        QStringLiteral("transform"), QStringLiteral("pivot"), QStringLiteral("parameters"), QStringLiteral("union"),
        QStringLiteral("all"),    QStringLiteral("asc"),      QStringLiteral("desc"),       QStringLiteral("true"),
        QStringLiteral("false"),  QStringLiteral("yes"),      QStringLiteral("no"),         QStringLiteral("date"),
        QStringLiteral("time"),   QStringLiteral("name"),     QStringLiteral("value"),      QStringLiteral("year"),
        QStringLiteral("month"),  QStringLiteral("day"),      QStringLiteral("level"),      QStringLiteral("text"),
        QStringLiteral("number"), QStringLiteral("currency"), QStringLiteral("mod"),        QStringLiteral("exists"),
        QStringLiteral("count"),  QStringLiteral("sum"),      QStringLiteral("avg"),        QStringLiteral("min"),
        QStringLiteral("max"),    QStringLiteral("first"),    QStringLiteral("last"),       QStringLiteral("cross")};
    return words.contains(word.toLower());
}

QString ident(const QString &name)
{
    static const QRegularExpression plain(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));
    return plain.match(name).hasMatch() && !isKeyword(name) ? name : QLatin1Char('[') + name + QLatin1Char(']');
}

QString accessAggregate(const QString &sqlFunction)
{
    const QString f = sqlFunction.toLower();
    return f == QLatin1String("sum")     ? QStringLiteral("Sum")
           : f == QLatin1String("avg")   ? QStringLiteral("Avg")
           : f == QLatin1String("count") ? QStringLiteral("Count")
           : f == QLatin1String("min")   ? QStringLiteral("Min")
           : f == QLatin1String("max")   ? QStringLiteral("Max")
                                         : QString();
}

QString parameterType(FieldType type)
{
    switch (type) {
    case FieldType::LongText: return QStringLiteral("LongText");
    case FieldType::AutoNumber:
    case FieldType::Integer: return QStringLiteral("Long");
    case FieldType::Number: return QStringLiteral("Double");
    case FieldType::Currency: return QStringLiteral("Currency");
    case FieldType::YesNo: return QStringLiteral("YesNo");
    case FieldType::Date: return QStringLiteral("Date");
    case FieldType::DateTime: return QStringLiteral("DateTime");
    default: return QStringLiteral("Text");
    }
}

std::optional<FieldType> parameterTypeFrom(const QString &word)
{
    const QString w = word.toLower();
    static const QHash<QString, FieldType> types{
        {QStringLiteral("text"), FieldType::Text},          {QStringLiteral("char"), FieldType::Text},
        {QStringLiteral("varchar"), FieldType::Text},       {QStringLiteral("string"), FieldType::Text},
        {QStringLiteral("memo"), FieldType::LongText},      {QStringLiteral("longtext"), FieldType::LongText},
        {QStringLiteral("longchar"), FieldType::LongText},  {QStringLiteral("byte"), FieldType::Integer},
        {QStringLiteral("short"), FieldType::Integer},      {QStringLiteral("smallint"), FieldType::Integer},
        {QStringLiteral("integer"), FieldType::Integer},    {QStringLiteral("int"), FieldType::Integer},
        {QStringLiteral("long"), FieldType::Integer},       {QStringLiteral("counter"), FieldType::Integer},
        {QStringLiteral("single"), FieldType::Number},      {QStringLiteral("double"), FieldType::Number},
        {QStringLiteral("float"), FieldType::Number},       {QStringLiteral("real"), FieldType::Number},
        {QStringLiteral("number"), FieldType::Number},      {QStringLiteral("numeric"), FieldType::Number},
        {QStringLiteral("decimal"), FieldType::Number},     {QStringLiteral("ieeesingle"), FieldType::Number},
        {QStringLiteral("ieeedouble"), FieldType::Number},  {QStringLiteral("currency"), FieldType::Currency},
        {QStringLiteral("money"), FieldType::Currency},     {QStringLiteral("bit"), FieldType::YesNo},
        {QStringLiteral("yesno"), FieldType::YesNo},        {QStringLiteral("logical"), FieldType::YesNo},
        {QStringLiteral("boolean"), FieldType::YesNo},      {QStringLiteral("date"), FieldType::Date},
        {QStringLiteral("datetime"), FieldType::DateTime},  {QStringLiteral("time"), FieldType::DateTime},
        {QStringLiteral("timestamp"), FieldType::DateTime}};
    if (types.contains(w)) {
        return types.value(w);
    }
    return std::nullopt;
}

} // namespace QindaOffice::Base::AccessSql
