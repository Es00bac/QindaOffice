// SPDX-License-Identifier: GPL-3.0-or-later
// BaseService: importing — text/CSV and Excel through one plan (proposal,
// then one undo step), and Access databases through mdb-tools with their
// design, relationships and select queries (M49, completed in M65).
#include "base_access_import.h"
#include "base_csv.h"
#include "base_document.h"
#include "base_service.h"
#include "base_xlsx_import.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

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

// The plan with each column's header and first values, and the first rows
// (the dialog's preview).
QVariantMap proposal(const Engine::BaseFile &file, const Engine::CsvTable &table, const QString &wanted)
{
    const Engine::CsvImportPlan plan = Engine::proposeImport(file, table, wanted);
    QVariantMap planMap = plan.toMap();
    QVariantList columns = planMap.value(QStringLiteral("columns")).toList();
    for (int i = 0; i < columns.size(); ++i) {
        QVariantMap c = columns.at(i).toMap();
        QStringList samples;
        for (int r = 0; r < table.rows.size() && samples.size() < 3; ++r) {
            const QString v = table.rows.at(r).value(i);
            if (!v.isEmpty()) {
                samples.append(v);
            }
        }
        c.insert(QStringLiteral("header"), table.header.value(i));
        c.insert(QStringLiteral("samples"), samples.join(QStringLiteral(" · ")));
        columns[i] = c;
    }
    planMap.insert(QStringLiteral("columns"), columns);
    QVariantList preview;
    for (int r = 0; r < table.rows.size() && r < 8; ++r) {
        preview.append(table.rows.at(r));
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("plan"), planMap},
            {QStringLiteral("header"), table.header},
            {QStringLiteral("preview"), preview},
            {QStringLiteral("rowCount"), table.rows.size()}};
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
    QVariantMap out = proposal(*f, *csv, target);
    out.insert(QStringLiteral("delimiter"), csv->delimiter == QLatin1Char('\t') ? QStringLiteral("tab")
                                                                                : QString(csv->delimiter));
    return out;
}

QVariantMap BaseService::xlsxProposal(const QUrl &url, const QString &sheet, const QString &table, bool headerRow)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    QString error;
    const std::optional<XlsxTable> read = readXlsxTable(localPath(url), sheet, headerRow, f->currency(), &error);
    if (!read) {
        return failed(error);
    }
    const QString wanted = read->sheets.size() > 1 ? read->sheet : QFileInfo(localPath(url)).completeBaseName();
    QVariantMap out = proposal(*f, read->table, table.trimmed().isEmpty() ? tableNameFor(*f, wanted) : table.trimmed());
    out.insert(QStringLiteral("sheets"), read->sheets);
    out.insert(QStringLiteral("sheet"), read->sheet);
    return out;
}

QVariantMap BaseService::importCsv(const QUrl &url, const QVariantMap &plan, const QString &delimiter, bool headerRow)
{
    QString error;
    const std::optional<Engine::CsvTable> csv = readCsv(url, delimiter, headerRow, &error);
    if (!csv) {
        return failed(error);
    }
    return importTable(*csv, plan, QFileInfo(localPath(url)).fileName());
}

QVariantMap BaseService::importXlsx(const QUrl &url, const QVariantMap &plan, const QString &sheet, bool headerRow)
{
    if (file() == nullptr) {
        return failed(tr("No database is open."));
    }
    QString error;
    const std::optional<XlsxTable> read = readXlsxTable(localPath(url), sheet, headerRow, file()->currency(), &error);
    if (!read) {
        return failed(error);
    }
    return importTable(read->table, plan, QFileInfo(localPath(url)).fileName());
}

QVariantMap BaseService::importTable(const Engine::CsvTable &table, const QVariantMap &plan, const QString &source)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::CsvImportPlan importPlan = Engine::CsvImportPlan::fromMap(plan);
    Engine::Change schema;
    Engine::Change data;
    const Engine::ImportResult result = Engine::importCsv(*f, table, importPlan, &schema, &data);
    if (!result.ok) {
        return failed(result.error);
    }
    m_document->pushChanges(tr("Import %1").arg(source), {schema, data});
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

QVariantMap BaseService::accessTools() const
{
    const Engine::AccessTools tools = Engine::findAccessTools();
    return {{QStringLiteral("available"), tools.available},
            {QStringLiteral("queries"), !tools.queriesProgram.isEmpty()},
            {QStringLiteral("schema"), !tools.schemaProgram.isEmpty()},
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

QVariantMap BaseService::importAccess(const QUrl &url, const QStringList &tables, bool queries)
{
    Engine::BaseFile *f = file();
    if (f == nullptr) {
        return failed(tr("No database is open."));
    }
    const Engine::AccessTools tools = Engine::findAccessTools();
    if (!tools.available) {
        return failed(tools.missingText());
    }
    Engine::MdbToolsReader reader(tools, localPath(url));
    QList<Engine::Change> changes;
    const Engine::AccessImportReport report = Engine::importAccessDatabase(*f, reader, {tables, queries}, &changes);
    if (!changes.isEmpty()) {
        m_document->pushChanges(tr("Import %1").arg(QFileInfo(localPath(url)).fileName()), changes);
    }
    return report.toMap();
}

} // namespace QindaOffice::Apps::Base
