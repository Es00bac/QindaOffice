// SPDX-License-Identifier: GPL-3.0-or-later
// The SQL view's union queries (M86, D-094): a union design printed as
// Access shows it — PARAMETERS once, the parts joined by UNION or UNION
// ALL, the union's ORDER BY after the last part — and read back: each part
// parsed as its own select (its own grid), the ORDER BY after the last part
// taken as the union's order (Access's rule), named or numbered.
#include "base_access_sql_parser.h"

namespace QindaOffice::Base::AccessSql {

QString unionText(const QueryDesign &design)
{
    QStringList lines;
    if (!design.parameters.isEmpty()) {
        QStringList declared;
        for (const QueryParameter &p : design.parameters) {
            declared.append(QStringLiteral("[%1] %2").arg(p.prompt, parameterType(p.type)));
        }
        lines.append(QStringLiteral("PARAMETERS %1;").arg(declared.join(QStringLiteral(", "))));
    }
    for (int i = 0; i < design.parts.size(); ++i) {
        QueryDesign part = design.parts.at(i);
        part.parameters.clear(); // declared once, above
        QString text = accessSql(part);
        if (text.endsWith(QLatin1Char(';'))) {
            text.chop(1);
        }
        if (i > 0) {
            lines.append(design.unionAll.value(i - 1) ? QStringLiteral("UNION ALL") : QStringLiteral("UNION"));
        }
        lines.append(text);
    }
    if (!design.unionOrder.isEmpty()) {
        QStringList keys;
        for (const UnionSort &o : design.unionOrder) {
            keys.append(ident(o.column) + (o.descending ? QStringLiteral(" DESC") : QString()));
        }
        lines.append(QStringLiteral("ORDER BY ") + keys.join(QStringLiteral(", ")));
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char(';');
}

QList<int> SqlParser::topLevelUnions() const
{
    QList<int> out;
    int depth = 0;
    for (int i = m_first; i < m_last; ++i) {
        if (m_t.at(i).op("(")) {
            ++depth;
        } else if (m_t.at(i).op(")")) {
            --depth;
        } else if (depth == 0 && m_t.at(i).is("union")) {
            out.append(i);
        }
    }
    return out;
}

bool SqlParser::unionStatement(const QList<int> &unions)
{
    d.kind = QueryKind::Union;
    QList<Range> segments;
    int start = m_first;
    for (const int u : unions) {
        segments.append({start, u});
        const bool all = at(u + 1).is("all");
        d.unionAll.append(all);
        start = u + (all ? 2 : 1);
    }
    Range last{start, m_last};
    Range order;
    for (int i = last.first, depth = 0; i < last.last; ++i) {
        if (at(i).op("(")) {
            ++depth;
        } else if (at(i).op(")")) {
            --depth;
        } else if (depth == 0 && at(i).is("order") && at(i + 1).is("by")) {
            order = {i + 2, last.last};
            last.last = i;
            break;
        }
    }
    segments.append(last);
    for (int i = 0; i < segments.size(); ++i) {
        const Range segment = segments.at(i);
        if (segment.empty() || !at(segment.first).is("select")) {
            return fail(QStringLiteral("Part %1 of the union is not a SELECT.").arg(i + 1));
        }
        const AccessSqlParse part = parseAccessSql(m_file, text(segment));
        if (!part.ok) {
            return fail(QStringLiteral("Part %1 of the union: %2").arg(i + 1).arg(part.error));
        }
        if (part.design.kind != QueryKind::Select) {
            return fail(QStringLiteral("Part %1 of the union makes a table; a union joins SELECT queries.").arg(i + 1));
        }
        if (part.design.top > 0) {
            return fail(QStringLiteral("Part %1 of the union returns Top values; sort the union as a whole, at its end.")
                            .arg(i + 1));
        }
        d.parts.append(part.design);
    }
    if (order.empty()) {
        return true;
    }
    // A numbered key names the first part's column at that place.
    QStringList names;
    const CompiledQuery first = compileQuery(m_file, d.parts.first());
    if (first.ok) {
        QString error;
        const Statement statement = m_file.db().prepare(first.sql, &error);
        for (int c = 0; c < statement.columnCount(); ++c) {
            names.append(statement.columnName(c));
        }
    }
    for (Range item : splitCommas(order)) {
        bool descending = false;
        if (item.last - item.first > 1 && (at(item.last - 1).is("desc") || at(item.last - 1).is("asc"))) {
            descending = at(item.last - 1).is("desc");
            --item.last;
        }
        const int length = item.last - item.first;
        QString column;
        if (length == 1 && at(item.first).kind == Token::Number) {
            const int n = at(item.first).text.toInt();
            if (n < 1 || n > names.size()) {
                return fail(QStringLiteral("ORDER BY %1 is not a column of the union.").arg(n));
            }
            column = names.at(n - 1);
        } else if (nameToken(at(item.last - 1)) && (length == 1 || (length == 3 && at(item.first + 1).op(".")))) {
            column = at(item.last - 1).text;
        } else {
            return fail(QStringLiteral("A union is sorted by the names or numbers of its columns."));
        }
        d.unionOrder.append({column, descending});
    }
    return true;
}

} // namespace QindaOffice::Base::AccessSql
