// SPDX-License-Identifier: GPL-3.0-or-later
// Query designs: maps for QML and saved objects, and compilation to SQL
// (select with joins/totals/criteria/sort, update, delete, append; the
// shared core is base_query_core.cpp, crosstabs base_query_crosstab.cpp).
#include "base_query_p.h"

#include <QHash>
#include <QSet>

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

QString q(const QString &identifier)
{
    return quoteIdentifier(identifier);
}

const char *const kKinds[] = {"select", "update", "delete", "append", "sql", "crosstab", "makeTable"};
const char *const kTotals[] = {"groupBy", "sum", "average", "count", "min", "max", "where", "expression"};

// Keys QueryDesign::toMap writes; everything else in a saved map is `extra`.
const QStringList kKnownKeys{s("kind"),        s("tables"),         s("joins"),          s("columns"),
                             s("totals"),      s("distinct"),       s("top"),            s("topPercent"),
                             s("appendTable"), s("makeTable"),      s("columnHeadings"), s("parameters"),
                             s("sql"),         s("version")};

} // namespace

QString queryKindId(QueryKind kind)
{
    return QString::fromLatin1(kKinds[int(kind)]);
}

QueryKind queryKindFromId(const QString &id)
{
    for (int i = 0; i < int(std::size(kKinds)); ++i) {
        if (id == QLatin1String(kKinds[i])) {
            return QueryKind(i);
        }
    }
    return QueryKind::Select;
}

QString totalKindId(TotalKind kind)
{
    return QString::fromLatin1(kTotals[int(kind)]);
}

TotalKind totalKindFromId(const QString &id)
{
    for (int i = 0; i < 8; ++i) {
        if (id == QLatin1String(kTotals[i])) {
            return TotalKind(i);
        }
    }
    return TotalKind::GroupBy;
}

QVariantMap QueryParameter::toMap() const
{
    return {{s("name"), name},
            {s("prompt"), prompt},
            {s("type"), typeId(type)},
            {s("typeLabel"), typeLabel(type)},
            {s("declared"), declared}};
}

QVariantMap QueryDesign::toMap() const
{
    QVariantMap out = extra;
    QVariantList tableList;
    for (const QueryTable &t : tables) {
        tableList.append(QVariantMap{{s("name"), t.name}, {s("alias"), t.alias}});
    }
    QVariantList joinList;
    for (const QueryJoin &j : joins) {
        joinList.append(QVariantMap{{s("leftTable"), j.leftTable}, {s("leftField"), j.leftField},
                                    {s("rightTable"), j.rightTable}, {s("rightField"), j.rightField},
                                    {s("kind"), j.kind}});
    }
    QVariantList columnList;
    for (const QueryColumn &c : columns) {
        columnList.append(QVariantMap{{s("table"), c.table},       {s("field"), c.field},
                                      {s("expression"), c.expression}, {s("alias"), c.alias},
                                      {s("total"), totalKindId(c.total)}, {s("sort"), c.sort},
                                      {s("show"), c.show},         {s("criteria"), c.criteria},
                                      {s("orCriteria"), c.orCriteria}, {s("updateTo"), c.updateTo},
                                      {s("appendTo"), c.appendTo},   {s("crosstab"), c.crosstab}});
    }
    QVariantList parameterList;
    for (const QueryParameter &p : parameters) {
        parameterList.append(QVariantMap{{s("prompt"), p.prompt}, {s("type"), typeId(p.type)}});
    }
    out.insert(s("version"), 1);
    out.insert(s("kind"), queryKindId(kind));
    out.insert(s("tables"), tableList);
    out.insert(s("joins"), joinList);
    out.insert(s("columns"), columnList);
    out.insert(s("totals"), totals);
    out.insert(s("distinct"), distinct);
    out.insert(s("top"), top);
    out.insert(s("topPercent"), topPercent);
    out.insert(s("appendTable"), appendTable);
    out.insert(s("makeTable"), makeTable);
    out.insert(s("columnHeadings"), columnHeadings);
    out.insert(s("parameters"), parameterList);
    out.insert(s("sql"), sql);
    return out;
}

QueryDesign QueryDesign::fromMap(const QVariantMap &map)
{
    QueryDesign d;
    d.kind = queryKindFromId(map.value(s("kind")).toString());
    for (const QVariant &v : map.value(s("tables")).toList()) {
        const QVariantMap m = v.toMap();
        d.tables.append({m.value(s("name")).toString(), m.value(s("alias")).toString()});
    }
    for (const QVariant &v : map.value(s("joins")).toList()) {
        const QVariantMap m = v.toMap();
        d.joins.append({m.value(s("leftTable")).toString(), m.value(s("leftField")).toString(),
                        m.value(s("rightTable")).toString(), m.value(s("rightField")).toString(),
                        m.value(s("kind"), s("inner")).toString()});
    }
    for (const QVariant &v : map.value(s("columns")).toList()) {
        const QVariantMap m = v.toMap();
        QueryColumn c;
        c.table = m.value(s("table")).toString();
        c.field = m.value(s("field")).toString();
        c.expression = m.value(s("expression")).toString();
        c.alias = m.value(s("alias")).toString();
        c.total = totalKindFromId(m.value(s("total")).toString());
        c.sort = m.value(s("sort")).toInt();
        c.show = m.value(s("show"), true).toBool();
        c.criteria = m.value(s("criteria")).toString();
        c.orCriteria = m.value(s("orCriteria")).toStringList();
        c.updateTo = m.value(s("updateTo")).toString();
        c.appendTo = m.value(s("appendTo")).toString();
        c.crosstab = m.value(s("crosstab")).toString();
        d.columns.append(c);
    }
    for (const QVariant &v : map.value(s("parameters")).toList()) {
        const QVariantMap m = v.toMap();
        QueryParameter p;
        p.prompt = m.value(s("prompt")).toString().trimmed();
        p.type = typeFromId(m.value(s("type")).toString()).value_or(FieldType::Text);
        p.declared = true;
        if (!p.prompt.isEmpty()) {
            d.parameters.append(p);
        }
    }
    d.totals = map.value(s("totals")).toBool();
    d.distinct = map.value(s("distinct")).toBool();
    d.top = map.value(s("top")).toInt();
    d.topPercent = map.value(s("topPercent")).toBool();
    d.appendTable = map.value(s("appendTable")).toString();
    d.makeTable = map.value(s("makeTable")).toString().trimmed();
    d.columnHeadings = map.value(s("columnHeadings")).toStringList();
    d.sql = map.value(s("sql")).toString();
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        if (!kKnownKeys.contains(it.key())) {
            d.extra.insert(it.key(), it.value());
        }
    }
    return d;
}

CompiledQuery compileQuery(const BaseFile &file, const QueryDesign &design)
{
    return compileQuery(file, design, {});
}

namespace {

CompiledQuery compileDesign(const BaseFile &file, const QueryDesign &design, const QVariantMap *parameterValues)
{
    CompiledQuery out;
    const auto fail = [&out](const QString &error) {
        out.ok = false;
        out.error = error;
        return out;
    };
    if (design.kind == QueryKind::Sql) {
        const SqlInfo info = inspectSql(file, design.sql);
        if (!info.ok) {
            return fail(info.error);
        }
        if (info.schema) {
            return fail(QStringLiteral("Schema changes (%1) belong in Database Tools ▸ SQL, where they are previewed and "
                                       "backed up.")
                            .arg(info.keyword));
        }
        out.ok = true;
        out.sql = design.sql.trimmed();
        out.action = !info.readOnly;
        out.parameters = info.parameters;
        return out;
    }
    if (design.kind == QueryKind::Crosstab) {
        return detail::compileCrosstab(file, design, parameterValues);
    }
    detail::QueryCore core;
    if (const QString e = detail::buildQueryCore(file, design, &out.parameters, &core); !e.isEmpty()) {
        return fail(e);
    }
    const Expr::Context ctx{core.resolve, core.currency, &out.parameters};
    if (design.kind == QueryKind::Select || design.kind == QueryKind::MakeTable) {
        QString error;
        out.sql = detail::selectSql(design, core, &out.columns, &error);
        if (!error.isEmpty()) {
            return fail(error);
        }
        if (design.kind == QueryKind::MakeTable) {
            // AGENT-GUARD: a make-table query replaces its target, so it may
            // never write into a table it reads (Access refuses it too).
            if (QString e = validateName(design.makeTable, QStringLiteral("table")); !e.isEmpty()) {
                return fail(QStringLiteral("Name the new table: %1").arg(e));
            }
            if (BaseFile::isSystemName(design.makeTable)) {
                return fail(QStringLiteral("Table names starting with qb_ or sqlite_ are reserved."));
            }
            for (const detail::QueryTableInfo &t : core.tables) {
                if (t.name.compare(design.makeTable, Qt::CaseInsensitive) == 0) {
                    return fail(QStringLiteral("The query reads %1, so it cannot replace it.").arg(t.name));
                }
            }
            out.makeTable = design.makeTable;
            out.action = true;
        }
        out.ok = true;
        return out;
    }

    out.action = true;
    const detail::QueryTableInfo &target = core.tables.first();
    const QString where = core.where();
    if (design.kind == QueryKind::Update || design.kind == QueryKind::Delete) {
        if (core.tables.size() != 1) {
            return fail(QStringLiteral("Update and delete queries work on one table."));
        }
        if (design.kind == QueryKind::Delete) {
            out.sql = QStringLiteral("DELETE FROM %1%2").arg(q(target.name), where);
            out.ok = true;
            return out;
        }
        QStringList sets;
        for (int i = 0; i < design.columns.size(); ++i) {
            const QueryColumn &c = design.columns.at(i);
            if (c.updateTo.trimmed().isEmpty() || core.cols.at(i).sourceField.isEmpty()) {
                continue;
            }
            const FieldType type = core.cols.at(i).type.value_or(FieldType::Text);
            Expr::Compiled v = Expr::valueFor(c.updateTo, type, ctx);
            if (!v.ok()) {
                return fail(QStringLiteral("Update to for %1: %2").arg(c.field, v.error));
            }
            if (type == FieldType::Currency && v.type != FieldType::Currency) {
                v.sql = QStringLiteral("CAST(round(%1) AS INTEGER)").arg(v.sql);
            }
            sets.append(QStringLiteral("%1 = %2").arg(q(core.cols.at(i).sourceField), v.sql));
        }
        if (sets.isEmpty()) {
            return fail(QStringLiteral("Fill in \"Update to\" for at least one field."));
        }
        out.sql = QStringLiteral("UPDATE %1 SET %2%3").arg(q(target.name), sets.join(QStringLiteral(", ")), where);
        out.ok = true;
        return out;
    }
    // Append: INSERT INTO target(fields) SELECT values FROM … WHERE …
    const std::optional<TableDef> dest = readTable(file, design.appendTable);
    if (!dest) {
        return fail(QStringLiteral("Choose the table to append to."));
    }
    QStringList into;
    QStringList values;
    for (int i = 0; i < design.columns.size(); ++i) {
        const QueryColumn &c = design.columns.at(i);
        if (c.appendTo.trimmed().isEmpty()) {
            continue;
        }
        const FieldDef *f = dest->field(c.appendTo);
        if (f == nullptr) {
            return fail(QStringLiteral("%1 has no field %2.").arg(dest->name, c.appendTo));
        }
        into.append(q(f->name));
        values.append(core.cols.at(i).sql);
    }
    if (into.isEmpty()) {
        return fail(QStringLiteral("Choose \"Append to\" fields."));
    }
    out.sql = QStringLiteral("INSERT INTO %1(%2)\nSELECT %3\nFROM %4%5")
                  .arg(q(dest->name), into.join(QStringLiteral(", ")), values.join(QStringLiteral(", ")), core.from, where);
    out.ok = true;
    return out;
}

} // namespace

CompiledQuery compileQuery(const BaseFile &file, const QueryDesign &design, const QVariantMap &parameters)
{
    // AGENT-NOTE: an empty map means "no values yet" (previews); a crosstab
    // then takes its headings from every row the criteria allow.
    return compileDesign(file, design, parameters.isEmpty() ? nullptr : &parameters);
}

} // namespace QindaOffice::Base
