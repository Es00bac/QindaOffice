// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService (M80, D-089 §1–4): the query designer's Access SQL view,
// make-table runs as one schema step, Table Design's Test Validation Rules,
// and the catalogs those views pick from (input masks, parameter types).
#include "base_access_sql.h"
#include "base_document.h"
#include "base_input_mask.h"
#include "base_query.h"
#include "base_rules.h"
#include "base_schema.h"
#include "base_service.h"

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QVariantMap failed(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

QString s(const char *text)
{
    return QString::fromLatin1(text);
}

// A schema plan as the plan dialog shows it (PlanDialog.qml).
QVariantMap planMap(const Engine::SchemaPlan &plan, const Engine::PlanPreview &preview)
{
    QStringList shown;
    for (const QString &statement : plan.statements) {
        if (!statement.startsWith(QLatin1String("DELETE FROM qb_")) && !statement.startsWith(QLatin1String("INSERT OR REPLACE INTO qb_"))
            && !statement.startsWith(QLatin1String("ALTER TABLE qb_"))) {
            shown.append(statement + QLatin1Char(';'));
        }
    }
    return {{s("ok"), plan.ok() && preview.ok},
            {s("error"), plan.ok() ? preview.error : plan.error},
            {s("label"), plan.label},
            {s("sql"), shown.join(s("\n\n"))},
            {s("notes"), plan.notes},
            {s("warnings"), preview.warnings},
            {s("destructive"), plan.destructive},
            {s("empty"), plan.statements.isEmpty()}};
}

} // namespace

QVariantList BaseService::inputMaskPresets() const
{
    return Engine::inputMaskPresets();
}

QVariantList BaseService::parameterTypes() const
{
    QVariantList out;
    for (const Engine::FieldType t : {Engine::FieldType::Text, Engine::FieldType::LongText, Engine::FieldType::Integer,
                                      Engine::FieldType::Number, Engine::FieldType::Currency, Engine::FieldType::YesNo,
                                      Engine::FieldType::Date, Engine::FieldType::DateTime}) {
        out.append(QVariantMap{{s("id"), Engine::typeId(t)}, {s("label"), Engine::typeLabel(t)}});
    }
    return out;
}

QVariantMap BaseService::accessSql(const QVariantMap &design) const
{
    return {{s("ok"), true}, {s("sql"), Engine::accessSql(Engine::QueryDesign::fromMap(design))}};
}

QVariantMap BaseService::parseAccessSql(const QString &sql) const
{
    if (file() == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::AccessSqlParse parse = Engine::parseAccessSql(*file(), sql);
    if (!parse.ok) {
        return failed(parse.error);
    }
    return {{s("ok"), true}, {s("design"), parse.design.toMap()}};
}

QVariantMap BaseService::makeTablePlan(const Engine::QueryDesign &design, const QVariantMap &parameters)
{
    Engine::BaseFile *f = file();
    const Engine::CompiledQuery c = Engine::compileQuery(*f, design, parameters);
    if (!c.ok) {
        return failed(c.error);
    }
    qint64 rows = 0;
    const Engine::SchemaPlan plan = Engine::planMakeTable(*f, c, parameters, &rows);
    const Engine::PlanPreview preview = plan.ok() ? f->previewPlan(plan) : Engine::PlanPreview{};
    if (!plan.ok() || !preview.ok) {
        return failed(plan.ok() ? preview.error : plan.error);
    }
    return {{s("ok"), true},
            {s("action"), true},
            {s("makeTable"), c.makeTable},
            {s("affected"), rows},
            {s("plan"), planMap(plan, preview)},
            {s("keyword"), s("CREATE")}};
}

QVariantMap BaseService::runMakeTable(const QVariantMap &design, const QVariantMap &parameters)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::CompiledQuery c = Engine::compileQuery(*f, Engine::QueryDesign::fromMap(design), parameters);
    if (!c.ok || c.makeTable.isEmpty()) {
        return failed(c.ok ? tr("This is not a make-table query.") : c.error);
    }
    qint64 rows = 0;
    const Engine::SchemaPlan plan = Engine::planMakeTable(*f, c, parameters, &rows);
    Engine::Change change;
    const Engine::Result r = f->applyPlan(plan, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    emit notify(tr("Table made"), tr("%1 now holds %n record(s). Undo reverses it.", nullptr, int(rows)).arg(c.makeTable),
                s("success"));
    return {{s("ok"), true}, {s("rows"), rows}, {s("name"), c.makeTable}, {s("backupPath"), change.backupPath}};
}

QVariantMap BaseService::testValidationRules(const QString &table) const
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const std::optional<Engine::TableDef> def = Engine::readTable(*f, table);
    if (!def) {
        return failed(tr("Save the table first; its rules are tested against its records."));
    }
    // Every rule counted as if it were new: what ruleRefusals would refuse.
    Engine::TableDef bare = *def;
    bare.validationRule.clear();
    for (Engine::FieldDef &field : bare.fields) {
        field.validationRule.clear();
    }
    QStringList lines;
    qint64 broken = 0;
    for (const auto &[sql, message] : Engine::ruleRefusals(bare, *def, f->currency())) {
        QString error;
        const qint64 count = f->db().scalar(sql, {}, &error).toLongLong();
        if (!error.isEmpty()) {
            lines.append(error);
            continue;
        }
        broken += count;
        if (count > 0) {
            lines.append(message.arg(count));
        }
    }
    if (!Engine::hasRules(*def)) {
        lines.append(tr("%1 has no validation rules.").arg(def->name));
    } else if (broken == 0 && lines.isEmpty()) {
        lines.append(tr("Every record of %1 meets its validation rules.").arg(def->name));
    }
    return {{s("ok"), true}, {s("lines"), lines}, {s("broken"), broken}};
}

} // namespace QindaOffice::Apps::Base
