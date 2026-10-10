// SPDX-License-Identifier: GPL-3.0-or-later
// Access's query wizards as query designs (base_query_wizards.h).
#include "base_query_wizards.h"

#include <QSet>

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

// A field or table name as Access text writes it in an expression.
QString bracket(const QString &name)
{
    return QLatin1Char('[') + name + QLatin1Char(']');
}

QueryColumn column(const QString &table, const QString &field)
{
    QueryColumn c;
    c.table = table;
    c.field = field;
    return c;
}

QueryColumn expressionColumn(const QString &expression, const QString &alias)
{
    QueryColumn c;
    c.expression = expression;
    c.alias = alias;
    return c;
}

WizardResult finish(const BaseFile &file, QueryDesign design)
{
    WizardResult out;
    const CompiledQuery compiled = compileQuery(file, design);
    if (!compiled.ok) {
        out.error = compiled.error;
        return out;
    }
    out.ok = true;
    out.design = std::move(design);
    return out;
}

WizardResult refused(const QString &error)
{
    WizardResult out;
    out.error = error;
    return out;
}

const FieldDef *fieldOf(const std::optional<TableDef> &table, const QString &name)
{
    return table ? table->field(name) : nullptr;
}

QString functionWord(const QString &function)
{
    return function == QLatin1String("average") ? s("Avg")
           : function == QLatin1String("min")   ? s("Min")
           : function == QLatin1String("max")   ? s("Max")
           : function == QLatin1String("count") ? s("Count")
                                                : s("Sum");
}

TotalKind totalOf(const QString &function)
{
    return function == QLatin1String("average") ? TotalKind::Average
           : function == QLatin1String("min")   ? TotalKind::Min
           : function == QLatin1String("max")   ? TotalKind::Max
           : function == QLatin1String("count") ? TotalKind::Count
                                                : TotalKind::Sum;
}

} // namespace

QList<QueryJoin> relationshipJoins(const BaseFile &file, const QStringList &tables)
{
    QList<QueryJoin> out;
    for (const Relationship &r : readRelationships(file)) {
        if (tables.contains(r.childTable, Qt::CaseInsensitive) && tables.contains(r.parentTable, Qt::CaseInsensitive)
            && r.childTable.compare(r.parentTable, Qt::CaseInsensitive) != 0) {
            out.append({r.childTable, r.childField, r.parentTable, r.parentField, s("inner")});
        }
    }
    return out;
}

WizardResult simpleQueryWizard(const BaseFile &file, const SimpleQuerySpec &spec)
{
    if (spec.fields.isEmpty()) {
        return refused(s("Choose the fields the query shows."));
    }
    QueryDesign d;
    QStringList tables;
    for (const auto &[table, field] : spec.fields) {
        const std::optional<TableDef> def = readTable(file, table);
        if (fieldOf(def, field) == nullptr) {
            return refused(s("%1 has no field %2.").arg(table, field));
        }
        if (!tables.contains(def->name, Qt::CaseInsensitive)) {
            tables.append(def->name);
            d.tables.append({def->name, QString()});
        }
    }
    d.joins = relationshipJoins(file, tables);
    // Every table must be joined to the first (a cross product is never what
    // the Simple Query Wizard means).
    QSet<QString> reached{tables.first().toLower()};
    for (bool grew = true; grew;) {
        grew = false;
        for (const QueryJoin &j : std::as_const(d.joins)) {
            const bool left = reached.contains(j.leftTable.toLower());
            const bool right = reached.contains(j.rightTable.toLower());
            if (left != right) {
                reached.insert((left ? j.rightTable : j.leftTable).toLower());
                grew = true;
            }
        }
    }
    for (const QString &t : std::as_const(tables)) {
        if (!reached.contains(t.toLower())) {
            return refused(s("%1 is not related to %2: relate them first (Database Tools ▸ Relationships), or choose "
                             "fields of related tables.")
                               .arg(t, tables.first()));
        }
    }
    d.totals = spec.summary;
    for (const auto &[table, field] : spec.fields) {
        const std::optional<TableDef> def = readTable(file, table);
        const FieldDef *f = fieldOf(def, field);
        const QString key = def->name + QLatin1Char('.') + f->name;
        const bool totalled = std::any_of(spec.totals.cbegin(), spec.totals.cend(),
                                          [&](const auto &t) { return t.first.compare(key, Qt::CaseInsensitive) == 0; });
        if (!spec.summary || !totalled) {
            const bool date = f->type == FieldType::Date || f->type == FieldType::DateTime;
            if (spec.summary && date && spec.dateGrouping != QLatin1String("unique")) {
                // Access's Summary Options group a date by these (Format()).
                const QString ref = bracket(def->name) + QLatin1Char('.') + bracket(f->name);
                const QString by = spec.dateGrouping;
                const QString expression =
                    by == QLatin1String("day")       ? s("Format(%1, \"Short Date\")").arg(ref)
                    : by == QLatin1String("month")   ? s("Format(%1, \"mmmm yyyy\")").arg(ref)
                    : by == QLatin1String("quarter") ? s("Format(%1, \"yyyy\") & \" Q\" & Format(%1, \"q\")").arg(ref)
                                                     : s("Format(%1, \"yyyy\")").arg(ref);
                const QString label = by == QLatin1String("day")       ? s("Day")
                                      : by == QLatin1String("month")   ? s("Month")
                                      : by == QLatin1String("quarter") ? s("Quarter")
                                                                       : s("Year");
                QueryColumn c = expressionColumn(expression, s("%1 By %2").arg(f->name, label));
                if (by == QLatin1String("month")) {
                    // Month names sort by the calendar through a hidden key.
                    QueryColumn key = expressionColumn(s("Format(%1, \"yyyy-mm\")").arg(ref), s("%1 Month Key").arg(f->name));
                    key.sort = 1;
                    key.show = false;
                    d.columns.append(key);
                } else {
                    c.sort = 1;
                }
                d.columns.append(c);
            } else {
                d.columns.append(column(def->name, f->name));
            }
        }
        if (spec.summary) {
            for (const auto &[target, function] : spec.totals) {
                if (target.compare(key, Qt::CaseInsensitive) != 0) {
                    continue;
                }
                if (!isNumeric(f->type)) {
                    return refused(s("%1 is not a number; only numbers are summed, averaged and compared.").arg(f->name));
                }
                QueryColumn c = column(def->name, f->name);
                c.total = totalOf(function);
                c.alias = s("%1 Of %2").arg(functionWord(function), f->name);
                d.columns.append(c);
            }
        }
    }
    if (spec.summary && spec.countRecords) {
        QueryColumn c = expressionColumn(s("Count(*)"), s("Count Of %1").arg(tables.first()));
        c.total = TotalKind::Expression;
        d.columns.append(c);
    }
    return finish(file, d);
}

WizardResult crosstabQueryWizard(const BaseFile &file, const CrosstabWizardSpec &spec)
{
    const std::optional<TableDef> def = readTable(file, spec.table);
    if (!def) {
        return refused(s("There is no table named %1.").arg(spec.table));
    }
    if (spec.rowHeadings.isEmpty() || spec.rowHeadings.size() > 3) {
        return refused(s("Choose one to three fields for the row headings."));
    }
    const FieldDef *heading = def->field(spec.columnHeading);
    const FieldDef *value = def->field(spec.valueField);
    if (heading == nullptr || value == nullptr) {
        return refused(s("Choose the column heading and the value from %1.").arg(def->name));
    }
    if (spec.rowHeadings.contains(heading->name, Qt::CaseInsensitive)) {
        return refused(s("%1 is a row heading already; choose another field for the columns.").arg(heading->name));
    }
    QueryDesign d;
    d.kind = QueryKind::Crosstab;
    d.totals = true;
    d.tables.append({def->name, QString()});
    for (const QString &row : spec.rowHeadings) {
        const FieldDef *f = def->field(row);
        if (f == nullptr) {
            return refused(s("%1 has no field %2.").arg(def->name, row));
        }
        QueryColumn c = column(def->name, f->name);
        c.crosstab = s("row");
        c.sort = 1;
        d.columns.append(c);
    }
    QueryColumn across = column(def->name, heading->name);
    const bool date = heading->type == FieldType::Date || heading->type == FieldType::DateTime;
    const QString ref = bracket(def->name) + QLatin1Char('.') + bracket(heading->name);
    if (date && spec.dateInterval == QLatin1String("year")) {
        across = expressionColumn(s("Format(%1, \"yyyy\")").arg(ref), s("Year"));
    } else if (date && spec.dateInterval == QLatin1String("quarter")) {
        across = expressionColumn(s("\"Qtr \" & Format(%1, \"q\")").arg(ref), s("Quarter"));
        d.columnHeadings = {s("Qtr 1"), s("Qtr 2"), s("Qtr 3"), s("Qtr 4")};
    } else if (date && spec.dateInterval == QLatin1String("month")) {
        across = expressionColumn(s("Format(%1, \"mmm\")").arg(ref), s("Month"));
        d.columnHeadings = {s("Jan"), s("Feb"), s("Mar"), s("Apr"), s("May"), s("Jun"),
                            s("Jul"), s("Aug"), s("Sep"), s("Oct"), s("Nov"), s("Dec")};
    } else if (date && spec.dateInterval == QLatin1String("date")) {
        across = expressionColumn(s("Format(%1, \"Short Date\")").arg(ref), s("Date"));
    }
    across.crosstab = s("column");
    d.columns.append(across);
    if (spec.function != QLatin1String("count") && !isNumeric(value->type)) {
        return refused(s("%1 is not a number; count it instead.").arg(value->name));
    }
    QueryColumn cell = column(def->name, value->name);
    cell.total = totalOf(spec.function);
    cell.crosstab = s("value");
    d.columns.append(cell);
    if (spec.rowSums) {
        QueryColumn sum = column(def->name, value->name);
        sum.total = totalOf(spec.function);
        sum.crosstab = s("row");
        sum.alias = s("Total Of %1").arg(value->name);
        d.columns.append(sum);
    }
    return finish(file, d);
}

WizardResult findDuplicatesWizard(const BaseFile &file, const DuplicatesSpec &spec)
{
    const std::optional<TableDef> def = readTable(file, spec.table);
    if (!def) {
        return refused(s("There is no table named %1.").arg(spec.table));
    }
    if (spec.duplicateFields.isEmpty()) {
        return refused(s("Choose the fields whose values may repeat."));
    }
    QStringList keys;
    for (const QString &name : spec.duplicateFields) {
        const FieldDef *f = def->field(name);
        if (f == nullptr || f->type == FieldType::File) {
            return refused(s("%1 has no field %2 that can repeat.").arg(def->name, name));
        }
        keys.append(f->name);
    }
    QueryDesign d;
    d.tables.append({def->name, QString()});
    if (spec.otherFields.isEmpty()) {
        // Access's own shape: the repeated values and how often they repeat.
        d.totals = true;
        for (const QString &key : std::as_const(keys)) {
            QueryColumn c = column(def->name, key);
            c.sort = 1;
            d.columns.append(c);
        }
        QueryColumn count = expressionColumn(s("Count(*)"), s("NumberOfDups"));
        count.total = TotalKind::Expression;
        count.criteria = s("> 1");
        d.columns.append(count);
        return finish(file, d);
    }
    QStringList inner;
    QStringList outer;
    for (const QString &key : std::as_const(keys)) {
        inner.append(bracket(key));
        outer.append(bracket(def->name) + QLatin1Char('.') + bracket(key));
    }
    const QString subquery = s("In (SELECT %1 FROM %2 AS Tmp GROUP BY %3 HAVING Count(*) > 1)")
                                 .arg(inner.join(s(" & \"|\" & ")), bracket(def->name), inner.join(s(", ")));
    for (const QString &key : std::as_const(keys)) {
        QueryColumn c = column(def->name, key);
        c.sort = 1;
        d.columns.append(c);
    }
    for (const QString &name : spec.otherFields) {
        const FieldDef *f = def->field(name);
        if (f == nullptr) {
            return refused(s("%1 has no field %2.").arg(def->name, name));
        }
        if (!keys.contains(f->name, Qt::CaseInsensitive)) {
            d.columns.append(column(def->name, f->name));
        }
    }
    if (keys.size() == 1) {
        d.columns.first().criteria = subquery;
    } else {
        QueryColumn joined = expressionColumn(outer.join(s(" & \"|\" & ")), s("DuplicateKey"));
        joined.show = false;
        joined.criteria = subquery;
        d.columns.append(joined);
    }
    return finish(file, d);
}

WizardResult findUnmatchedWizard(const BaseFile &file, const UnmatchedSpec &spec)
{
    const std::optional<TableDef> def = readTable(file, spec.table);
    const std::optional<TableDef> other = readTable(file, spec.relatedTable);
    if (!def || !other) {
        return refused(s("Choose the table whose records are listed and the table to look in."));
    }
    if (def->name.compare(other->name, Qt::CaseInsensitive) == 0) {
        return refused(s("Look for matches in another table."));
    }
    const FieldDef *mine = def->field(spec.field);
    const FieldDef *theirs = other->field(spec.relatedField);
    if (mine == nullptr || theirs == nullptr) {
        return refused(s("Choose the field the two tables have in common."));
    }
    QueryDesign d;
    d.tables = {{def->name, QString()}, {other->name, QString()}};
    d.joins.append({def->name, mine->name, other->name, theirs->name, s("left")});
    const QStringList shown = spec.showFields.isEmpty() ? QStringList{mine->name} : spec.showFields;
    for (const QString &name : shown) {
        const FieldDef *f = def->field(name);
        if (f == nullptr) {
            return refused(s("%1 has no field %2.").arg(def->name, name));
        }
        d.columns.append(column(def->name, f->name));
    }
    QueryColumn missing = column(other->name, theirs->name);
    missing.show = false;
    missing.criteria = s("Is Null");
    d.columns.append(missing);
    return finish(file, d);
}

} // namespace QindaOffice::Base
