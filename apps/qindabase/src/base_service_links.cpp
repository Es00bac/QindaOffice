// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService (M86, D-094): Access's XML import (base_xml_import.h) and
// linked tables to another .qbase, a CSV file or an Excel sheet
// (base_links.h) — linked, refreshed (at open, and on demand as one undo
// step), relinked and converted to local tables. The engine reads .qbase
// and CSV sources itself; an Excel sheet comes through calc_io here.
#include "base_document.h"
#include "base_links.h"
#include "base_schema.h"
#include "base_service.h"
#include "base_xlsx_import.h"
#include "base_xml_import.h"

#include <QFileInfo>

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

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

// The window's reader for Excel links (base_links.h LinkReader).
Engine::LinkReader excelReader(const Engine::CurrencyFormat &currency)
{
    return [currency](const Engine::LinkSource &source, const QString &path,
                      QString *error) -> std::optional<Engine::CsvTable> {
        const std::optional<XlsxTable> table = readXlsxTable(path, source.table, source.headerRow, currency, error);
        return table ? std::optional<Engine::CsvTable>(table->table) : std::nullopt;
    };
}

QVariantMap linkMap(const QString &table, const Engine::LinkSource &l)
{
    return {{s("table"), table},          {s("kind"), l.kind},         {s("source"), l.description()},
            {s("path"), l.path},          {s("refreshed"), l.refreshed}, {s("rows"), l.rows},
            {s("problem"), l.problem},    {s("sourceTable"), l.table}, {s("linked"), true}};
}

} // namespace

QVariantMap BaseService::xmlTables(const QUrl &url) const
{
    const Engine::AccessXmlDocument doc = Engine::readAccessXml(localPath(url));
    if (!doc.ok) {
        return failed(doc.error);
    }
    QVariantList tables;
    for (const Engine::XmlTable &t : doc.tables) {
        tables.append(QVariantMap{{s("name"), t.name}, {s("fields"), t.fields}, {s("records"), t.records}});
    }
    return {{s("ok"), true}, {s("tables"), tables}, {s("schema"), doc.schemaSource}};
}

QVariantMap BaseService::importXml(const QUrl &url, const QStringList &tables)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::AccessXmlDocument doc = Engine::readAccessXml(localPath(url));
    if (!doc.ok) {
        return failed(doc.error);
    }
    QList<Engine::Change> changes;
    const Engine::AccessImportReport report = Engine::importAccessXml(*f, doc, tables, &changes);
    if (!changes.isEmpty()) {
        m_document->pushChanges(tr("Import %1").arg(QFileInfo(localPath(url)).fileName()), changes);
        emit objectsChanged(); // the records came after the tables: recount them
    }
    return report.toMap();
}

QVariantMap BaseService::linkChoices(const QUrl &url, const QString &kind) const
{
    QString error;
    if (kind == QLatin1String("qbase")) {
        const QStringList tables = Engine::linkableTables(localPath(url), &error);
        return error.isEmpty() ? QVariantMap{{s("ok"), true}, {s("tables"), tables}} : failed(error);
    }
    if (kind == QLatin1String("xlsx")) {
        const std::optional<XlsxTable> book =
            readXlsxTable(localPath(url), QString(), true, file() != nullptr ? file()->currency() : Engine::CurrencyFormat(),
                          &error);
        return book ? QVariantMap{{s("ok"), true}, {s("sheets"), book->sheets}} : failed(error);
    }
    return {{s("ok"), true}, {s("tables"), QStringList()}};
}

QVariantMap BaseService::linkTable(const QString &name, const QVariantMap &spec)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    if (!f->hasSystemTables()) {
        return needsSystemTables();
    }
    Engine::LinkSource source;
    source.kind = spec.value(s("kind")).toString();
    source.path = localPath(spec.value(s("url")).toUrl());
    source.table = spec.value(source.kind == QLatin1String("xlsx") ? s("sheet") : s("table")).toString();
    source.delimiter = spec.value(s("delimiter")).toString();
    source.headerRow = spec.value(s("headerRow"), true).toBool();
    const Engine::LinkResult r = Engine::linkTable(*f, name.trimmed(), source, excelReader(f->currency()));
    if (!r.ok) {
        return failed(r.error);
    }
    m_document->pushChanges(tr("Link %1").arg(r.table), r.changes);
    emit objectsChanged();
    return {{s("ok"), true}, {s("name"), r.table}, {s("rows"), r.rows}};
}

QVariantList BaseService::linkedTables() const
{
    QVariantList out;
    if (file() != nullptr) {
        for (const auto &[table, link] : Engine::linkedTables(*file())) {
            out.append(linkMap(table, link));
        }
    }
    return out;
}

QVariantMap BaseService::linkInfo(const QString &table) const
{
    Engine::BaseFile *f = file();
    const std::optional<Engine::TableDef> def = f != nullptr ? Engine::readTable(*f, table) : std::nullopt;
    if (!def || def->link.isEmpty()) {
        return {{s("linked"), false}};
    }
    return linkMap(def->name, Engine::LinkSource::fromMap(def->link));
}

QVariantMap BaseService::refreshLink(const QString &table)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::LinkResult r = Engine::refreshLink(*f, table, excelReader(f->currency()), true);
    if (!r.changes.isEmpty()) {
        m_document->pushChanges(tr("Refresh %1").arg(table), r.changes);
        emit objectsChanged();
    }
    if (!r.ok) {
        return failed(r.error);
    }
    return {{s("ok"), true}, {s("rows"), r.rows}, {s("redesigned"), r.redesigned}};
}

QVariantMap BaseService::refreshAllLinks()
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    QList<Engine::Change> changes;
    QStringList problems;
    int refreshed = 0;
    for (const auto &[table, link] : Engine::linkedTables(*f)) {
        const Engine::LinkResult r = Engine::refreshLink(*f, table, excelReader(f->currency()), true);
        changes.append(r.changes);
        if (r.ok) {
            ++refreshed;
        } else {
            problems.append(tr("%1: %2").arg(table, r.error));
        }
    }
    if (!changes.isEmpty()) {
        m_document->pushChanges(tr("Refresh links"), changes);
        emit objectsChanged();
    }
    return {{s("ok"), problems.isEmpty()}, {s("refreshed"), refreshed}, {s("problems"), problems},
            {s("error"), problems.join(QLatin1Char('\n'))}};
}

QVariantMap BaseService::relinkTable(const QString &table, const QUrl &url)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::SchemaPlan plan = Engine::planRelinkTable(*f, table, localPath(url));
    Engine::Change relinked;
    const Engine::Result made = plan.ok() ? f->applyPlan(plan, &relinked) : Engine::Result::failure(plan.error);
    if (!made.ok) {
        return failed(made.error);
    }
    const Engine::LinkResult r = Engine::refreshLink(*f, table, excelReader(f->currency()), true);
    QList<Engine::Change> changes{relinked};
    changes.append(r.changes);
    m_document->pushChanges(plan.label, changes);
    emit objectsChanged();
    return r.ok ? QVariantMap{{s("ok"), true}, {s("rows"), r.rows}} : failed(r.error);
}

QVariantMap BaseService::unlinkTable(const QString &table)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::SchemaPlan plan = Engine::planUnlinkTable(*f, table);
    Engine::Change change;
    const Engine::Result made = plan.ok() ? f->applyPlan(plan, &change) : Engine::Result::failure(plan.error);
    if (!made.ok) {
        return failed(made.error);
    }
    m_document->pushChange(change);
    return {{s("ok"), true}};
}

void BaseService::refreshLinksAtOpen()
{
    m_openLinks.clear();
    Engine::BaseFile *f = file();
    if (f == nullptr || !f->hasSystemTables()) {
        return;
    }
    QStringList problems;
    for (const Engine::LinkResult &r : Engine::refreshLinks(*f, excelReader(f->currency()))) {
        m_openLinks.append(QVariantMap{{s("table"), r.table}, {s("ok"), r.ok}, {s("error"), r.error}, {s("rows"), r.rows}});
        if (!r.ok) {
            problems.append(tr("%1 shows its last copy: %2").arg(r.table, r.error));
        }
    }
    if (!problems.isEmpty()) {
        emit notify(tr("Linked tables"), problems.join(QLatin1Char('\n')), s("warning"));
    }
}

} // namespace QindaOffice::Apps::Base
