// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService: reports — a spec saved as an object, a snapshot of its
// source (table or saved select query, parameters asked first) and a
// QindaTK design file the report designer and LocalReportDocumentModel use.
#include "base_document.h"
#include "base_objects.h"
#include "base_records.h"
#include "base_service.h"
#include "base_snapshot.h"
#include "design_files.h"

#include <qindatk/local_data_design_document.h>
#include <qindatk/local_data_design_store.h>
#include <qindatk/local_database_provider.h>

#include <QLocale>
#include <QSet>

#include <cmath>

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

QString valueKey(const QVariant &v)
{
    return v.metaType().id() == QMetaType::Double && std::floor(v.toDouble()) == v.toDouble()
               ? QString::number(qint64(v.toDouble()))
               : v.toString();
}

QHash<QString, QString> lookupMap(const Engine::BaseFile &file, const Engine::LookupDef &lookup)
{
    QHash<QString, QString> out;
    if (lookup.isSet() && !lookup.displayField.isEmpty()) {
        for (const auto &[value, text] : Engine::Records::lookupChoices(file, lookup)) {
            out.insert(valueKey(value), text);
        }
    }
    return out;
}

} // namespace

QVariantMap BaseService::createReport(const QString &name, const QVariantMap &spec)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    if (!f->hasSystemTables()) {
        return needsSystemTables();
    }
    const QVariantMap source = spec.value(s("source")).toMap();
    const QString sourceName = source.value(s("name")).toString();
    if (sourceFields(source.value(s("kind")).toString(), sourceName).isEmpty()) {
        return failed(tr("Choose a table or query for the report."));
    }
    QVariantMap def = spec;
    def.insert(s("version"), 1);
    if (!def.contains(s("detail"))) {
        def.insert(s("detail"), true);
    }
    const QString objectName = name.trimmed().isEmpty()
                                   ? Engine::ObjectStore::uniqueName(*f, s("report"), sourceName)
                                   : name.trimmed();
    Engine::Change change;
    const Engine::Result r = Engine::ObjectStore::save(*f, s("report"), objectName, def, false, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{s("ok"), true}, {s("name"), objectName}};
}

QVariantList BaseService::sourceFields(const QString &kind, const QString &name) const
{
    QVariantList out;
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return out;
    }
    if (kind == QLatin1String("table")) {
        return tableFields(name);
    }
    const std::optional<Engine::SavedObject> o = Engine::ObjectStore::load(*f, s("query"), name);
    if (!o) {
        return out;
    }
    const Engine::CompiledQuery c = Engine::compileQuery(*f, Engine::QueryDesign::fromMap(o->definition));
    if (!c.ok || c.action) {
        return out;
    }
    // Prepared, not run: the columns are known without parameter values.
    QString error;
    const Engine::Statement statement = f->db().prepare(c.sql, &error);
    for (int i = 0; i < statement.columnCount(); ++i) {
        Engine::FieldType type = Engine::FieldType::Text;
        const QString origin = statement.originTable(i);
        if (!origin.isEmpty()) {
            type = Engine::fieldTypes(*f, origin).value(statement.originColumn(i), Engine::FieldType::Text);
        }
        for (const Engine::OutputColumn &hint : c.columns) {
            if (hint.name == statement.columnName(i) && hint.typeKnown) {
                type = hint.type;
            }
        }
        out.append(QVariantMap{{s("name"), statement.columnName(i)}, {s("type"), Engine::typeId(type)},
                               {s("typeLabel"), Engine::typeLabel(type)}});
    }
    return out;
}

QVariantList BaseService::reportParameters(const QString &name) const
{
    QVariantList out;
    Engine::BaseFile *f = file();
    const std::optional<Engine::SavedObject> o = f != nullptr ? Engine::ObjectStore::load(*f, s("report"), name)
                                                              : std::nullopt;
    if (!o) {
        return out;
    }
    const QVariantMap source = o->definition.value(s("source")).toMap();
    if (source.value(s("kind")).toString() != QLatin1String("query")) {
        return out;
    }
    const std::optional<Engine::SavedObject> q = Engine::ObjectStore::load(*f, s("query"), source.value(s("name")).toString());
    if (!q) {
        return out;
    }
    for (const Engine::QueryParameter &p :
         Engine::compileQuery(*f, Engine::QueryDesign::fromMap(q->definition)).parameters) {
        out.append(p.toMap());
    }
    return out;
}

QVariantMap BaseService::openReport(const QString &name, const QVariantMap &parameters, bool dataOnly)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const std::optional<Engine::SavedObject> o = Engine::ObjectStore::load(*f, s("report"), name);
    if (!o) {
        return failed(tr("There is no report named %1.").arg(name));
    }
    const QVariantMap def = o->definition;
    const QVariantMap source = def.value(s("source")).toMap();
    const QString kind = source.value(s("kind")).toString();
    const QString sourceName = source.value(s("name")).toString();
    QList<Engine::SnapshotColumn> columns;
    QList<QVariantList> rows;
    if (kind == QLatin1String("table")) {
        Engine::RowQuery query;
        query.limit = Engine::kSnapshotRowLimit + 1;
        const Engine::RowSet set = Engine::Records::read(*f, sourceName, query);
        if (!set.error.isEmpty()) {
            return failed(set.error);
        }
        for (const Engine::ColumnInfo &c : set.columns) {
            columns.append({c.name, c.type, lookupMap(*f, c.lookup)});
        }
        rows = set.values;
    } else {
        const std::optional<Engine::SavedObject> q = Engine::ObjectStore::load(*f, s("query"), sourceName);
        if (!q) {
            return failed(tr("The report's query %1 no longer exists.").arg(sourceName));
        }
        const Engine::CompiledQuery c = Engine::compileQuery(*f, Engine::QueryDesign::fromMap(q->definition));
        if (!c.ok || c.action) {
            return failed(c.ok ? tr("A report needs a select query.") : c.error);
        }
        const Engine::QueryResult result =
            Engine::runSelect(*f, c.sql, c.parameters, parameters, Engine::kSnapshotRowLimit + 1, c.columns);
        if (!result.ok) {
            return failed(result.error);
        }
        for (const Engine::OutputColumn &col : result.columns) {
            Engine::LookupDef lookup;
            if (!col.sourceTable.isEmpty()) {
                for (const Engine::ColumnInfo &ci : Engine::Records::columns(*f, col.sourceTable)) {
                    if (ci.name == col.sourceField) {
                        lookup = ci.lookup;
                    }
                }
            }
            columns.append({col.name, col.type, lookupMap(*f, lookup)});
        }
        rows = result.rows;
    }
    const QString snapshot = scratchPath(s("reports.sqlite"));
    bool truncated = false;
    const Engine::Result written = Engine::writeSnapshot(snapshot, sourceName, columns, rows, f->currency(), &truncated);
    if (!written.ok) {
        return failed(written.error);
    }
    if (dataOnly) {
        return {{s("ok"), true}, {s("snapshotPath"), snapshot}, {s("truncated"), truncated},
                {s("rowCount"), qMin(rows.size(), qsizetype(Engine::kSnapshotRowLimit))}};
    }
    QindaTK::LocalDatabaseProvider provider;
    provider.open(snapshot);
    static int serial = 0;
    const QString designPath = scratchPath(QStringLiteral("report-%1.qtkdata").arg(++serial));
    QString notice;
    QString problem;
    QVariantMap saved = def.value(s("design")).toMap();
    if (!saved.isEmpty()) {
        if (writeDesignFile(designPath, saved, &provider, &problem)) {
            return {{s("ok"), true},       {s("table"), sourceName},     {s("snapshotPath"), snapshot},
                    {s("designPath"), designPath}, {s("rowCount"), qMin(rows.size(), qsizetype(Engine::kSnapshotRowLimit))},
                    {s("truncated"), truncated}, {s("title"), def.value(s("title"))}};
        }
        notice = tr("The report's layout no longer matched its source (%1); it was rebuilt.").arg(problem);
    }
    // Build the toolkit design from the report's spec, with readable labels:
    // a field's caption (tables) or the column's own name (queries).
    QHash<QString, QString> captions;
    for (const QVariant &v : sourceFields(kind, sourceName)) {
        const QVariantMap f = v.toMap();
        const QString caption = f.value(s("caption")).toString();
        captions.insert(f.value(s("name")).toString(), caption.isEmpty() ? f.value(s("name")).toString() : caption);
    }
    const auto labelOf = [&](const QString &field, const QString &given) {
        return given.trimmed().isEmpty() ? captions.value(field, field) : given;
    };
    const auto totalLabel = [&](const QVariantMap &t) {
        static const QHash<QString, QString> words{{s("sum"), tr("Total %1")}, {s("average"), tr("Average %1")},
                                                   {s("countValues"), tr("Number of %1")}, {s("minimum"), tr("Lowest %1")},
                                                   {s("maximum"), tr("Highest %1")}};
        const QString field = t.value(s("field")).toString();
        return words.value(t.value(s("function"), s("sum")).toString(), s("%1")).arg(captions.value(field, field));
    };
    const auto labelTotals = [&](const QVariantList &totals) {
        QVariantList out;
        for (const QVariant &t : totals) {
            QVariantMap tm = t.toMap();
            if (tm.value(s("label")).toString().isEmpty()) {
                tm.insert(s("label"), totalLabel(tm));
            }
            out.append(tm);
        }
        return out;
    };
    QindaTK::LocalDataDesignDocument doc;
    QStringList bound;
    QVariantList labelled;
    for (const QVariant &v : def.value(s("columns")).toList()) {
        const QVariantMap m = v.toMap();
        const QString field = m.value(s("field")).toString();
        bound.append(field);
        labelled.append(QVariantMap{{s("field"), field}, {s("label"), labelOf(field, m.value(s("label")).toString())}});
    }
    QVariantList groups;
    for (const QVariant &g : def.value(s("groups")).toList()) {
        QVariantMap gm = g.toMap();
        const QString field = gm.value(s("field")).toString();
        bound.append(field);
        gm.insert(s("label"), labelOf(field, gm.value(s("label")).toString()));
        for (const QVariant &t : gm.value(s("totals")).toList()) {
            bound.append(t.toMap().value(s("field")).toString());
        }
        gm.insert(s("totals"), labelTotals(gm.value(s("totals")).toList()));
        groups.append(gm);
    }
    const QVariantList totals = labelTotals(def.value(s("totals")).toList());
    for (const QVariant &t : totals) {
        bound.append(t.toMap().value(s("field")).toString());
    }
    bound.removeDuplicates();
    bound.removeAll(QString());
    if (!doc.bindTable(&provider, sourceName, bound)) {
        return failed(doc.lastError());
    }
    doc.setReportText(s("headerText"), def.value(s("title"), o->name).toString());
    doc.setReportText(s("footerText"), def.value(s("footer")).toString());
    if (!groups.isEmpty()) {
        doc.setReportGroups(groups);
    }
    doc.setReportDetail(def.value(s("detail"), true).toBool());
    const bool imperial = QLocale().measurementSystem() != QLocale::MetricSystem;
    doc.setReportPage(imperial ? s("letter") : s("a4"), def.value(s("orientation"), s("portrait")).toString());
    // Labelled columns and report-footer totals ride in the report
    // definition itself ({field, label}; {field, function, label}).
    QVariantMap design = doc.design();
    QVariantMap reportMap = design.value(s("report")).toMap();
    if (!labelled.isEmpty()) {
        reportMap.insert(s("columns"), labelled);
    }
    if (!totals.isEmpty()) {
        reportMap.insert(s("totals"), totals);
    }
    design.insert(s("report"), reportMap);
    if (!writeDesignFile(designPath, design, &provider, &problem)) {
        return failed(problem);
    }
    return {{s("ok"), true},
            {s("table"), sourceName},
            {s("snapshotPath"), snapshot},
            {s("designPath"), designPath},
            {s("rowCount"), qMin(rows.size(), qsizetype(Engine::kSnapshotRowLimit))},
            {s("truncated"), truncated},
            {s("title"), def.value(s("title"))},
            {s("notice"), notice}};
}

QVariantMap BaseService::saveReportDesign(const QString &name, const QVariantMap &design)
{
    Engine::BaseFile *f = file();
    const std::optional<Engine::SavedObject> o =
        f != nullptr ? Engine::ObjectStore::load(*f, s("report"), name) : std::nullopt;
    if (!o) {
        return failed(tr("There is no report named %1.").arg(name));
    }
    QVariantMap def = o->definition;
    def.insert(s("design"), design);
    const QVariantMap report = design.value(s("report")).toMap();
    def.insert(s("title"), report.value(s("headerText")));
    def.insert(s("footer"), report.value(s("footerText")));
    Engine::Change change;
    const Engine::Result r = Engine::ObjectStore::save(*f, s("report"), name, def, true, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return {{s("ok"), true}};
}

} // namespace QindaOffice::Apps::Base
