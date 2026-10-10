// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService: the database, navigation objects, rename/delete, settings.
#include "base_service.h"

#include "base_document.h"
#include "base_links.h"
#include "base_objects.h"
#include "base_report.h"
#include "base_schema.h"
#include "base_templates.h"
#include "label_sheet.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QVariantMap ok(QVariantMap extra = {})
{
    extra.insert(QStringLiteral("ok"), true);
    return extra;
}

QVariantMap failed(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

QString iconFor(const QString &kind)
{
    if (kind == QLatin1String("table")) {
        return QStringLiteral("table");
    }
    if (kind == QLatin1String("query")) {
        return QStringLiteral("funnel");
    }
    if (kind == QLatin1String("form")) {
        return QStringLiteral("layout-template");
    }
    if (kind == QLatin1String("macro")) {
        return QStringLiteral("workflow"); // M80
    }
    return QStringLiteral("scroll-text");
}

} // namespace

BaseService::BaseService(BaseDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
    , m_datasheet(new DatasheetModel(this))
    , m_labels(new LabelSheet(this))
{
    m_datasheet->setDocument(document);
    connect(document, &BaseDocument::fileReplaced, this, [this] {
        bindFile();
        emit fileChanged();
        emit objectsChanged();
    });
    connect(document, &BaseDocument::opened, this, &BaseService::databaseOpened);
    connect(m_datasheet, &DatasheetModel::messageRequested, this, &BaseService::notify);
    bindFile();
}

BaseService::~BaseService() = default;

void BaseService::bindFile()
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return;
    }
    connect(f, &Engine::BaseFile::schemaChanged, this, &BaseService::objectsChanged);
    connect(f, &Engine::BaseFile::objectsChanged, this, &BaseService::objectsChanged);
    connect(f, &Engine::BaseFile::dataChanged, this, &BaseService::recordsChanged);
    refreshLinksAtOpen(); // M86 (base_links.h): a linked table follows its source from the start
}

Engine::BaseFile *BaseService::file() const
{
    return m_document != nullptr ? m_document->file() : nullptr;
}

QString BaseService::scratchPath(const QString &name) const
{
    return m_scratch.filePath(name);
}

bool BaseService::hasDatabase() const
{
    return file() != nullptr;
}

QString BaseService::databaseName() const
{
    if (file() == nullptr) {
        return {};
    }
    return m_document->isUntitled() ? tr("New database") : QFileInfo(file()->path()).completeBaseName();
}

QString BaseService::workingPath() const
{
    return file() != nullptr ? file()->path() : QString();
}

bool BaseService::hasSystemTables() const
{
    return file() != nullptr && file()->hasSystemTables();
}

bool BaseService::readOnly() const
{
    return file() != nullptr && file()->readOnly();
}

QStringList BaseService::openProblems() const
{
    return file() != nullptr ? file()->openProblems() : QStringList();
}

QVariantList BaseService::objects() const
{
    QVariantList out;
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return out;
    }
    // M86: a linked table shows a link icon and where it comes from.
    QHash<QString, QString> links;
    for (const auto &[table, link] : Engine::linkedTables(*f)) {
        links.insert(table.toLower(), link.description());
    }
    for (const QString &table : f->tableNames()) {
        const qint64 rows = f->db().scalar(QStringLiteral("SELECT count(*) FROM %1").arg(Engine::quoteIdentifier(table)))
                                .toLongLong();
        const QString link = links.value(table.toLower());
        out.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("table")},
                               {QStringLiteral("name"), table},
                               {QStringLiteral("description"), link.isEmpty() ? tr("%n record(s)", nullptr, int(rows))
                                                                              : tr("linked · %1").arg(link)},
                               {QStringLiteral("iconName"), link.isEmpty() ? iconFor(QStringLiteral("table"))
                                                                           : QStringLiteral("link")},
                               {QStringLiteral("linked"), !link.isEmpty()}});
    }
    for (const QString &kind : {QStringLiteral("query"), QStringLiteral("form"), QStringLiteral("report"),
                                QStringLiteral("macro")}) {
        for (const Engine::SavedObject &o : Engine::ObjectStore::list(*f, kind)) {
            QString description = o.definition.value(QStringLiteral("description")).toString();
            if (description.isEmpty() && kind != QLatin1String("query")) {
                description = o.definition.value(QStringLiteral("source")).toMap().value(QStringLiteral("name")).toString();
            }
            const bool labels = o.definition.value(QStringLiteral("type")).toString() == QLatin1String("labels");
            if (labels) {
                description = tr("labels · %1").arg(description); // M80: a label report
            }
            if (kind == QLatin1String("macro") && description.isEmpty()) {
                description = tr("%n action(s)", nullptr,
                                 int(o.definition.value(QStringLiteral("actions")).toList().size()));
            }
            if (kind == QLatin1String("query")) {
                const QString type = o.definition.value(QStringLiteral("kind")).toString();
                if (type != QLatin1String("select") && description.isEmpty()) {
                    description = type == QLatin1String("sql") ? tr("SQL") : tr("%1 query").arg(type);
                }
            }
            out.append(QVariantMap{{QStringLiteral("kind"), kind},
                                   {QStringLiteral("name"), o.name},
                                   {QStringLiteral("description"), description},
                                   {QStringLiteral("iconName"), labels ? QStringLiteral("tag") : iconFor(kind)}});
        }
    }
    return out;
}

QStringList BaseService::tableNames() const
{
    return file() != nullptr ? file()->tableNames() : QStringList();
}

QStringList BaseService::queryNames() const
{
    QStringList out;
    if (file() != nullptr) {
        for (const Engine::SavedObject &o : Engine::ObjectStore::list(*file(), QStringLiteral("query"))) {
            out.append(o.name);
        }
    }
    return out;
}

QVariantList BaseService::templates() const
{
    QVariantList out{QVariantMap{{QStringLiteral("id"), QString()},
                                 {QStringLiteral("name"), tr("Blank database")},
                                 {QStringLiteral("description"), tr("Start with no tables and design your own.")},
                                 {QStringLiteral("iconName"), QStringLiteral("database")}}};
    for (const Engine::TemplateInfo &t : Engine::templates()) {
        out.append(t.toMap());
    }
    return out;
}

QVariantList BaseService::fieldTypes() const
{
    QVariantList out;
    for (const Engine::FieldType t : Engine::designableTypes()) {
        out.append(QVariantMap{{QStringLiteral("id"), Engine::typeId(t)}, {QStringLiteral("label"), Engine::typeLabel(t)}});
    }
    // M86: a calculated field picks its result type in its properties.
    out.append(QVariantMap{{QStringLiteral("id"), QStringLiteral("calculated")}, {QStringLiteral("label"), tr("Calculated")}});
    return out;
}

QVariantMap BaseService::needsSystemTables() const
{
    return {{QStringLiteral("ok"), false},
            {QStringLiteral("needsSystemTables"), true},
            {QStringLiteral("error"), tr("This SQLite file has no QindaBase system tables yet. Add them to save designs, "
                                         "captions and lookups in it?")}};
}

QVariantMap BaseService::createUntitled(const QString &templateId)
{
    QString error;
    if (!m_document->createUntitled(templateId, &error)) {
        return failed(error);
    }
    return ok();
}

QVariantMap BaseService::addSystemTables()
{
    if (file() == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::Result r = file()->ensureSystemTables();
    if (!r.ok) {
        return failed(r.error);
    }
    emit objectsChanged();
    return ok();
}

QVariantMap BaseService::properties() const
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return {};
    }
    const Engine::CurrencyFormat currency = f->currency();
    const QList<QVariantList> size = f->db().rows(QStringLiteral("SELECT page_count * page_size FROM pragma_page_count(), "
                                                                 "pragma_page_size()"));
    return {{QStringLiteral("path"), m_document->isUntitled() ? QString() : f->path()},
            {QStringLiteral("currencySymbol"), currency.symbol},
            {QStringLiteral("currencyDecimals"), currency.decimals},
            {QStringLiteral("description"), f->setting(QStringLiteral("description"))},
            {QStringLiteral("created"), f->setting(QStringLiteral("created"))},
            {QStringLiteral("template"), f->setting(QStringLiteral("template"))},
            {QStringLiteral("isQBase"), f->isQBase()},
            {QStringLiteral("sizeBytes"), size.isEmpty() ? 0 : size.first().value(0).toLongLong()},
            {QStringLiteral("tables"), f->tableNames().size()},
            {QStringLiteral("backupFolder"), Engine::BaseFile::backupDirectory()}};
}

QVariantMap BaseService::setProperties(const QVariantMap &values)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    if (!f->hasSystemTables()) {
        return needsSystemTables();
    }
    const int decimals = values.value(QStringLiteral("currencyDecimals"), 2).toInt();
    if (decimals < 0 || decimals > 4) {
        return failed(tr("Currency uses 0 to 4 decimal places."));
    }
    if (decimals != f->currency().decimals) {
        // AGENT-GUARD: stored currency is minor units; changing the scale
        // would silently multiply every amount. Refuse once money exists.
        bool money = false;
        for (const QString &table : f->tableNames()) {
            const QHash<QString, Engine::FieldType> types = Engine::fieldTypes(*f, table);
            money = money || std::any_of(types.cbegin(), types.cend(),
                                         [](Engine::FieldType t) { return t == Engine::FieldType::Currency; });
        }
        if (money) {
            return failed(tr("Currency fields already hold amounts in hundredths; the number of decimals cannot "
                             "change now."));
        }
    }
    Engine::Result r = f->setSetting(QStringLiteral("currency.symbol"),
                                     values.value(QStringLiteral("currencySymbol"), QStringLiteral("$")).toString().left(8));
    if (r.ok) {
        r = f->setSetting(QStringLiteral("currency.decimals"), QString::number(decimals));
    }
    if (r.ok) {
        r = f->setSetting(QStringLiteral("description"), values.value(QStringLiteral("description")).toString());
    }
    if (!r.ok) {
        return failed(r.error);
    }
    m_datasheet->refresh();
    return ok();
}

QVariantMap BaseService::objectInfo(const QString &kind, const QString &name) const
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return {};
    }
    if (kind == QLatin1String("table")) {
        return {{QStringLiteral("kind"), kind}, {QStringLiteral("name"), name}, {QStringLiteral("exists"), f->tableExists(name)}};
    }
    const std::optional<Engine::SavedObject> o = Engine::ObjectStore::load(*f, kind, name);
    return o ? o->toMap() : QVariantMap{};
}

QVariantMap BaseService::renameObject(const QString &kind, const QString &from, const QString &to)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const QString target = to.trimmed();
    if (kind == QLatin1String("table")) {
        const std::optional<Engine::TableDef> before = Engine::readTable(*f, from);
        if (!before) {
            return failed(tr("There is no table named %1.").arg(from));
        }
        Engine::TableDef after = *before;
        after.name = target;
        Engine::SchemaPlan plan = Engine::planAlterTable(*f, *before, after);
        plan.statements.append(Engine::ObjectStore::tableRenameStatements(*f, from, target));
        Engine::Change change;
        const Engine::Result r = f->applyPlan(plan, &change);
        if (!r.ok) {
            return failed(r.error);
        }
        m_document->pushChange(change);
        return ok({{QStringLiteral("name"), target}});
    }
    Engine::Change change;
    Engine::Result r = Engine::ObjectStore::rename(*f, kind, from, target, &change);
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    if (kind == QLatin1String("query")) {
        // Reports built on the query follow it.
        for (const Engine::SavedObject &o : Engine::ObjectStore::list(*f, QStringLiteral("report"))) {
            QVariantMap def = o.definition;
            QVariantMap source = def.value(QStringLiteral("source")).toMap();
            if (source.value(QStringLiteral("kind")).toString() == QLatin1String("query")
                && source.value(QStringLiteral("name")).toString().compare(from, Qt::CaseInsensitive) == 0) {
                source.insert(QStringLiteral("name"), target);
                def.insert(QStringLiteral("source"), source);
                Engine::Change follow;
                if (Engine::ObjectStore::save(*f, QStringLiteral("report"), o.name, def, true, &follow).ok) {
                    m_document->pushChange(follow);
                }
            }
        }
    }
    return ok({{QStringLiteral("name"), target}});
}

QVariantMap BaseService::deleteObjectImpact(const QString &kind, const QString &name) const
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    QStringList lines;
    bool blocked = false;
    if (kind == QLatin1String("table")) {
        const Engine::SchemaPlan plan = Engine::planDropTable(*f, name);
        if (!plan.ok()) {
            return {{QStringLiteral("ok"), true}, {QStringLiteral("blocked"), true}, {QStringLiteral("lines"), QStringList{plan.error}}};
        }
        lines = plan.notes;
        const QStringList refs = Engine::ObjectStore::referencesTo(*f, name);
        if (!refs.isEmpty()) {
            lines.append(tr("These will no longer work: %1.").arg(refs.join(QStringLiteral(", "))));
        }
    } else {
        lines.append(tr("Deletes the %1 %2. Undo brings it back.").arg(kind, name));
        if (kind == QLatin1String("query")) {
            for (const Engine::SavedObject &o : Engine::ObjectStore::list(*f, QStringLiteral("report"))) {
                const QVariantMap source = o.definition.value(QStringLiteral("source")).toMap();
                if (source.value(QStringLiteral("kind")).toString() == QLatin1String("query")
                    && source.value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0) {
                    lines.append(tr("The report %1 uses this query.").arg(o.name));
                }
            }
        }
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("blocked"), blocked}, {QStringLiteral("lines"), lines}};
}

QVariantMap BaseService::deleteObject(const QString &kind, const QString &name)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    Engine::Change change;
    Engine::Result r;
    if (kind == QLatin1String("table")) {
        r = f->applyPlan(Engine::planDropTable(*f, name), &change);
    } else {
        r = Engine::ObjectStore::remove(*f, kind, name, &change);
    }
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChange(change);
    return ok({{QStringLiteral("backupPath"), change.backupPath}});
}

} // namespace QindaOffice::Apps::Base
