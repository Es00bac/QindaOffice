// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService: query designs, compilation, parameters, running selects
// into the datasheet and action queries behind a confirmed count.
#include "base_document.h"
#include "base_objects.h"
#include "base_query.h"
#include "base_service.h"

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QVariantMap failed(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

QVariantList parameterList(const QList<Engine::QueryParameter> &parameters)
{
    QVariantList out;
    for (const Engine::QueryParameter &p : parameters) {
        out.append(p.toMap());
    }
    return out;
}

} // namespace

QVariantMap BaseService::queryDesign(const QString &name) const
{
    if (file() == nullptr) {
        return failed(tr("No database is open."));
    }
    const std::optional<Engine::SavedObject> o = Engine::ObjectStore::load(*file(), QStringLiteral("query"), name);
    if (!o) {
        return failed(tr("There is no query named %1.").arg(name));
    }
    QVariantMap map = Engine::QueryDesign::fromMap(o->definition).toMap();
    map.insert(QStringLiteral("ok"), true);
    map.insert(QStringLiteral("name"), o->name);
    map.insert(QStringLiteral("description"), o->definition.value(QStringLiteral("description")));
    return map;
}

QVariantMap BaseService::newQueryDesign(const QStringList &tables) const
{
    Engine::QueryDesign d;
    for (const QString &t : tables) {
        d.tables.append({t, QString()});
    }
    QVariantMap map = d.toMap();
    map.insert(QStringLiteral("joins"), suggestJoins(tables));
    map.insert(QStringLiteral("ok"), true);
    map.insert(QStringLiteral("name"), QString());
    return map;
}

QVariantList BaseService::suggestJoins(const QStringList &tables) const
{
    QVariantList out;
    if (file() == nullptr) {
        return out;
    }
    for (const Engine::Relationship &r : Engine::readRelationships(*file())) {
        if (tables.contains(r.childTable, Qt::CaseInsensitive) && tables.contains(r.parentTable, Qt::CaseInsensitive)
            && r.childTable.compare(r.parentTable, Qt::CaseInsensitive) != 0) {
            out.append(QVariantMap{{QStringLiteral("leftTable"), r.childTable},
                                   {QStringLiteral("leftField"), r.childField},
                                   {QStringLiteral("rightTable"), r.parentTable},
                                   {QStringLiteral("rightField"), r.parentField},
                                   {QStringLiteral("kind"), QStringLiteral("inner")}});
        }
    }
    return out;
}

QVariantMap BaseService::compileQuery(const QVariantMap &design) const
{
    if (file() == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::CompiledQuery c = Engine::compileQuery(*file(), Engine::QueryDesign::fromMap(design));
    QStringList columns; // M86: a union's order picks from its first part's columns
    for (const Engine::OutputColumn &o : c.columns) {
        columns.append(o.name);
    }
    return {{QStringLiteral("ok"), c.ok},
            {QStringLiteral("columns"), columns},
            {QStringLiteral("error"), c.error},
            {QStringLiteral("sql"), c.sql},
            {QStringLiteral("action"), c.action},
            {QStringLiteral("parameters"), parameterList(c.parameters)},
            // M80: a crosstab's headings (every row's, before parameters)
            // and a make-table query's new table.
            {QStringLiteral("crosstab"), c.crosstab},
            {QStringLiteral("headings"), c.headings},
            {QStringLiteral("makeTable"), c.makeTable}};
}

QVariantMap BaseService::saveQuery(const QString &name, const QVariantMap &design)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    if (!f->hasSystemTables()) {
        return needsSystemTables();
    }
    // Merge into what is saved so members a newer version wrote survive.
    QVariantMap definition;
    if (const std::optional<Engine::SavedObject> existing = Engine::ObjectStore::load(*f, QStringLiteral("query"), name)) {
        definition = existing->definition;
    }
    const QVariantMap known = Engine::QueryDesign::fromMap(design).toMap();
    for (auto it = known.cbegin(); it != known.cend(); ++it) {
        definition.insert(it.key(), it.value());
    }
    if (design.contains(QStringLiteral("description"))) {
        definition.insert(QStringLiteral("description"), design.value(QStringLiteral("description")));
    }
    Engine::Change change;
    const Engine::Result r = Engine::ObjectStore::save(*f, QStringLiteral("query"), name.trimmed(), definition, true, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("name"), name.trimmed()}};
}

QVariantMap BaseService::runQuery(const QString &name, const QVariantMap &design, const QVariantMap &parameters)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    QVariantMap source = design;
    if (source.isEmpty()) {
        const std::optional<Engine::SavedObject> o = Engine::ObjectStore::load(*f, QStringLiteral("query"), name);
        if (!o) {
            return failed(tr("There is no query named %1.").arg(name));
        }
        source = o->definition;
    }
    const Engine::CompiledQuery c = Engine::compileQuery(*f, Engine::QueryDesign::fromMap(source));
    if (!c.ok) {
        return failed(c.error);
    }
    QVariantList missing;
    for (const Engine::QueryParameter &p : c.parameters) {
        if (!parameters.contains(p.name)) {
            missing.append(p.toMap());
        }
    }
    if (!missing.isEmpty()) {
        return {{QStringLiteral("ok"), false},
                {QStringLiteral("needsParameters"), true},
                {QStringLiteral("parameters"), parameterList(c.parameters)}};
    }
    if (!c.makeTable.isEmpty()) {
        // M80: a make-table query is a schema step, shown as its plan first.
        return makeTablePlan(Engine::QueryDesign::fromMap(source), parameters);
    }
    if (c.crosstab && !parameters.isEmpty()) {
        // M80: a crosstab's columns are the headings of the rows these
        // values select, so it compiles again with them.
        const Engine::CompiledQuery bound = Engine::compileQuery(*f, Engine::QueryDesign::fromMap(source), parameters);
        if (!bound.ok) {
            return failed(bound.error);
        }
        if (!m_datasheet->showQuery(name.isEmpty() ? tr("Query") : name, bound.sql, bound.parameters, parameters,
                                    bound.columns)) {
            return failed(m_datasheet->error());
        }
        return {{QStringLiteral("ok"), true}, {QStringLiteral("action"), false},
                {QStringLiteral("rows"), m_datasheet->recordCount()}, {QStringLiteral("sql"), bound.sql}};
    }
    if (c.action) {
        qint64 affected = 0;
        const Engine::Result preview = Engine::previewAction(*f, c.sql, c.parameters, parameters, &affected);
        if (!preview.ok) {
            return failed(preview.error);
        }
        return {{QStringLiteral("ok"), true},
                {QStringLiteral("action"), true},
                {QStringLiteral("affected"), affected},
                {QStringLiteral("sql"), c.sql},
                {QStringLiteral("keyword"), Engine::firstKeyword(c.sql)}};
    }
    if (!m_datasheet->showQuery(name.isEmpty() ? tr("Query") : name, c.sql, c.parameters, parameters, c.columns)) {
        return failed(m_datasheet->error());
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("action"), false},
            {QStringLiteral("rows"), m_datasheet->recordCount()},
            {QStringLiteral("sql"), c.sql}};
}

QVariantMap BaseService::runAction(const QVariantMap &design, const QVariantMap &parameters, qint64 confirmedCount)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::CompiledQuery c = Engine::compileQuery(*f, Engine::QueryDesign::fromMap(design));
    if (!c.ok) {
        return failed(c.error);
    }
    if (!c.action) {
        return failed(tr("This query does not change data."));
    }
    qint64 affected = 0;
    Engine::Change change;
    const Engine::Result r = Engine::runAction(*f, c.sql, c.parameters, parameters, confirmedCount, &change, &affected);
    if (!r.ok) {
        return failed(r.error);
    }
    change.label = tr("Run %1 query").arg(Engine::firstKeyword(c.sql).toLower());
    m_document->pushChange(change);
    emit notify(tr("Query run"), tr("%n record(s) changed. Undo reverses it.", nullptr, int(affected)),
                QStringLiteral("success"));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("affected"), affected}};
}

QVariantMap BaseService::previewSql(const QString &sql, const QVariantMap &parameters, int limit) const
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::SqlInfo info = Engine::inspectSql(*f, sql);
    if (!info.ok) {
        return failed(info.error);
    }
    if (!info.readOnly) {
        return failed(info.schema ? tr("Schema changes go through the plan on the left, where they are previewed and "
                                       "backed up.")
                                  : tr("This statement changes data: save it as a query and run it, so you see how "
                                       "many records it changes first."));
    }
    for (const Engine::QueryParameter &p : info.parameters) {
        if (!parameters.contains(p.name)) {
            return {{QStringLiteral("ok"), false},
                    {QStringLiteral("needsParameters"), true},
                    {QStringLiteral("parameters"), parameterList(info.parameters)}};
        }
    }
    const Engine::QueryResult result = Engine::runSelect(*f, sql, info.parameters, parameters, limit);
    if (!result.ok) {
        return failed(result.error);
    }
    const Engine::CurrencyFormat currency = f->currency();
    QStringList columns;
    for (const Engine::OutputColumn &c : result.columns) {
        columns.append(c.name);
    }
    QVariantList rows;
    for (const QVariantList &row : result.rows) {
        QVariantMap cells;
        for (int i = 0; i < result.columns.size(); ++i) {
            cells.insert(QStringLiteral("c%1").arg(i), Engine::displayText(result.columns.at(i).type, row.value(i), currency));
        }
        rows.append(cells);
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("columns"), columns},
            {QStringLiteral("rows"), rows},
            {QStringLiteral("truncated"), result.truncated}};
}

} // namespace QindaOffice::Apps::Base
