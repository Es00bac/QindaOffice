// SPDX-License-Identifier: GPL-3.0-or-later
// The SQL view's parser: Access SQL back into a design the grid shows
// (base_access_sql.h). Statement shapes and FROM are here; select items,
// GROUP BY, criteria and ORDER BY are in base_access_sql_columns.cpp.
#include "base_access_sql_parser.h"

namespace QindaOffice::Base::AccessSql {

int SqlParser::find(int from, int to, const char *word) const
{
    int depth = 0;
    for (int i = from; i < to; ++i) {
        if (m_t.at(i).op("(")) {
            ++depth;
        } else if (m_t.at(i).op(")")) {
            --depth;
        } else if (depth == 0 && m_t.at(i).is(word)) {
            return i;
        }
    }
    return -1;
}

QList<SqlParser::Range> SqlParser::splitCommas(Range r) const
{
    QList<Range> out;
    int depth = 0;
    int start = r.first;
    for (int i = r.first; i < r.last; ++i) {
        if (m_t.at(i).op("(")) {
            ++depth;
        } else if (m_t.at(i).op(")")) {
            --depth;
        } else if (depth == 0 && m_t.at(i).op(",")) {
            out.append({start, i});
            start = i + 1;
        }
    }
    out.append({start, r.last});
    return out;
}

bool SqlParser::parameters()
{
    int semicolon = -1;
    for (int i = 1; i < m_t.size() && semicolon < 0; ++i) {
        semicolon = m_t.at(i).op(";") ? i : -1;
    }
    if (semicolon < 0) {
        return fail(QStringLiteral("PARAMETERS ends with a semicolon before the query."));
    }
    for (const Range &r : splitCommas({1, semicolon})) {
        if (r.last - r.first < 2 || (at(r.first).kind != Token::Bracket && at(r.first).kind != Token::Word)) {
            return fail(QStringLiteral("Each parameter is written [Name] Type."));
        }
        const std::optional<FieldType> type = parameterTypeFrom(at(r.first + 1).text);
        if (!type) {
            return fail(QStringLiteral("Unknown parameter type %1.").arg(at(r.first + 1).text));
        }
        QueryParameter p;
        p.prompt = at(r.first).text.trimmed();
        p.type = *type;
        p.declared = true;
        d.parameters.append(p);
    }
    m_first = semicolon + 1;
    return true;
}

bool SqlParser::tableRef(int &pos, int last, QStringList *refs)
{
    if (pos >= last || (at(pos).kind != Token::Word && at(pos).kind != Token::Bracket)) {
        return fail(QStringLiteral("Expected a table name near \"%1\".").arg(at(pos).text));
    }
    QueryTable t{at(pos).text, QString()};
    ++pos;
    if (pos < last && at(pos).is("as")) {
        ++pos;
    }
    static const char *const stops[] = {"inner", "left", "right", "join", "on", "outer"};
    const bool stop = std::any_of(std::begin(stops), std::end(stops), [&](const char *w) { return at(pos).is(w); });
    if (pos < last && (at(pos).kind == Token::Bracket || (at(pos).kind == Token::Word && !stop))) {
        t.alias = at(pos).text;
        ++pos;
    }
    const std::optional<TableDef> def = readTable(m_file, t.name);
    if (!def) {
        return fail(QStringLiteral("There is no table named %1.").arg(t.name));
    }
    t.name = def->name;
    if (t.alias.compare(t.name, Qt::CaseInsensitive) == 0) {
        t.alias.clear();
    }
    m_defs.insert(t.ref().toLower(), *def);
    d.tables.append(t);
    refs->append(t.ref().toLower());
    return true;
}

bool SqlParser::fromPrimary(int &pos, int last, QStringList *refs)
{
    if (pos < last && at(pos).op("(")) {
        const int close = closing(m_t, pos);
        if (close < 0 || close >= last) {
            return fail(QStringLiteral("A parenthesis in FROM is not closed."));
        }
        int inner = pos + 1;
        if (!joinExpression(inner, close, refs)) {
            return false;
        }
        if (inner != close) {
            return fail(QStringLiteral("Unexpected \"%1\" in FROM.").arg(at(inner).text));
        }
        pos = close + 1;
        return true;
    }
    return tableRef(pos, last, refs);
}

// primary { (INNER | LEFT [OUTER] | RIGHT [OUTER]) JOIN primary ON a.x = b.y }
bool SqlParser::joinExpression(int &pos, int last, QStringList *refs)
{
    if (!fromPrimary(pos, last, refs)) {
        return false;
    }
    while (pos < last && (at(pos).is("inner") || at(pos).is("left") || at(pos).is("right") || at(pos).is("join"))) {
        QString kind = at(pos).is("left") ? QStringLiteral("left") : at(pos).is("right") ? QStringLiteral("right")
                                                                                        : QStringLiteral("inner");
        while (pos < last && !at(pos).is("join")) {
            ++pos;
        }
        ++pos;
        QStringList right;
        if (!fromPrimary(pos, last, &right)) {
            return false;
        }
        if (!at(pos).is("on")) {
            return fail(QStringLiteral("A JOIN needs ON."));
        }
        ++pos;
        // The condition runs to the next join or the end of this group.
        int a = pos;
        int b = pos;
        for (int depth = 0; b < last; ++b) {
            if (at(b).op("(")) {
                ++depth;
            } else if (at(b).op(")")) {
                --depth;
            } else if (depth == 0 && (at(b).is("inner") || at(b).is("left") || at(b).is("right") || at(b).is("join"))) {
                break;
            }
        }
        pos = b;
        while (a < b && at(a).op("(") && closing(m_t, a) == b - 1) {
            ++a;
            --b;
        }
        // One equality per join: the grid's join is one pair of fields.
        if (b - a != 7 || !at(a + 1).op(".") || !at(a + 3).op("=") || !at(a + 5).op(".")) {
            return fail(QStringLiteral("The design grid joins on one pair of fields (a.x = b.y); keep this as SQL."));
        }
        QueryJoin join{at(a).text, at(a + 2).text, at(a + 4).text, at(a + 6).text, kind};
        // "A LEFT JOIN B" keeps every row of A: when ON names B first, that
        // is every row of the join's right table.
        if (kind != QLatin1String("inner") && right.contains(join.leftTable.toLower())) {
            join.kind = kind == QLatin1String("left") ? QStringLiteral("right") : QStringLiteral("left");
        }
        d.joins.append(join);
        refs->append(right);
    }
    return true;
}

bool SqlParser::fromClause(Range r)
{
    for (const Range &part : splitCommas(r)) {
        int pos = part.first;
        QStringList refs;
        if (!joinExpression(pos, part.last, &refs)) {
            return false;
        }
        if (pos != part.last) {
            return fail(QStringLiteral("Unexpected \"%1\" in FROM.").arg(at(pos).text));
        }
    }
    return d.tables.isEmpty() ? fail(QStringLiteral("The query needs a table in FROM.")) : true;
}

// The clauses of a SELECT-shaped statement from `first`: each starts at
// its keyword and runs to the next one (at parenthesis depth 0).
bool SqlParser::clauses(int first, QHash<QString, Range> *out)
{
    static const char *const keys[] = {"select", "into", "from", "where", "group", "having", "order", "pivot", "set"};
    QString current;
    int start = first;
    int depth = 0;
    for (int i = first; i <= m_last; ++i) {
        const bool end = i == m_last;
        if (!end && m_t.at(i).op("(")) {
            ++depth;
        } else if (!end && m_t.at(i).op(")")) {
            --depth;
        }
        if (!end && (depth != 0 || m_t.at(i).kind != Token::Word)) {
            continue;
        }
        if (!end && (m_t.at(i).is("union") || m_t.at(i).is("exists"))) {
            return fail(QStringLiteral("%1 is SQL the design grid cannot show; keep this as SQL.").arg(m_t.at(i).text.toUpper()));
        }
        const auto key = std::find_if(std::begin(keys), std::end(keys), [&](const char *k) { return !end && m_t.at(i).is(k); });
        if (!end && key == std::end(keys)) {
            continue;
        }
        if (!current.isEmpty()) {
            if (out->contains(current)) {
                return fail(QStringLiteral("%1 appears twice.").arg(current.toUpper()));
            }
            out->insert(current, {start, i});
        }
        if (end) {
            break;
        }
        current = QString::fromLatin1(*key);
        start = i + 1;
        if (current == QLatin1String("group") || current == QLatin1String("order")) {
            if (!m_t.at(i + 1).is("by")) {
                return fail(QStringLiteral("%1 needs BY.").arg(m_t.at(i).text.toUpper()));
            }
            ++start;
        }
    }
    return true;
}

AccessSqlParse SqlParser::run()
{
    AccessSqlParse out;
    m_t = tokenize(m_src, &m_error);
    if (!m_error.isEmpty()) {
        out.error = m_error;
        return out;
    }
    m_last = int(m_t.size()) - 1; // the End token
    while (m_last > 0 && m_t.at(m_last - 1).op(";")) {
        --m_last;
    }
    if (m_t.first().is("parameters") && !parameters()) {
        out.error = m_error;
        return out;
    }
    for (int i = m_first; i < m_last; ++i) {
        if (m_t.at(i).op(";")) {
            out.error = QStringLiteral("Write one statement at a time.");
            return out;
        }
    }
    bool ok = false;
    const Token &head = at(m_first);
    if (head.is("select")) {
        ok = selectStatement(m_first);
    } else if (head.is("transform")) {
        ok = crosstabStatement();
    } else if (head.is("update")) {
        ok = updateStatement();
    } else if (head.is("delete")) {
        ok = deleteStatement();
    } else if (head.is("insert")) {
        ok = appendStatement();
    } else {
        ok = fail(QStringLiteral("A query starts with SELECT, TRANSFORM, UPDATE, DELETE or INSERT INTO."));
    }
    if (!ok) {
        out.error = m_error;
        return out;
    }
    const CompiledQuery compiled = compileQuery(m_file, d);
    if (!compiled.ok) {
        out.error = compiled.error;
        return out;
    }
    out.ok = true;
    out.design = d;
    return out;
}

} // namespace QindaOffice::Base::AccessSql

namespace QindaOffice::Base {

AccessSqlParse parseAccessSql(const BaseFile &file, const QString &sql)
{
    AccessSql::SqlParser parser(file, sql);
    return parser.run();
}

} // namespace QindaOffice::Base
