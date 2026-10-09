// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService: reports — a spec saved as an object, a snapshot of its
// source (table or saved select query, parameters asked first) and a
// QindaTK design file the report designer edits; the pages themselves are
// evaluated by the engine (base_service_report_output.cpp, M80).
#include "base_document.h"
#include "base_objects.h"
#include "base_labels.h"
#include "base_records.h"
#include "base_report.h"
#include "base_service.h"
#include "base_snapshot.h"
#include "design_files.h"

#include <qindatk/local_data_design_document.h>
#include <qindatk/local_data_design_store.h>
#include <qindatk/local_database_provider.h>

#include <QLocale>
#include <QSet>

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
    if (o->definition.value(s("type")).toString() == QLatin1String("labels")) {
        for (const Engine::QueryParameter &p : Engine::labelParameters(*f, Engine::LabelSpec::fromMap(o->definition))) {
            out.append(p.toMap()); // M80: a label report asks what its query asks
        }
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
    // M80: the engine reads the records once (base_report.h); they feed the
    // pages buildReport lays out and the snapshot the designer reads.
    QString error;
    const std::optional<Engine::ReportSource> read =
        Engine::readReportSource(*f, kind, sourceName, parameters, &error, Engine::kSnapshotRowLimit);
    if (!read) {
        return failed(error);
    }
    m_reportSource = std::make_shared<Engine::ReportSource>(*read);
    QList<Engine::SnapshotColumn> columns;
    for (const Engine::ReportColumn &c : read->columns) {
        columns.append({c.name, c.type, c.lookup});
    }
    const QList<QVariantList> &rows = read->rows;
    const QString snapshot = scratchPath(s("reports.sqlite"));
    const Engine::Result written = Engine::writeSnapshot(snapshot, sourceName, columns, rows, f->currency());
    if (!written.ok) {
        return failed(written.error);
    }
    const bool truncated = read->truncated;
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
    // Money prints as money: currency columns (REAL units in the snapshot)
    // and their sums/averages/extremes carry the file's symbol and decimals.
    // AGENT-CONTRACT: QindaTK local_report_spec "format" {decimals, grouping,
    // prefix}; a toolkit without number formats ignores the key and prints
    // the plain number, so this is safe either way.
    QSet<QString> money;
    for (const Engine::SnapshotColumn &col : columns) {
        if (col.type == Engine::FieldType::Currency) {
            money.insert(col.name);
        }
    }
    const Engine::CurrencyFormat currency = f->currency();
    const QVariantMap moneyFormat{{s("decimals"), currency.decimals}, {s("grouping"), true}, {s("prefix"), currency.symbol}};
    const auto labelTotals = [&](const QVariantList &totals) {
        QVariantList out;
        for (const QVariant &t : totals) {
            QVariantMap tm = t.toMap();
            if (tm.value(s("label")).toString().isEmpty()) {
                tm.insert(s("label"), totalLabel(tm));
            }
            if (money.contains(tm.value(s("field")).toString()) && tm.value(s("function")).toString() != s("countValues")
                && !tm.contains(s("format"))) {
                tm.insert(s("format"), moneyFormat);
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
        QVariantMap column{{s("field"), field}, {s("label"), labelOf(field, m.value(s("label")).toString())}};
        if (money.contains(field)) {
            column.insert(s("format"), moneyFormat);
        }
        labelled.append(column);
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
    // M80: the wizard's sort order after the groups (QindaTK query.sort).
    QVariantList sort;
    for (const QVariant &v : def.value(s("sort")).toList()) {
        const QVariantMap m = v.toMap();
        bound.append(m.value(s("field")).toString());
        sort.append(QVariantMap{{s("field"), m.value(s("field"))}, {s("ascending"), !m.value(s("descending")).toBool()}});
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
    if (!sort.isEmpty()) {
        QVariantMap query = reportMap.value(s("query")).toMap();
        query.insert(s("sort"), sort);
        reportMap.insert(s("query"), query);
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
