// SPDX-License-Identifier: GPL-3.0-or-later
// Reading Access files through the mdb-tools programs (base_access.h).
#include "base_access.h"

#include <QProcess>
#include <QStandardPaths>

namespace QindaOffice::Base {

namespace {

constexpr int kTimeoutMs = 120000;

std::optional<QByteArray> run(const QString &program, const QStringList &arguments, QString *error)
{
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(10000)) {
        *error = QStringLiteral("%1 could not be started.").arg(program);
        return std::nullopt;
    }
    process.closeWriteChannel();
    if (!process.waitForFinished(kTimeoutMs)) {
        process.kill();
        process.waitForFinished(2000);
        *error = QStringLiteral("%1 took too long and was stopped.").arg(program);
        return std::nullopt;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        *error = stderrText.isEmpty() ? QStringLiteral("%1 failed.").arg(program) : stderrText.left(400);
        return std::nullopt;
    }
    return process.readAllStandardOutput();
}

QStringList lines(const QByteArray &out)
{
    QStringList names;
    for (const QString &line : QString::fromUtf8(out).split(QLatin1Char('\n'))) {
        const QString name = line.trimmed();
        if (!name.isEmpty()) {
            names.append(name);
        }
    }
    return names;
}

// ISO dates (short dates as dates, the rest as date-times) and, with
// `binary`, binary columns as hex — or stripped.
QStringList exportArguments(const QString &path, const QString &table, const QString &binary)
{
    return {QStringLiteral("-b"), binary, QStringLiteral("-D"), QStringLiteral("%Y-%m-%d"), QStringLiteral("-T"),
            QStringLiteral("%Y-%m-%d %H:%M:%S"), path, table};
}

} // namespace

QString AccessTools::missingText() const
{
    return QStringLiteral("Importing Access databases needs the mdb-tools programs (mdb-tables and mdb-export), which "
                          "are not installed. On Gentoo: emerge app-office/mdbtools.");
}

AccessTools findAccessTools()
{
    AccessTools tools;
    tools.tablesProgram = QStandardPaths::findExecutable(QStringLiteral("mdb-tables"));
    tools.exportProgram = QStandardPaths::findExecutable(QStringLiteral("mdb-export"));
    tools.schemaProgram = QStandardPaths::findExecutable(QStringLiteral("mdb-schema"));
    tools.queriesProgram = QStandardPaths::findExecutable(QStringLiteral("mdb-queries"));
    tools.available = !tools.tablesProgram.isEmpty() && !tools.exportProgram.isEmpty();
    return tools;
}

QStringList accessTableNames(const AccessTools &tools, const QString &path, QString *error)
{
    if (!tools.available) {
        *error = tools.missingText();
        return {};
    }
    const std::optional<QByteArray> out = run(tools.tablesProgram, {QStringLiteral("-1"), path}, error);
    if (!out) {
        return {};
    }
    QStringList names = lines(*out);
    names.removeIf([](const QString &name) { return name.startsWith(QLatin1String("MSys"), Qt::CaseInsensitive); });
    return names;
}

std::optional<CsvTable> exportAccessTable(const AccessTools &tools, const QString &path, const QString &table,
                                          QString *error)
{
    if (!tools.available) {
        *error = tools.missingText();
        return std::nullopt;
    }
    std::optional<QByteArray> out = run(tools.exportProgram, exportArguments(path, table, QStringLiteral("hex")), error);
    if (!out && error->contains(QLatin1String("option"), Qt::CaseInsensitive)) {
        // AGENT-NOTE: mdb-tools before 1.0 has no -T/-b hex; its one -D
        // format covers every date and binary comes out raw.
        error->clear();
        out = run(tools.exportProgram, {QStringLiteral("-D"), QStringLiteral("%Y-%m-%d %H:%M:%S"), path, table}, error);
    }
    if (!out) {
        return std::nullopt;
    }
    CsvTable csv = parseCsv(*out, QLatin1Char(','), true);
    // Date-times that are all at midnight are dates (Access keeps both in
    // one Date/Time type); drop the time so the column becomes a date.
    for (int c = 0; c < csv.header.size(); ++c) {
        bool allMidnight = !csv.rows.isEmpty();
        bool any = false;
        for (const QStringList &row : std::as_const(csv.rows)) {
            const QString v = row.value(c);
            any = any || !v.isEmpty();
            if (!v.isEmpty() && !v.endsWith(QLatin1String(" 00:00:00"))) {
                allMidnight = false;
                break;
            }
        }
        if (allMidnight && any) {
            for (QStringList &row : csv.rows) {
                if (!row[c].isEmpty()) {
                    row[c].chop(9);
                }
            }
        }
    }
    return csv;
}

MdbToolsReader::MdbToolsReader(AccessTools tools, QString path)
    : m_tools(std::move(tools))
    , m_path(std::move(path))
{
}

QStringList MdbToolsReader::tables(QString *error)
{
    return accessTableNames(m_tools, m_path, error);
}

std::optional<CsvTable> MdbToolsReader::rows(const QString &table, QString *error)
{
    return exportAccessTable(m_tools, m_path, table, error);
}

QString MdbToolsReader::schema(QString *error)
{
    if (m_tools.schemaProgram.isEmpty()) {
        *error = QStringLiteral("mdb-schema is not installed");
        return {};
    }
    const std::optional<QByteArray> out = run(m_tools.schemaProgram, {m_path, QStringLiteral("postgres")}, error);
    return out ? QString::fromUtf8(*out) : QString();
}

QStringList MdbToolsReader::queries(QString *error)
{
    if (m_tools.queriesProgram.isEmpty()) {
        *error = QStringLiteral("mdb-queries is not installed");
        return {};
    }
    const std::optional<QByteArray> out =
        run(m_tools.queriesProgram, {QStringLiteral("-L"), QStringLiteral("-1"), m_path}, error);
    return out ? lines(*out) : QStringList();
}

QString MdbToolsReader::querySql(const QString &name, QString *error)
{
    const std::optional<QByteArray> out = run(m_tools.queriesProgram, {m_path, name}, error);
    return out ? QString::fromUtf8(*out).trimmed() : QString();
}

QHash<QString, int> MdbToolsReader::queryFlags(QString *error)
{
    const std::optional<QByteArray> out =
        run(m_tools.exportProgram, exportArguments(m_path, QStringLiteral("MSysObjects"), QStringLiteral("strip")), error);
    return out ? parseQueryFlags(parseCsv(*out, QLatin1Char(','), true)) : QHash<QString, int>();
}

QHash<QString, int> parseQueryFlags(const CsvTable &objects)
{
    const auto column = [&](const char *name) {
        return int(objects.header.indexOf(QString::fromLatin1(name), 0));
    };
    const int nameColumn = column("Name");
    const int typeColumn = column("Type");
    const int flagsColumn = column("Flags");
    QHash<QString, int> out;
    if (nameColumn < 0 || typeColumn < 0 || flagsColumn < 0) {
        return out;
    }
    for (const QStringList &row : objects.rows) {
        bool ok = false;
        const int flags = row.value(flagsColumn).toInt(&ok);
        if (row.value(typeColumn).trimmed() == QLatin1String("5") && ok) { // 5 = a query
            out.insert(row.value(nameColumn), flags);
        }
    }
    return out;
}

} // namespace QindaOffice::Base
