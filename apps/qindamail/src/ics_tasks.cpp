// SPDX-License-Identifier: GPL-3.0-or-later
#include "ics_tasks.h"

#include <QStringList>

namespace QindaOffice::Apps::Mail {

namespace {

QString unescape(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c == QLatin1Char('\\') && i + 1 < value.size()) {
            const QChar next = value.at(++i);
            out += (next == QLatin1Char('n') || next == QLatin1Char('N')) ? QLatin1Char('\n') : next;
        } else {
            out += c;
        }
    }
    return out;
}

} // namespace

QList<IcsTask> parseIcsTasks(const QByteArray &ics)
{
    // RFC 5545 §3.1: a line starting with a space or tab continues the last.
    QStringList lines;
    for (QString line : QString::fromUtf8(ics).split(QLatin1Char('\n'))) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if ((line.startsWith(QLatin1Char(' ')) || line.startsWith(QLatin1Char('\t'))) && !lines.isEmpty()) {
            lines.last() += line.mid(1);
        } else {
            lines.append(line);
        }
    }
    QList<IcsTask> tasks;
    IcsTask current;
    bool inTodo = false;
    for (const QString &line : lines) {
        if (line.compare(QLatin1String("BEGIN:VTODO"), Qt::CaseInsensitive) == 0) {
            inTodo = true;
            current = IcsTask();
            continue;
        }
        if (line.compare(QLatin1String("END:VTODO"), Qt::CaseInsensitive) == 0) {
            if (inTodo && !current.title.isEmpty()) {
                tasks.append(current);
            }
            inTodo = false;
            continue;
        }
        if (!inTodo) {
            continue;
        }
        const qsizetype colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0) {
            continue;
        }
        const QString name = line.left(colon).section(QLatin1Char(';'), 0, 0).toUpper();
        const QString value = line.mid(colon + 1);
        if (name == QLatin1String("SUMMARY")) {
            current.title = unescape(value).trimmed();
        } else if (name == QLatin1String("DESCRIPTION")) {
            current.description = unescape(value).trimmed();
        } else if (name == QLatin1String("DUE")) {
            current.due = QDate::fromString(value.left(8), QStringLiteral("yyyyMMdd"));
        } else if (name == QLatin1String("PRIORITY")) {
            const int p = value.toInt();
            current.priority = p == 0 ? QString() : p <= 4 ? QStringLiteral("High")
                             : p == 5 ? QStringLiteral("Normal") : QStringLiteral("Low");
        } else if (name == QLatin1String("STATUS")) {
            current.done = value.compare(QLatin1String("COMPLETED"), Qt::CaseInsensitive) == 0;
        }
    }
    return tasks;
}

} // namespace QindaOffice::Apps::Mail
