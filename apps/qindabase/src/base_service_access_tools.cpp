// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService (M86, D-094): Access's query wizards, data macros and the
// application log they write, hyperlink fields, the report levels Group,
// Sort and Total edits, and the AutoExec macro the window runs at open.
#include "base_data_macro.h"
#include "base_document.h"
#include "base_hyperlink.h"
#include "base_objects.h"
#include "base_query_wizards.h"
#include "base_report.h"
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

// "table.field" or {table, field} → (table, field).
QPair<QString, QString> fieldRef(const QVariant &v)
{
    if (v.metaType().id() == QMetaType::QString) {
        const QString text = v.toString();
        const qsizetype dot = text.indexOf(QLatin1Char('.'));
        return {text.left(dot), text.mid(dot + 1)};
    }
    const QVariantMap m = v.toMap();
    return {m.value(s("table")).toString(), m.value(s("field")).toString()};
}

} // namespace

QVariantList BaseService::dataMacroEvents() const
{
    QVariantList out;
    for (const QString &event : Engine::dataMacroEvents()) {
        out.append(QVariantMap{{s("id"), event}, {s("label"), Engine::dataMacroEventLabel(event)}});
    }
    return out;
}

QVariantList BaseService::dataMacroActions() const
{
    QVariantList out;
    for (const Engine::MacroActionInfo &a : Engine::dataMacroActions()) {
        out.append(a.toMap());
    }
    return out;
}

QVariantMap BaseService::queryWizard(const QString &kind, const QVariantMap &spec) const
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    Engine::WizardResult result;
    if (kind == QLatin1String("simple")) {
        Engine::SimpleQuerySpec simple;
        for (const QVariant &v : spec.value(s("fields")).toList()) {
            simple.fields.append(fieldRef(v));
        }
        simple.summary = spec.value(s("summary")).toBool();
        for (const QVariant &v : spec.value(s("totals")).toList()) {
            const QVariantMap m = v.toMap();
            simple.totals.append({m.value(s("field")).toString(), m.value(s("function"), s("sum")).toString()});
        }
        simple.countRecords = spec.value(s("countRecords")).toBool();
        simple.dateGrouping = spec.value(s("dateGrouping"), s("unique")).toString();
        result = Engine::simpleQueryWizard(*f, simple);
    } else if (kind == QLatin1String("crosstab")) {
        Engine::CrosstabWizardSpec crosstab;
        crosstab.table = spec.value(s("table")).toString();
        crosstab.rowHeadings = spec.value(s("rowHeadings")).toStringList();
        crosstab.columnHeading = spec.value(s("columnHeading")).toString();
        crosstab.dateInterval = spec.value(s("dateInterval"), s("year")).toString();
        crosstab.valueField = spec.value(s("valueField")).toString();
        crosstab.function = spec.value(s("function"), s("sum")).toString();
        crosstab.rowSums = spec.value(s("rowSums"), true).toBool();
        result = Engine::crosstabQueryWizard(*f, crosstab);
    } else if (kind == QLatin1String("duplicates")) {
        result = Engine::findDuplicatesWizard(*f, {spec.value(s("table")).toString(),
                                                   spec.value(s("duplicateFields")).toStringList(),
                                                   spec.value(s("otherFields")).toStringList()});
    } else if (kind == QLatin1String("unmatched")) {
        result = Engine::findUnmatchedWizard(*f, {spec.value(s("table")).toString(), spec.value(s("relatedTable")).toString(),
                                                  spec.value(s("field")).toString(), spec.value(s("relatedField")).toString(),
                                                  spec.value(s("showFields")).toStringList()});
    } else {
        return failed(tr("There is no query wizard \"%1\".").arg(kind));
    }
    if (!result.ok) {
        return failed(result.error);
    }
    return {{s("ok"), true}, {s("design"), result.design.toMap()}};
}

QVariantMap BaseService::dataMacroDesign(const QString &table, const QString &event) const
{
    Engine::BaseFile *f = file();
    QString error;
    const std::optional<Engine::TableDef> def = f != nullptr ? Engine::readTable(*f, table, &error) : std::nullopt;
    if (!def) {
        return failed(f == nullptr ? tr("No database is open.") : error);
    }
    if (!Engine::dataMacroEvents().contains(event)) {
        return failed(tr("There is no table event \"%1\".").arg(event));
    }
    QVariantMap map = Engine::dataMacro(*def, event).toMap();
    map.insert(s("ok"), true);
    map.insert(s("table"), def->name);
    map.insert(s("event"), event);
    map.insert(s("label"), Engine::dataMacroEventLabel(event));
    return map;
}

QVariantMap BaseService::checkDataMacro(const QString &table, const QString &event, const QVariantMap &macro) const
{
    Engine::BaseFile *f = file();
    const std::optional<Engine::TableDef> def = f != nullptr ? Engine::readTable(*f, table) : std::nullopt;
    if (!def) {
        return failed(tr("There is no table named %1.").arg(table));
    }
    const QStringList problems = Engine::checkDataMacro(*def, event, Engine::Macro::fromMap(macro), f->currency());
    return {{s("ok"), problems.isEmpty()}, {s("problems"), problems},
            {s("error"), problems.isEmpty() ? QString() : problems.first()}};
}

QVariantMap BaseService::saveDataMacro(const QString &table, const QString &event, const QVariantMap &macro)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    if (!f->hasSystemTables()) {
        return needsSystemTables();
    }
    const Engine::SchemaPlan plan = Engine::planDataMacro(*f, table, event, Engine::Macro::fromMap(macro));
    if (!plan.ok()) {
        return failed(plan.error);
    }
    Engine::Change change;
    const Engine::Result r = f->applyPlan(plan, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{s("ok"), true}, {s("label"), plan.label}};
}

QVariantMap BaseService::openApplicationLog()
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    if (!f->db().scalar(s("SELECT count(*) FROM sqlite_schema WHERE type = 'table' AND name = ?"),
                        {QString::fromLatin1(Engine::kApplicationLogTable)})
             .toInt()) {
        return failed(tr("The application log is empty: no data macro has logged anything yet."));
    }
    // AGENT-NOTE: the log is QindaBase's system table (qb_), so it opens as
    // a read-only result, newest first — the navigation pane never lists it.
    const QString sql = s("SELECT created AS \"Created\", event AS \"Event\", source_object AS \"Table\", "
                          "description AS \"Description\", category AS \"Category\" FROM %1 ORDER BY id DESC")
                            .arg(Engine::quoteIdentifier(QString::fromLatin1(Engine::kApplicationLogTable)));
    if (!m_datasheet->showQuery(tr("Application Log"), sql, {}, {}, {})) {
        return failed(m_datasheet->error());
    }
    return {{s("ok"), true}};
}

QVariantMap BaseService::hyperlinkParts(const QString &text) const
{
    const Engine::Hyperlink link = Engine::parseHyperlink(text);
    return {{s("display"), link.display}, {s("address"), link.address}, {s("subAddress"), link.subAddress},
            {s("screenTip"), link.screenTip}, {s("target"), link.target()}, {s("shown"), link.shown()}};
}

QString BaseService::hyperlinkText(const QVariantMap &parts) const
{
    Engine::Hyperlink link;
    link.display = parts.value(s("display")).toString().trimmed();
    link.address = parts.value(s("address")).toString().trimmed();
    link.subAddress = parts.value(s("subAddress")).toString().trimmed();
    link.screenTip = parts.value(s("screenTip")).toString().trimmed();
    return link.text();
}

QVariantList BaseService::reportLevels(const QVariantMap &report) const
{
    return Engine::reportLevels(report);
}

QVariantMap BaseService::reportWithLevels(const QVariantMap &report, const QVariantList &levels) const
{
    return Engine::reportWithLevels(report, levels);
}

QString BaseService::autoExecMacro() const
{
    if (file() == nullptr) {
        return {};
    }
    for (const Engine::SavedObject &o : Engine::ObjectStore::list(*file(), s("macro"))) {
        if (o.name.compare(QLatin1String("AutoExec"), Qt::CaseInsensitive) == 0) {
            return o.name;
        }
    }
    return {};
}

} // namespace QindaOffice::Apps::Base
