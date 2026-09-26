// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService: CSV import (proposal, then one undo step), CSV/XLSX export,
// Access import through mdb-tools, compact & repair, integrity, backups.
#include "base_access.h"
#include "base_csv.h"
#include "base_document.h"
#include "base_objects.h"
#include "base_service.h"
#include "base_xlsx_export.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
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

std::optional<Engine::CsvTable> readCsv(const QUrl &url, const QString &delimiter, bool headerRow, QString *error)
{
    QFile file(localPath(url));
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return std::nullopt;
    }
    if (file.size() > qint64(200) * 1024 * 1024) {
        *error = QObject::tr("The file is larger than 200 MB.");
        return std::nullopt;
    }
    const QChar sep = delimiter == QLatin1String("tab") ? QLatin1Char('\t')
                      : delimiter.size() == 1           ? delimiter.at(0)
                                                        : QChar();
    return Engine::parseCsv(file.readAll(), sep, headerRow);
}

QString tableNameFor(const Engine::BaseFile &file, const QString &wanted)
{
    QString base = wanted.trimmed();
    base.remove(QRegularExpression(QStringLiteral("[\"\\[\\]`]")));
    if (base.isEmpty() || Engine::BaseFile::isSystemName(base)) {
        base = QStringLiteral("Imported");
    }
    QString name = base;
    for (int n = 2; file.tableExists(name); ++n) {
        name = QStringLiteral("%1 %2").arg(base).arg(n);
    }
    return name;
}

} // namespace

QVariantMap BaseService::csvProposal(const QUrl &url, const QString &table, const QString &delimiter, bool headerRow)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    QString error;
    const std::optional<Engine::CsvTable> csv = readCsv(url, delimiter, headerRow, &error);
    if (!csv) {
        return failed(error);
    }
    const QString target = table.trimmed().isEmpty() ? tableNameFor(*f, QFileInfo(localPath(url)).completeBaseName())
                                                     : table.trimmed();
    const Engine::CsvImportPlan plan = Engine::proposeImport(*f, *csv, target);
    QVariantMap planMap = plan.toMap();
    QVariantList columns = planMap.value(QStringLiteral("columns")).toList();
    for (int i = 0; i < columns.size(); ++i) {
        QVariantMap c = columns.at(i).toMap();
        QStringList samples;
        for (int r = 0; r < csv->rows.size() && samples.size() < 3; ++r) {
            const QString v = csv->rows.at(r).value(i);
            if (!v.isEmpty()) {
                samples.append(v);
            }
        }
        c.insert(QStringLiteral("header"), csv->header.value(i));
        c.insert(QStringLiteral("samples"), samples.join(QStringLiteral(" · ")));
        columns[i] = c;
    }
    planMap.insert(QStringLiteral("columns"), columns);
    QVariantList preview;
    for (int r = 0; r < csv->rows.size() && r < 8; ++r) {
        preview.append(csv->rows.at(r));
    }
    const QString sep = csv->delimiter == QLatin1Char('\t') ? QStringLiteral("tab") : QString(csv->delimiter);
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("plan"), planMap},
            {QStringLiteral("header"), csv->header},
            {QStringLiteral("preview"), preview},
            {QStringLiteral("rowCount"), csv->rows.size()},
            {QStringLiteral("delimiter"), sep}};
}

QVariantMap BaseService::importCsv(const QUrl &url, const QVariantMap &plan, const QString &delimiter, bool headerRow)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    QString error;
    const std::optional<Engine::CsvTable> csv = readCsv(url, delimiter, headerRow, &error);
    if (!csv) {
        return failed(error);
    }
    const Engine::CsvImportPlan importPlan = Engine::CsvImportPlan::fromMap(plan);
    Engine::Change schema;
    Engine::Change data;
    const Engine::ImportResult result = Engine::importCsv(*f, *csv, importPlan, &schema, &data);
    if (!result.ok) {
        return failed(result.error);
    }
    m_document->pushChanges(tr("Import %1").arg(QFileInfo(localPath(url)).fileName()), {schema, data});
    emit notify(tr("Import finished"),
                result.skipped > 0 ? tr("%1 record(s) imported into %2, %3 skipped.")
                                         .arg(result.imported)
                                         .arg(importPlan.table)
                                         .arg(result.skipped)
                                   : tr("%1 record(s) imported into %2.").arg(result.imported).arg(importPlan.table),
                result.skipped > 0 ? QStringLiteral("warning") : QStringLiteral("success"));
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("table"), importPlan.table},
            {QStringLiteral("imported"), result.imported},
            {QStringLiteral("skipped"), result.skipped},
            {QStringLiteral("problems"), result.problems}};
}

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

QVariantMap BaseService::accessTools() const
{
    const Engine::AccessTools tools = Engine::findAccessTools();
    return {{QStringLiteral("available"), tools.available},
            {QStringLiteral("message"), tools.available ? QString() : tools.missingText()}};
}

QVariantMap BaseService::accessTables(const QUrl &url) const
{
    QString error;
    const QStringList tables = Engine::accessTableNames(Engine::findAccessTools(), localPath(url), &error);
    if (!error.isEmpty()) {
        return failed(error);
    }
    return {{QStringLiteral("ok"), true}, {QStringLiteral("tables"), tables}};
}

QVariantMap BaseService::importAccess(const QUrl &url, const QStringList &tables)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::AccessTools tools = Engine::findAccessTools();
    QList<Engine::Change> changes;
    QVariantList imported;
    QStringList errors;
    for (const QString &table : tables) {
        QString error;
        const std::optional<Engine::CsvTable> csv = Engine::exportAccessTable(tools, localPath(url), table, &error);
        if (!csv) {
            errors.append(QStringLiteral("%1: %2").arg(table, error));
            continue;
        }
        const Engine::CsvImportPlan plan = Engine::proposeImport(*f, *csv, tableNameFor(*f, table));
        Engine::Change schema;
        Engine::Change data;
        const Engine::ImportResult result = Engine::importCsv(*f, *csv, plan, &schema, &data);
        if (!result.ok) {
            errors.append(QStringLiteral("%1: %2").arg(table, result.error));
            continue;
        }
        changes << schema << data;
        imported.append(QVariantMap{{QStringLiteral("table"), plan.table}, {QStringLiteral("rows"), result.imported}});
    }
    m_document->pushChanges(tr("Import %1").arg(QFileInfo(localPath(url)).fileName()), changes);
    return {{QStringLiteral("ok"), errors.isEmpty()},
            {QStringLiteral("imported"), imported},
            {QStringLiteral("error"), errors.join(QLatin1Char('\n'))}};
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
