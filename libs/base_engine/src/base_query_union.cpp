// SPDX-License-Identifier: GPL-3.0-or-later
// Union queries (M86, D-094): Access's SELECT … UNION [ALL] SELECT …,
// each part a select design of its own (its own grid), the whole sorted by
// the union's own order (Access's ORDER BY after the last SELECT).
//
// AGENT-NOTE: SQLite allows ORDER BY and LIMIT only on a compound select
// as a whole, and Access SQL sorts a union only after its last SELECT — so
// a part takes no sort and no Top value; the union's own order names
// output columns of its first part (Access's rule) and compiles to their
// positions.
#include "base_query_p.h"

namespace QindaOffice::Base::detail {

CompiledQuery compileUnion(const BaseFile &file, const QueryDesign &design, const QVariantMap *values)
{
    CompiledQuery out;
    const auto fail = [&out](const QString &error) {
        out.ok = false;
        out.error = error;
        return out;
    };
    if (design.parts.size() < 2) {
        return fail(QStringLiteral("A union query joins two or more select queries."));
    }
    QStringList sql;
    QStringList firstNames;
    for (int i = 0; i < design.parts.size(); ++i) {
        QueryDesign part = design.parts.at(i);
        if (part.kind != QueryKind::Select) {
            return fail(QStringLiteral("Part %1 of the union is not a select query.").arg(i + 1));
        }
        if (part.top > 0 || std::any_of(part.columns.cbegin(), part.columns.cend(),
                                        [](const QueryColumn &col) { return col.sort != 0; })) {
            return fail(QStringLiteral("Part %1 of the union sorts or returns Top values; sort the union as a whole "
                                       "instead.")
                            .arg(i + 1));
        }
        // The union's declared parameters are every part's.
        for (const QueryParameter &p : design.parameters) {
            if (std::none_of(part.parameters.cbegin(), part.parameters.cend(), [&](const QueryParameter &x) {
                    return x.prompt.compare(p.prompt, Qt::CaseInsensitive) == 0;
                })) {
                part.parameters.append(p);
            }
        }
        const CompiledQuery c = values != nullptr ? compileQuery(file, part, *values) : compileQuery(file, part);
        if (!c.ok) {
            return fail(QStringLiteral("Part %1 of the union: %2").arg(i + 1).arg(c.error));
        }
        QString error;
        const Statement statement = file.db().prepare(c.sql, &error);
        if (!statement.isValid()) {
            return fail(QStringLiteral("Part %1 of the union: %2").arg(i + 1).arg(error));
        }
        QStringList names;
        for (int col = 0; col < statement.columnCount(); ++col) {
            names.append(statement.columnName(col));
        }
        if (i == 0) {
            firstNames = names;
            out.columns = c.columns;
        } else if (names.size() != firstNames.size()) {
            return fail(QStringLiteral("Part %1 of the union shows %2 column(s) and part 1 shows %3: every part shows "
                                       "as many.")
                            .arg(i + 1)
                            .arg(names.size())
                            .arg(firstNames.size()));
        }
        for (const QueryParameter &p : c.parameters) {
            if (std::none_of(out.parameters.cbegin(), out.parameters.cend(),
                             [&](const QueryParameter &x) { return x.name == p.name; })) {
                out.parameters.append(p);
            }
        }
        sql.append(c.sql);
    }
    QString text = sql.first();
    for (int i = 1; i < sql.size(); ++i) {
        text += design.unionAll.value(i - 1) ? QStringLiteral("\nUNION ALL\n") : QStringLiteral("\nUNION\n");
        text += sql.at(i);
    }
    QStringList order;
    for (const UnionSort &key : design.unionOrder) {
        const qsizetype at = firstNames.indexOf(key.column);
        qsizetype found = at;
        for (qsizetype n = 0; found < 0 && n < firstNames.size(); ++n) {
            if (firstNames.at(n).compare(key.column, Qt::CaseInsensitive) == 0) {
                found = n;
            }
        }
        if (found < 0) {
            return fail(QStringLiteral("The union is sorted by %1, which its first part does not show.").arg(key.column));
        }
        order.append(QString::number(found + 1) + (key.descending ? QStringLiteral(" DESC") : QString()));
    }
    if (!order.isEmpty()) {
        text += QStringLiteral("\nORDER BY ") + order.join(QStringLiteral(", "));
    }
    out.sql = text;
    out.ok = true;
    return out;
}

} // namespace QindaOffice::Base::detail
