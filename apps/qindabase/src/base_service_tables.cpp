// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService: table design, relationships and the SQL console — every
// schema change planned, previewed with its SQL, then applied as one step.
#include "base_document.h"
#include "base_objects.h"
#include "base_schema.h"
#include "base_service.h"

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QVariantMap failed(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

bool needsMetadata(const Engine::TableDef &table)
{
    // M80: rules and masks live in qb_fields too (base_rules.h).
    return !table.validationRule.isEmpty() || !table.validationText.isEmpty()
           || std::any_of(table.fields.cbegin(), table.fields.cend(), [](const Engine::FieldDef &f) {
                  return !f.caption.isEmpty() || !f.description.isEmpty() || f.lookup.isSet()
                         || !f.validationRule.isEmpty() || !f.validationText.isEmpty() || !f.inputMask.isEmpty();
              });
}

// The SQL people read: the schema statements, with QindaBase's own
// bookkeeping (captions, lookups, saved objects, pragmas) summarised in a
// comment rather than listed.
QString readableSql(const Engine::SchemaPlan &plan)
{
    QStringList shown;
    int bookkeeping = 0;
    for (const QString &statement : plan.statements) {
        if (statement.startsWith(QLatin1String("DELETE FROM qb_")) || statement.startsWith(QLatin1String("INSERT OR REPLACE INTO qb_"))
            || statement.startsWith(QLatin1String("UPDATE qb_")) || statement.startsWith(QLatin1String("PRAGMA "))
            || statement.startsWith(QLatin1String("ALTER TABLE qb_"))) {
            ++bookkeeping;
        } else {
            shown.append(statement + QLatin1Char(';'));
        }
    }
    if (bookkeeping > 0) {
        shown.append(BaseService::tr("-- and %n statement(s) keeping QindaBase's captions, lookups and saved objects in step",
                                     nullptr, bookkeeping));
    }
    return shown.join(QStringLiteral("\n\n"));
}

QVariantMap planResult(const Engine::SchemaPlan &plan, const Engine::PlanPreview &preview)
{
    return {{QStringLiteral("ok"), plan.ok() && preview.ok},
            {QStringLiteral("error"), plan.ok() ? preview.error : plan.error},
            {QStringLiteral("label"), plan.label},
            {QStringLiteral("sql"), readableSql(plan)},
            {QStringLiteral("notes"), plan.notes},
            {QStringLiteral("warnings"), preview.warnings},
            {QStringLiteral("destructive"), plan.destructive},
            {QStringLiteral("empty"), plan.statements.isEmpty()}};
}

} // namespace

QVariantMap BaseService::tableDesign(const QString &name) const
{
    if (file() == nullptr) {
        return {};
    }
    const std::optional<Engine::TableDef> def = Engine::readTable(*file(), name);
    if (!def) {
        return failed(tr("There is no table named %1.").arg(name));
    }
    QVariantMap map = def->toMap();
    map.insert(QStringLiteral("ok"), true);
    map.insert(QStringLiteral("isNew"), false);
    return map;
}

QVariantMap BaseService::newTableDesign(const QString &name) const
{
    Engine::TableDef t;
    t.name = name;
    t.autoIncrement = true;
    Engine::FieldDef id;
    id.name = QStringLiteral("id");
    id.type = Engine::FieldType::AutoNumber;
    id.primaryKey = true;
    t.fields.append(id);
    QVariantMap map = t.toMap();
    map.insert(QStringLiteral("ok"), true);
    map.insert(QStringLiteral("isNew"), true);
    return map;
}

QVariantMap BaseService::planTableDesign(const QVariantMap &design)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::TableDef after = Engine::TableDef::fromMap(design);
    if (!f->hasSystemTables() && needsMetadata(after)) {
        return needsSystemTables();
    }
    Engine::SchemaPlan plan;
    if (after.originalName.isEmpty()) {
        plan = Engine::planCreateTable(*f, after);
    } else {
        const std::optional<Engine::TableDef> before = Engine::readTable(*f, after.originalName);
        if (!before) {
            return failed(tr("The table %1 no longer exists.").arg(after.originalName));
        }
        plan = Engine::planAlterTable(*f, *before, after);
        if (plan.ok() && before->name != after.name) {
            plan.statements.append(Engine::ObjectStore::tableRenameStatements(*f, before->name, after.name));
        }
    }
    const Engine::PlanPreview preview = plan.ok() ? f->previewPlan(plan) : Engine::PlanPreview{};
    return planResult(plan, preview);
}

QVariantMap BaseService::applyTableDesign(const QVariantMap &design)
{
    const QVariantMap planned = planTableDesign(design);
    if (!planned.value(QStringLiteral("ok")).toBool()) {
        return planned;
    }
    Engine::BaseFile *f = file();
    const Engine::TableDef after = Engine::TableDef::fromMap(design);
    Engine::SchemaPlan plan;
    if (after.originalName.isEmpty()) {
        plan = Engine::planCreateTable(*f, after);
    } else {
        const std::optional<Engine::TableDef> before = Engine::readTable(*f, after.originalName);
        plan = Engine::planAlterTable(*f, *before, after);
        if (before->name != after.name) {
            plan.statements.append(Engine::ObjectStore::tableRenameStatements(*f, before->name, after.name));
        }
    }
    if (plan.statements.isEmpty()) {
        return {{QStringLiteral("ok"), true}, {QStringLiteral("name"), after.name}, {QStringLiteral("unchanged"), true}};
    }
    Engine::Change change;
    QStringList warnings;
    const Engine::Result r = f->applyPlan(plan, &change, &warnings);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    emit notify(plan.label, warnings.isEmpty() ? tr("Saved.") : warnings.join(QLatin1Char(' ')),
                warnings.isEmpty() ? QStringLiteral("success") : QStringLiteral("warning"));
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("name"), after.name},
            {QStringLiteral("warnings"), warnings},
            {QStringLiteral("backupPath"), change.backupPath}};
}

QVariantMap BaseService::quickTable(const QString &name)
{
    QVariantMap design = newTableDesign(name.trimmed());
    QVariantList fields = design.value(QStringLiteral("fields")).toList();
    Engine::FieldDef label;
    label.name = QStringLiteral("name");
    label.type = Engine::FieldType::Text;
    fields.append(label.toMap());
    design.insert(QStringLiteral("fields"), fields);
    return applyTableDesign(design);
}

QVariantList BaseService::tableFields(const QString &table) const
{
    QVariantList out;
    if (file() == nullptr) {
        return out;
    }
    if (const std::optional<Engine::TableDef> def = Engine::readTable(*file(), table)) {
        for (const Engine::FieldDef &f : def->fields) {
            out.append(f.toMap());
        }
    }
    return out;
}

QVariantList BaseService::relationships() const
{
    QVariantList out;
    if (file() == nullptr) {
        return out;
    }
    for (const Engine::Relationship &r : Engine::readRelationships(*file())) {
        QVariantMap m = r.toMap();
        m.insert(QStringLiteral("label"),
                 QStringLiteral("%1.%2 → %3.%4").arg(r.childTable, r.childField, r.parentTable, r.parentField));
        out.append(m);
    }
    return out;
}

QVariantMap BaseService::planRelationship(const QVariantMap &relationship, bool add)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::Relationship r = Engine::Relationship::fromMap(relationship);
    const Engine::SchemaPlan plan = add ? Engine::planAddRelationship(*f, r) : Engine::planRemoveRelationship(*f, r);
    const Engine::PlanPreview preview = plan.ok() ? f->previewPlan(plan) : Engine::PlanPreview{};
    return planResult(plan, preview);
}

QVariantMap BaseService::applyRelationship(const QVariantMap &relationship, bool add)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::Relationship r = Engine::Relationship::fromMap(relationship);
    const Engine::SchemaPlan plan = add ? Engine::planAddRelationship(*f, r) : Engine::planRemoveRelationship(*f, r);
    Engine::Change change;
    const Engine::Result applied = f->applyPlan(plan, &change);
    if (!applied.ok) {
        return failed(applied.error);
    }
    m_document->pushChange(change);
    return {{QStringLiteral("ok"), true}};
}

QVariantMap BaseService::planSql(const QStringList &statements)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    Engine::SchemaPlan plan;
    plan.label = tr("SQL change");
    for (const QString &s : statements) {
        QString error;
        const QStringList parts = Engine::splitStatements(s, &error);
        if (!error.isEmpty()) {
            return failed(error);
        }
        plan.statements.append(parts);
    }
    for (const QString &s : std::as_const(plan.statements)) {
        const QString keyword = Engine::firstKeyword(s);
        if (keyword == QLatin1String("DROP") || s.contains(QLatin1String("DROP COLUMN"), Qt::CaseInsensitive)) {
            plan.destructive = true;
        }
        if (QStringList{QStringLiteral("PRAGMA"), QStringLiteral("ATTACH"), QStringLiteral("DETACH"),
                        QStringLiteral("BEGIN"), QStringLiteral("COMMIT"), QStringLiteral("ROLLBACK"),
                        QStringLiteral("VACUUM")}
                .contains(keyword)) {
            return failed(tr("%1 statements are not allowed here.").arg(keyword));
        }
    }
    if (plan.statements.isEmpty()) {
        return failed(tr("Write the SQL to run."));
    }
    const Engine::PlanPreview preview = f->previewPlan(plan);
    return planResult(plan, preview);
}

QVariantMap BaseService::applySql(const QStringList &statements)
{
    const QVariantMap planned = planSql(statements);
    if (!planned.value(QStringLiteral("ok")).toBool()) {
        return planned;
    }
    Engine::SchemaPlan plan;
    plan.label = tr("SQL change");
    for (const QString &s : statements) {
        plan.statements.append(Engine::splitStatements(s));
    }
    plan.destructive = planned.value(QStringLiteral("destructive")).toBool();
    Engine::Change change;
    const Engine::Result r = file()->applyPlan(plan, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("backupPath"), change.backupPath}};
}

} // namespace QindaOffice::Apps::Base
