// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService: CSV, XLSX and (M80) Access XML export, compact & repair,
// integrity, backups.
// Importing is base_service_import.cpp; file fields base_service_files.cpp.
#include "base_csv.h"
#include "base_document.h"
#include "base_objects.h"
#include "base_schema.h"
#include "base_service.h"
#include "base_xlsx_export.h"
#include "base_xml_export.h"

#include <QDir>
#include <QFileInfo>
#include <QUndoStack>

namespace QindaOffice::Apps::Base {

namespace Engine = ::QindaOffice::Base;

namespace {

QVariantMap failed(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), error}};
}

QString localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

} // namespace

QVariantMap BaseService::exportObject(const QString &kind, const QString &name, const QUrl &url, const QString &format)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    QList<Engine::ColumnInfo> columns;
    QList<QVariantList> rows;
    QStringList header;
    QList<QStringList> textRows;
    // What the datasheet shows (filters, sort, parameters) is what exports.
    const bool shown = m_datasheet->sourceName() == name && m_datasheet->sourceKind() == kind;
    if (shown) {
        columns = m_datasheet->columnInfo();
        rows = m_datasheet->storageRows();
        header = m_datasheet->exportHeader();
        textRows = m_datasheet->exportRows();
    } else if (kind == QLatin1String("table")) {
        DatasheetModel scratch;
        scratch.setDocument(m_document);
        if (!scratch.openTable(name)) {
            return failed(scratch.error());
        }
        columns = scratch.columnInfo();
        rows = scratch.storageRows();
        header = scratch.exportHeader();
        textRows = scratch.exportRows();
    } else {
        return failed(tr("Open the query first; its datasheet is what gets exported."));
    }
    const QString path = localPath(url);
    Engine::Result r;
    if (format == QLatin1String("xlsx")) {
        r = writeXlsx(path, name, columns, rows, f->currency());
    } else if (format == QLatin1String("xml")) {
        // M80 (D-089 §8): Access's XML pair — the data file and its schema
        // beside it, named alike (base_xml_export.h).
        const QFileInfo target(path);
        const QString schemaName = target.completeBaseName() + QStringLiteral(".xsd");
        const std::optional<Engine::TableDef> design =
            kind == QLatin1String("table") ? Engine::readTable(*f, name) : std::nullopt;
        const Engine::AccessXml xml =
            Engine::accessXml(name, columns, rows, f->currency(), design ? &*design : nullptr, schemaName);
        r = Engine::writeFileAtomically(path, xml.data);
        if (r.ok) {
            r = Engine::writeFileAtomically(target.dir().filePath(schemaName), xml.schema);
        }
    } else {
        r = Engine::writeFileAtomically(path, Engine::csvBytes(header, textRows));
    }
    if (!r.ok) {
        return failed(r.error);
    }
    emit notify(tr("Exported"), tr("%1 record(s) written to %2.").arg(rows.size()).arg(QFileInfo(path).fileName()),
                QStringLiteral("success"));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("rows"), rows.size()}, {QStringLiteral("path"), path}};
}

QVariantMap BaseService::compactAndRepair()
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const qint64 before = QFileInfo(f->path()).size();
    QStringList problems;
    const Engine::Result r = f->compact(&problems);
    if (!r.ok) {
        return failed(r.error);
    }
    // AGENT-NOTE: VACUUM may renumber the rowids of tables without an
    // INTEGER PRIMARY KEY, which the recorded undo steps address — so the
    // history ends here (the user is told).
    m_document->undoStack()->clear();
    m_datasheet->refresh();
    const qint64 after = QFileInfo(f->path()).size();
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("problems"), problems},
            {QStringLiteral("before"), before},
            {QStringLiteral("after"), after}};
}

QVariantMap BaseService::checkIntegrity() const
{
    if (file() == nullptr) {
        return failed(tr("No database is open."));
    }
    const QStringList problems = file()->integrityProblems(true);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("problems"), problems}};
}

QVariantMap BaseService::backupNow()
{
    if (file() == nullptr) {
        return failed(tr("No database is open."));
    }
    QString error;
    const QString path = file()->automaticBackup(QStringLiteral("backup"), &error);
    if (path.isEmpty()) {
        return failed(error);
    }
    emit notify(tr("Backed up"), tr("A copy was saved as %1.").arg(path), QStringLiteral("success"));
    return {{QStringLiteral("ok"), true}, {QStringLiteral("path"), path}};
}

} // namespace QindaOffice::Apps::Base
