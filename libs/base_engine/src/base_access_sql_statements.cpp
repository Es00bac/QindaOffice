// SPDX-License-Identifier: GPL-3.0-or-later
// The SQL view's parser, part three: each statement shape the design grid
// holds — SELECT (and SELECT … INTO, a make-table), TRANSFORM … PIVOT,
// UPDATE … SET, DELETE … FROM and INSERT INTO … SELECT.
#include "base_access_sql_parser.h"

namespace QindaOffice::Base::AccessSql {

bool SqlParser::selectStatement(int first)
{
    QHash<QString, Range> cl;
    if (!clauses(first, &cl)) {
        return false;
    }
    if (cl.contains(QStringLiteral("pivot")) || cl.contains(QStringLiteral("set"))) {
        return fail(QStringLiteral("PIVOT belongs to TRANSFORM, SET to UPDATE."));
    }
    if (!cl.contains(QStringLiteral("from"))) {
        return fail(QStringLiteral("The query needs FROM and a table."));
    }
    const Range sel = cl.value(QStringLiteral("select"));
    int pos = sel.first;
    if (at(pos).is("distinct")) {
        d.distinct = true;
        ++pos;
    } else if (at(pos).is("distinctrow")) {
        ++pos; // Access's unique records: what a plain select of keyed tables gives
    }
    if (at(pos).is("top")) {
        if (at(pos + 1).kind != Token::Number) {
            return fail(QStringLiteral("TOP takes a number."));
        }
        d.top = at(pos + 1).text.toInt();
        pos += 2;
        if (at(pos).is("percent")) {
            d.topPercent = true;
            ++pos;
        }
    }
    d.kind = QueryKind::Select;
    if (cl.contains(QStringLiteral("into"))) {
        const Range into = cl.value(QStringLiteral("into"));
        if (into.last - into.first != 1 || !nameToken(at(into.first))) {
            return fail(QStringLiteral("SELECT … INTO names one new table."));
        }
        d.kind = QueryKind::MakeTable;
        d.makeTable = at(into.first).text;
    }
    if (!fromClause(cl.value(QStringLiteral("from")))) {
        return false;
    }
    const Range items{pos, sel.last};
    bool aggregate = false;
    for (const Range &item : splitCommas(items)) {
        aggregate = aggregate || containsAggregate(m_t, item.first, item.last);
    }
    d.totals = aggregate || cl.contains(QStringLiteral("group")) || cl.contains(QStringLiteral("having"));
    if (!selectList(items, totals(), QString())) {
        return false;
    }
    if (cl.contains(QStringLiteral("group")) && !groupBy(cl.value(QStringLiteral("group")))) {
        return false;
    }
    if (cl.contains(QStringLiteral("where")) && !criteria(cl.value(QStringLiteral("where")), false)) {
        return false;
    }
    if (cl.contains(QStringLiteral("having")) && !criteria(cl.value(QStringLiteral("having")), true)) {
        return false;
    }
    return !cl.contains(QStringLiteral("order")) || orderBy(cl.value(QStringLiteral("order")));
}

// TRANSFORM <total> SELECT <row headings> FROM … [WHERE] GROUP BY <rows>
// [HAVING] [ORDER BY] PIVOT <column heading> [IN (…)]
bool SqlParser::crosstabStatement()
{
    const int select = find(m_first + 1, m_last, "select");
    if (select < 0) {
        return fail(QStringLiteral("TRANSFORM is followed by a total and then SELECT."));
    }
    QHash<QString, Range> cl;
    if (!clauses(select, &cl)) {
        return false;
    }
    if (!cl.contains(QStringLiteral("pivot")) || !cl.contains(QStringLiteral("from"))) {
        return fail(QStringLiteral("A crosstab needs FROM and PIVOT."));
    }
    d.kind = QueryKind::Crosstab;
    d.totals = true;
    if (!fromClause(cl.value(QStringLiteral("from")))) {
        return false;
    }
    const Range value{m_first + 1, select};
    if (!aggregateCallIn(m_t, value.first, at(value.last - 2).is("as") ? value.last - 2 : value.last)) {
        return fail(QStringLiteral("TRANSFORM takes a total: Sum, Avg, Count, Min or Max."));
    }
    if (!selectList(cl.value(QStringLiteral("select")), true, QStringLiteral("row"))) {
        return false;
    }
    const Range pivot = cl.value(QStringLiteral("pivot"));
    const int in = find(pivot.first, pivot.last, "in");
    QueryColumn heading = columnFor({pivot.first, in < 0 ? pivot.last : in}, false);
    heading.crosstab = QStringLiteral("column");
    addColumn(heading);
    if (in >= 0 && !inList({in + 1, pivot.last}, &d.columnHeadings)) {
        return false;
    }
    QueryColumn cell = columnFor(value, true);
    cell.crosstab = QStringLiteral("value");
    addColumn(cell);
    if (cl.contains(QStringLiteral("group")) && !groupBy(cl.value(QStringLiteral("group")))) {
        return false;
    }
    if (cl.contains(QStringLiteral("where")) && !criteria(cl.value(QStringLiteral("where")), false)) {
        return false;
    }
    if (cl.contains(QStringLiteral("having")) && !criteria(cl.value(QStringLiteral("having")), true)) {
        return false;
    }
    return !cl.contains(QStringLiteral("order")) || orderBy(cl.value(QStringLiteral("order")));
}

// UPDATE <table> SET <table.field> = <value>, … [WHERE …]
bool SqlParser::updateStatement()
{
    const int set = find(m_first + 1, m_last, "set");
    if (set < 0) {
        return fail(QStringLiteral("UPDATE needs SET."));
    }
    d.kind = QueryKind::Update;
    if (!fromClause({m_first + 1, set})) {
        return false;
    }
    if (d.tables.size() != 1) {
        return fail(QStringLiteral("Update queries work on one table."));
    }
    QHash<QString, Range> cl;
    if (!clauses(set, &cl)) {
        return false;
    }
    for (const Range &item : splitCommas(cl.value(QStringLiteral("set")))) {
        int eq = item.first;
        while (eq < item.last && !at(eq).op("=")) {
            ++eq;
        }
        const auto ref = fieldRefIn(m_t, item.first, eq);
        if (eq >= item.last - 1 || !ref) {
            return fail(QStringLiteral("SET takes field = value."));
        }
        QueryColumn c;
        c.table = ref->first;
        c.field = ref->second;
        c.updateTo = text({eq + 1, item.last});
        addColumn(c);
    }
    return !cl.contains(QStringLiteral("where")) || criteria(cl.value(QStringLiteral("where")), false);
}

// DELETE [* | table.*] FROM <table> [WHERE …]
bool SqlParser::deleteStatement()
{
    const int from = find(m_first + 1, m_last, "from");
    if (from < 0) {
        return fail(QStringLiteral("DELETE needs FROM."));
    }
    QHash<QString, Range> cl;
    if (!clauses(from, &cl)) {
        return false;
    }
    d.kind = QueryKind::Delete;
    if (!fromClause(cl.value(QStringLiteral("from")))) {
        return false;
    }
    if (d.tables.size() != 1) {
        return fail(QStringLiteral("Delete queries work on one table."));
    }
    return !cl.contains(QStringLiteral("where")) || criteria(cl.value(QStringLiteral("where")), false);
}

// INSERT INTO <table> [(fields)] SELECT <values> FROM … [WHERE …]
bool SqlParser::appendStatement()
{
    if (!at(m_first + 1).is("into") || !nameToken(at(m_first + 2))) {
        return fail(QStringLiteral("INSERT INTO names the table to append to."));
    }
    const std::optional<TableDef> target = readTable(m_file, at(m_first + 2).text);
    if (!target) {
        return fail(QStringLiteral("There is no table named %1.").arg(at(m_first + 2).text));
    }
    int pos = m_first + 3;
    QStringList fields;
    if (at(pos).op("(")) {
        const int close = closing(m_t, pos);
        for (const Range &f : splitCommas({pos + 1, close})) {
            if (f.last - f.first != 1 || target->field(at(f.first).text) == nullptr) {
                return fail(QStringLiteral("%1 has no field %2.").arg(target->name, text(f)));
            }
            fields.append(target->field(at(f.first).text)->name);
        }
        pos = close + 1;
    }
    if (at(pos).is("values")) {
        return fail(QStringLiteral("INSERT … VALUES adds fixed records; keep it as SQL."));
    }
    if (!at(pos).is("select")) {
        return fail(QStringLiteral("INSERT INTO is followed by SELECT."));
    }
    QHash<QString, Range> cl;
    if (!clauses(pos, &cl)) {
        return false;
    }
    if (cl.contains(QStringLiteral("group")) || cl.contains(QStringLiteral("having")) || cl.contains(QStringLiteral("order"))
        || at(cl.value(QStringLiteral("select")).first).is("distinct")) {
        return fail(QStringLiteral("An append query in the grid takes no DISTINCT, GROUP BY or ORDER BY; keep it as SQL."));
    }
    d.kind = QueryKind::Append;
    d.appendTable = target->name;
    if (!fromClause(cl.value(QStringLiteral("from")))) {
        return false;
    }
    const QList<Range> items = splitCommas(cl.value(QStringLiteral("select")));
    for (int i = 0; i < items.size(); ++i) {
        const QString into = i < fields.size() ? fields.at(i) : fields.isEmpty() ? target->fields.value(i).name : QString();
        if (into.isEmpty()) {
            return fail(QStringLiteral("More values than fields to append them to."));
        }
        QueryColumn c = columnFor(items.at(i), false);
        c.appendTo = into;
        addColumn(c);
    }
    return !cl.contains(QStringLiteral("where")) || criteria(cl.value(QStringLiteral("where")), false);
}

} // namespace QindaOffice::Base::AccessSql
