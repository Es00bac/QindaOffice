// SPDX-License-Identifier: GPL-3.0-or-later
// The SQL view's printer: a query design as the Access SQL statement Access
// shows for it (base_access_sql.h). It mirrors the compiler's choices
// exactly — which criteria go to WHERE and which to HAVING, the join walk,
// which columns group — so the parser can give the same query back.
#include "base_access_sql_p.h"
#include "base_query_p.h"

namespace QindaOffice::Base {

using namespace AccessSql;

namespace {

QString aggregateWord(TotalKind total)
{
    switch (total) {
    case TotalKind::Sum: return QStringLiteral("Sum");
    case TotalKind::Average: return QStringLiteral("Avg");
    case TotalKind::Count: return QStringLiteral("Count");
    case TotalKind::Min: return QStringLiteral("Min");
    case TotalKind::Max: return QStringLiteral("Max");
    default: return {};
    }
}

QString quoteText(QString text)
{
    text.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + text + QLatin1Char('"');
}

QString fieldText(const QueryColumn &c)
{
    if (c.field == QLatin1String("*")) {
        return c.table.isEmpty() ? QStringLiteral("*") : ident(c.table) + QStringLiteral(".*");
    }
    return c.table.isEmpty() ? ident(c.field) : ident(c.table) + QLatin1Char('.') + ident(c.field);
}

QString valueText(const QueryColumn &c)
{
    return c.expression.trimmed().isEmpty() ? fieldText(c) : c.expression.trimmed();
}

// An expression that totals (Sum(…), Count(*), …): the compiler sends its
// criteria to HAVING and leaves it out of GROUP BY.
bool expressionTotals(const QString &text)
{
    QString error;
    const QList<Token> t = tokenize(text, &error);
    for (int i = 0; i + 1 < t.size(); ++i) {
        if (t.at(i).kind == Token::Word && !accessAggregate(t.at(i).text).isEmpty() && t.at(i + 1).op("(")) {
            return true;
        }
    }
    return false;
}

struct Printer {
    const QueryDesign &d;
    bool totals = false;
    bool crosstab = false;

    [[nodiscard]] bool aggregateTotal(const QueryColumn &c) const { return totals && !aggregateWord(c.total).isEmpty(); }
    [[nodiscard]] bool aggregates(const QueryColumn &c) const
    {
        return aggregateTotal(c) || (totals && !c.expression.trimmed().isEmpty() && expressionTotals(c.expression));
    }
    [[nodiscard]] QString subject(const QueryColumn &c) const
    {
        return aggregateTotal(c) ? QStringLiteral("%1(%2)").arg(aggregateWord(c.total), valueText(c)) : valueText(c);
    }
    [[nodiscard]] bool toHaving(const QueryColumn &c) const
    {
        return totals && c.total != TotalKind::Where && (aggregates(c) || c.total == TotalKind::GroupBy)
               && !(crosstab && c.crosstab == QLatin1String("column"));
    }
    [[nodiscard]] QString item(const QueryColumn &c) const
    {
        const QString alias = c.alias.trimmed();
        return subject(c) + (alias.isEmpty() || c.field == QLatin1String("*") ? QString()
                                                                              : QStringLiteral(" AS ") + ident(alias));
    }
    // One criteria cell as predicates on its column ("> 5 And < 10" →
    // "(x > 5 And x < 10)"); a value alone compares for equality.
    [[nodiscard]] QString predicate(const QString &subjectText, const QString &criteria) const;
    [[nodiscard]] QString conditions(bool having) const;
    [[nodiscard]] QString from() const;
};

QString atomPredicate(const QString &subject, const QString &src, const QList<Token> &t, int first, int last)
{
    const Token &f = t.at(first);
    const QString text = textOf(src, t, first, last);
    static const QStringList comparisons{QStringLiteral("="), QStringLiteral("<>"), QStringLiteral("<"),
                                         QStringLiteral(">"), QStringLiteral("<="), QStringLiteral(">=")};
    if ((f.kind == Token::Op && comparisons.contains(f.text)) || f.is("like") || f.is("between") || f.is("in")
        || f.is("is")) {
        return subject + QLatin1Char(' ') + text;
    }
    if (f.is("not")) {
        if (first + 1 < last && (t.at(first + 1).is("like") || t.at(first + 1).is("in") || t.at(first + 1).is("between"))) {
            return subject + QLatin1Char(' ') + text;
        }
        if (first + 1 < last) {
            return QStringLiteral("Not ") + atomPredicate(subject, src, t, first + 1, last);
        }
    }
    if (last - first == 1 && f.is("null")) {
        return subject + QStringLiteral(" Is Null");
    }
    return subject + QStringLiteral(" = ") + text;
}

QString Printer::predicate(const QString &subjectText, const QString &criteria) const
{
    QString error;
    const QList<Token> t = tokenize(criteria, &error);
    if (!error.isEmpty() || t.size() < 2) {
        return subjectText + QStringLiteral(" = ") + criteria.trimmed();
    }
    // Several plain words are one text, as the criteria compiler reads them.
    static const QStringList keywords{QStringLiteral("not"), QStringLiteral("like"), QStringLiteral("between"),
                                      QStringLiteral("in"),  QStringLiteral("is"),   QStringLiteral("null"),
                                      QStringLiteral("and"), QStringLiteral("or"),   QStringLiteral("true"),
                                      QStringLiteral("false"), QStringLiteral("yes"), QStringLiteral("no")};
    const bool wordsOnly = t.size() > 2 && std::all_of(t.cbegin(), t.cend() - 1, [](const Token &x) {
        return x.kind == Token::Word && !keywords.contains(x.text.toLower());
    });
    if (wordsOnly) {
        return subjectText + QStringLiteral(" = ") + quoteText(criteria.trimmed());
    }
    QStringList parts;
    int start = 0;
    int depth = 0;
    bool between = false;
    const int end = int(t.size()) - 1;
    for (int i = 0; i <= end; ++i) {
        const Token &x = t.at(i);
        if (x.op("(")) {
            ++depth;
        } else if (x.op(")")) {
            --depth;
        } else if (depth == 0 && x.is("between")) {
            between = true;
        } else if (depth == 0 && x.is("and") && between) {
            between = false;
            continue;
        }
        const bool connector = depth == 0 && (x.is("and") || x.is("or"));
        if (i == end || connector) {
            if (i > start) {
                parts.append(atomPredicate(subjectText, criteria, t, start, i));
            }
            if (connector) {
                parts.append(x.is("and") ? QStringLiteral("And") : QStringLiteral("Or"));
            }
            start = i + 1;
        }
    }
    const QString joined = parts.join(QLatin1Char(' '));
    return parts.size() > 1 ? QLatin1Char('(') + joined + QLatin1Char(')') : joined;
}

QString Printer::conditions(bool having) const
{
    int rows = 0;
    for (const QueryColumn &c : d.columns) {
        rows = std::max(rows, int(c.orCriteria.size()) + 1);
    }
    QStringList rowTexts;
    for (int r = 0; r < rows; ++r) {
        QStringList parts;
        for (const QueryColumn &c : d.columns) {
            const QString text = r == 0 ? c.criteria : c.orCriteria.value(r - 1);
            if (text.trimmed().isEmpty() || toHaving(c) != having) {
                continue;
            }
            parts.append(predicate(subject(c), text));
        }
        if (!parts.isEmpty()) {
            rowTexts.append(parts.join(QStringLiteral(" AND ")));
        }
    }
    if (rowTexts.size() <= 1) {
        return rowTexts.value(0);
    }
    return QLatin1Char('(') + rowTexts.join(QStringLiteral(") OR (")) + QLatin1Char(')');
}

QString Printer::from() const
{
    if (d.tables.isEmpty()) {
        return {};
    }
    const auto tableText = [](const QueryTable &t) {
        return t.alias.isEmpty() ? ident(t.name) : ident(t.name) + QStringLiteral(" AS ") + ident(t.alias);
    };
    QString text = tableText(d.tables.first());
    QStringList placed{d.tables.first().ref().toLower()};
    QList<QueryJoin> pending = d.joins;
    QStringList crossed;
    while (placed.size() + crossed.size() < d.tables.size()) {
        bool progressed = false;
        for (int j = 0; j < pending.size(); ++j) {
            const QueryJoin &join = pending.at(j);
            const bool leftIn = placed.contains(join.leftTable.toLower());
            if (leftIn == placed.contains(join.rightTable.toLower())) {
                continue;
            }
            const QString add = leftIn ? join.rightTable : join.leftTable;
            const auto t = std::find_if(d.tables.cbegin(), d.tables.cend(),
                                        [&](const QueryTable &x) { return x.ref().compare(add, Qt::CaseInsensitive) == 0; });
            if (t == d.tables.cend()) {
                pending.removeAt(j);
                progressed = true;
                break;
            }
            // The same flip the compiler makes when the join brings in its left table.
            QString kind = join.kind == QLatin1String("left")    ? QStringLiteral("LEFT JOIN")
                           : join.kind == QLatin1String("right") ? QStringLiteral("RIGHT JOIN")
                                                                 : QStringLiteral("INNER JOIN");
            if (!leftIn && kind != QLatin1String("INNER JOIN")) {
                kind = kind == QLatin1String("LEFT JOIN") ? QStringLiteral("RIGHT JOIN") : QStringLiteral("LEFT JOIN");
            }
            if (text.contains(QLatin1String(" JOIN "))) {
                text = QLatin1Char('(') + text + QLatin1Char(')');
            }
            text += QStringLiteral(" %1 %2 ON %3.%4 = %5.%6")
                        .arg(kind, tableText(*t), ident(join.leftTable), ident(join.leftField), ident(join.rightTable),
                             ident(join.rightField));
            placed.append(add.toLower());
            pending.removeAt(j);
            progressed = true;
            break;
        }
        if (!progressed) {
            for (const QueryTable &t : d.tables) {
                if (!placed.contains(t.ref().toLower()) && !crossed.contains(t.ref().toLower())) {
                    crossed.append(t.ref().toLower());
                    text += QStringLiteral(", ") + tableText(t);
                }
            }
        }
    }
    return text;
}

} // namespace

QString accessSql(const QueryDesign &design)
{
    if (design.kind == QueryKind::Sql) {
        return design.sql;
    }
    if (design.kind == QueryKind::Union) {
        return unionText(design); // M86
    }
    if (design.tables.isEmpty()) {
        return {}; // nothing to show until the query reads a table
    }
    Printer p{design, detail::totalsOn(design), design.kind == QueryKind::Crosstab};
    QStringList lines;
    if (!design.parameters.isEmpty()) {
        QStringList declared;
        for (const QueryParameter &prm : design.parameters) {
            declared.append(QStringLiteral("[%1] %2").arg(prm.prompt, parameterType(prm.type)));
        }
        lines.append(QStringLiteral("PARAMETERS %1;").arg(declared.join(QStringLiteral(", "))));
    }
    const QString where = p.conditions(false);
    const QString having = p.conditions(true);
    QStringList group;
    QStringList order;
    for (const QueryColumn &c : design.columns) {
        if (p.totals && c.total == TotalKind::GroupBy && !p.aggregates(c)
            && (!p.crosstab || c.crosstab == QLatin1String("row"))) {
            group.append(valueText(c));
        }
        if (c.sort != 0 && (!p.crosstab || c.crosstab == QLatin1String("row"))) {
            order.append(p.subject(c) + (c.sort < 0 ? QStringLiteral(" DESC") : QString()));
        }
    }
    const auto tail = [&](bool withGroups) {
        lines.append(QStringLiteral("FROM ") + p.from());
        if (!where.isEmpty()) {
            lines.append(QStringLiteral("WHERE ") + where);
        }
        if (withGroups && !group.isEmpty()) {
            lines.append(QStringLiteral("GROUP BY ") + group.join(QStringLiteral(", ")));
        }
        if (withGroups && !having.isEmpty()) {
            lines.append(QStringLiteral("HAVING ") + having);
        }
        if (withGroups && !order.isEmpty()) {
            lines.append(QStringLiteral("ORDER BY ") + order.join(QStringLiteral(", ")));
        }
    };
    QStringList items;
    switch (design.kind) {
    case QueryKind::Update: {
        QStringList sets;
        for (const QueryColumn &c : design.columns) {
            if (!c.updateTo.trimmed().isEmpty() && c.expression.trimmed().isEmpty()) {
                sets.append(fieldText(c) + QStringLiteral(" = ") + c.updateTo.trimmed());
            }
        }
        lines.append(QStringLiteral("UPDATE %1").arg(p.from()));
        lines.append(QStringLiteral("SET ") + sets.join(QStringLiteral(", ")));
        if (!where.isEmpty()) {
            lines.append(QStringLiteral("WHERE ") + where);
        }
        break;
    }
    case QueryKind::Delete:
        lines.append(QStringLiteral("DELETE"));
        tail(false);
        break;
    case QueryKind::Append: {
        QStringList into;
        for (const QueryColumn &c : design.columns) {
            if (!c.appendTo.trimmed().isEmpty()) {
                into.append(ident(c.appendTo.trimmed()));
                items.append(valueText(c));
            }
        }
        lines.append(QStringLiteral("INSERT INTO %1 (%2)").arg(ident(design.appendTable), into.join(QStringLiteral(", "))));
        lines.append(QStringLiteral("SELECT ") + items.join(QStringLiteral(", ")));
        tail(false);
        break;
    }
    case QueryKind::Crosstab: {
        QString pivot;
        for (const QueryColumn &c : design.columns) {
            if (c.crosstab == QLatin1String("value")) {
                lines.append(QStringLiteral("TRANSFORM ") + p.item(c));
            } else if (c.crosstab == QLatin1String("row")) {
                items.append(p.item(c));
            } else if (c.crosstab == QLatin1String("column")) {
                pivot = valueText(c);
            }
        }
        lines.append(QStringLiteral("SELECT ") + items.join(QStringLiteral(", ")));
        tail(true);
        QStringList headings;
        for (const QString &h : design.columnHeadings) {
            headings.append(quoteText(h));
        }
        lines.append(QStringLiteral("PIVOT ") + pivot
                     + (headings.isEmpty() ? QString() : QStringLiteral(" IN (%1)").arg(headings.join(QStringLiteral(", ")))));
        break;
    }
    default: {
        for (const QueryColumn &c : design.columns) {
            if (c.show && !(p.totals && c.total == TotalKind::Where)) {
                items.append(p.item(c));
            }
        }
        QString head = QStringLiteral("SELECT ");
        if (design.distinct) {
            head += QStringLiteral("DISTINCT ");
        }
        if (design.top > 0) {
            head += QStringLiteral("TOP %1 %2").arg(design.top).arg(design.topPercent ? QStringLiteral("PERCENT ") : QString());
        }
        lines.append(head + items.join(QStringLiteral(", ")));
        if (design.kind == QueryKind::MakeTable) {
            lines.append(QStringLiteral("INTO ") + ident(design.makeTable));
        }
        tail(true);
        break;
    }
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char(';');
}

} // namespace QindaOffice::Base
