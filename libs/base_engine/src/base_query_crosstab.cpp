// SPDX-License-Identifier: GPL-3.0-or-later
// Crosstab queries (M80, D-089 §3): Access's TRANSFORM … PIVOT as plain
// SQLite — one output column per column-heading value, each the value's
// total over the rows holding that heading:
//
//   SELECT <row headings>, [<row summaries>,]
//          CASE WHEN max(<heading> = 'Cash') THEN sum(CASE WHEN <heading> = 'Cash' THEN <value> END) END AS "Cash", …
//   FROM … WHERE … GROUP BY <row headings> [HAVING …] ORDER BY <row headings>
//
// AGENT-NOTE: a cell with no rows is empty (Null) for every total, Count
// included — what Access shows — hence the CASE WHEN max(…) guard. The
// headings are the distinct values the rows hold (sorted), unless the
// design fixes them (Column Headings); a Null heading is the column "<>".
#include "base_query_p.h"

#include <QSet>

namespace QindaOffice::Base::detail {

namespace {

constexpr int kMaxHeadings = 254; // Access's limit for a crosstab's columns

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

QString uniqueName(const QString &wanted, QSet<QString> *used)
{
    QString name = wanted.isEmpty() ? QStringLiteral("(blank)") : wanted;
    for (int k = 2; used->contains(name.toLower()); ++k) {
        name = QStringLiteral("%1 (%2)").arg(wanted).arg(k);
    }
    used->insert(name.toLower());
    return name;
}

} // namespace

CompiledQuery compileCrosstab(const BaseFile &file, const QueryDesign &design, const QVariantMap *values)
{
    CompiledQuery out;
    const auto fail = [&out](const QString &error) {
        out.ok = false;
        out.error = error;
        return out;
    };
    QueryCore core;
    if (const QString e = buildQueryCore(file, design, &out.parameters, &core); !e.isEmpty()) {
        return fail(e);
    }
    QList<int> rows;
    int column = -1;
    int value = -1;
    for (int i = 0; i < design.columns.size(); ++i) {
        const QueryColumn &c = design.columns.at(i);
        const QueryCol &col = core.cols.at(i);
        if (col.name == QLatin1String("*")) {
            return fail(QStringLiteral("A crosstab cannot use *; add the fields one by one."));
        }
        if (c.crosstab == QLatin1String("row")) {
            rows.append(i);
        } else if (c.crosstab == QLatin1String("column")) {
            if (column >= 0) {
                return fail(QStringLiteral("A crosstab has exactly one Column Heading."));
            }
            if (col.aggregate) {
                return fail(QStringLiteral("The Column Heading is grouped by: set its Total to Group By."));
            }
            column = i;
        } else if (c.crosstab == QLatin1String("value")) {
            if (value >= 0) {
                return fail(QStringLiteral("A crosstab has exactly one Value."));
            }
            if (col.function.isEmpty()) {
                return fail(QStringLiteral("The Value is a total: set its Total to Sum, Avg, Count, Min or Max."));
            }
            value = i;
        } else if (c.total != TotalKind::Where) {
            return fail(QStringLiteral("Column %1: choose Row Heading, Column Heading or Value — or Where to filter.")
                            .arg(i + 1));
        }
    }
    if (rows.isEmpty()) {
        return fail(QStringLiteral("A crosstab needs at least one Row Heading."));
    }
    if (column < 0) {
        return fail(QStringLiteral("A crosstab needs a Column Heading."));
    }
    if (value < 0) {
        return fail(QStringLiteral("A crosstab needs a Value, totalled with Sum, Avg, Count, Min or Max."));
    }
    const QueryCol &heading = core.cols.at(column);
    const FieldType headingType = heading.type.value_or(FieldType::Text);
    const QueryCol &cell = core.cols.at(value);

    // The headings: fixed by the design, or the distinct values of the rows.
    QList<QVariant> headingValues;
    QStringList headingNames;
    if (!design.columnHeadings.isEmpty()) {
        for (const QString &text : design.columnHeadings) {
            const Parsed parsed = parseInput(headingType, text, core.currency);
            if (!parsed.ok) {
                return fail(QStringLiteral("Column heading \"%1\" %2.").arg(text, parsed.error));
            }
            headingValues.append(parsed.value);
            headingNames.append(text.trimmed());
        }
    } else {
        const bool bound = values != nullptr && !out.parameters.isEmpty();
        // Without parameter values, the headings of every row the criteria
        // allow when they ask for nothing — or of every row when they do.
        const QString where = out.parameters.isEmpty() || bound ? core.where() : QString();
        QString error;
        Statement statement = file.db().prepare(
            QStringLiteral("SELECT DISTINCT %1 FROM %2%3 ORDER BY 1").arg(heading.sql, core.from, where), &error);
        if (!statement.isValid()) {
            return fail(error);
        }
        if (bound) {
            if (const Result r = bindParameters(file, statement, out.parameters, *values); !r.ok) {
                return fail(r.error);
            }
        }
        while (statement.step() == Statement::Step::Row) {
            if (headingValues.size() >= kMaxHeadings) {
                return fail(QStringLiteral("The Column Heading has more than %1 values; add criteria or fix the column "
                                           "headings.")
                                .arg(kMaxHeadings));
            }
            const QVariant v = statement.value(0);
            headingValues.append(v);
            headingNames.append(v.isValid() ? displayText(headingType, v, core.currency) : QStringLiteral("<>"));
        }
    }

    QSet<QString> used;
    QStringList select;
    QStringList group;
    QStringList order;
    for (const int i : std::as_const(rows)) {
        const QueryCol &col = core.cols.at(i);
        const QString name = uniqueName(col.name, &used);
        QString sql = col.sql;
        if (col.type == FieldType::Currency && !col.aggregate && !design.columns.at(i).expression.isEmpty()) {
            sql = QStringLiteral("CAST(round(%1) AS INTEGER)").arg(sql);
        }
        select.append(QStringLiteral("%1 AS %2").arg(sql, q(name)));
        out.columns.append({name, col.type.value_or(FieldType::Text), col.type.has_value(), col.sourceTable,
                            col.sourceField});
        if (!col.aggregate) {
            group.append(col.sql);
            order.append(col.sql + (design.columns.at(i).sort < 0 ? QStringLiteral(" DESC") : QStringLiteral(" ASC")));
        }
    }
    if (group.isEmpty()) {
        return fail(QStringLiteral("A crosstab needs a Row Heading grouped by (not only totals)."));
    }
    const std::optional<FieldType> cellType = cell.type;
    for (int h = 0; h < headingValues.size(); ++h) {
        const QVariant &v = headingValues.at(h);
        const QString match = v.isValid() ? QStringLiteral("%1 = %2").arg(heading.sql, sqlLiteral(v))
                                           : QStringLiteral("%1 IS NULL").arg(heading.sql);
        const QString name = uniqueName(headingNames.at(h), &used);
        select.append(QStringLiteral("CASE WHEN max(%1) THEN %2(CASE WHEN %1 THEN %3 END) END AS %4")
                          .arg(match, cell.function, cell.inner, q(name)));
        out.columns.append({name, cellType.value_or(FieldType::Number), cellType.has_value(), QString(), QString()});
        out.headings.append(name);
    }
    out.sql = QStringLiteral("SELECT %1\nFROM %2%3\nGROUP BY %4%5\nORDER BY %6")
                  .arg(select.join(QStringLiteral(", ")), core.from, core.where(), group.join(QStringLiteral(", ")),
                       core.having(), order.join(QStringLiteral(", ")));
    out.crosstab = true;
    out.ok = true;
    return out;
}

} // namespace QindaOffice::Base::detail
