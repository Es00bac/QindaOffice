// SPDX-License-Identifier: GPL-3.0-or-later
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
    QStringList names;
    for (const QString &line : QString::fromUtf8(*out).split(QLatin1Char('\n'))) {
        const QString name = line.trimmed();
        if (!name.isEmpty() && !name.startsWith(QLatin1String("MSys"), Qt::CaseInsensitive)) {
            names.append(name);
        }
    }
    return names;
}

std::optional<CsvTable> exportAccessTable(const AccessTools &tools, const QString &path, const QString &table,
                                          QString *error)
{
    if (!tools.available) {
        *error = tools.missingText();
        return std::nullopt;
    }
    // ISO dates, so the CSV type detection reads them as dates.
    const std::optional<QByteArray> out =
        run(tools.exportProgram, {QStringLiteral("-D"), QStringLiteral("%Y-%m-%d %H:%M:%S"), path, table}, error);
    if (!out) {
        return std::nullopt;
    }
    CsvTable csv = parseCsv(*out, QLatin1Char(','), true);
    // mdb-export writes midnight times on date-only fields; drop them so the
    // column is a date, not a date-time.
    for (int c = 0; c < csv.header.size(); ++c) {
        bool allMidnight = !csv.rows.isEmpty();
        for (const QStringList &row : std::as_const(csv.rows)) {
            const QString v = row.value(c);
            if (!v.isEmpty() && !v.endsWith(QLatin1String(" 00:00:00"))) {
                allMidnight = false;
                break;
            }
        }
        if (allMidnight) {
            for (QStringList &row : csv.rows) {
                if (!row[c].isEmpty()) {
                    row[c].chop(9);
                }
            }
        }
    }
    return csv;
}

} // namespace QindaOffice::Base
