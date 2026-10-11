// SPDX-License-Identifier: GPL-3.0-or-later
// QuickBooks IIF text → records and transaction blocks (iif_reader.h).
#include "iif_reader.h"

#include "csv_util.h"

#include <QRegularExpression>

namespace QindaOffice::Books::IifReader {

namespace {

QString unquote(const QString &field)
{
    if (field.size() >= 2 && field.startsWith(QLatin1Char('"')) && field.endsWith(QLatin1Char('"'))) {
        return field.mid(1, field.size() - 2).replace(QStringLiteral("\"\""), QStringLiteral("\""));
    }
    return field;
}

void finish(IifDocument &doc, std::optional<IifTransaction> &open, const QString &why)
{
    if (!open) {
        return;
    }
    if (!why.isEmpty()) {
        open->error = why;
    }
    doc.transactions << *open;
    open.reset();
}

} // namespace

QStringList splitLine(const QString &line)
{
    const QStringList raw = line.split(QLatin1Char('\t'));
    QStringList out;
    for (qsizetype i = 0; i < raw.size(); ++i) {
        QString field = raw.at(i);
        // AGENT-NOTE: a quoted value may hold a tab; glue the pieces back
        // until the quote closes.
        if (field.startsWith(QLatin1Char('"')) && (field.size() < 2 || !field.endsWith(QLatin1Char('"')))) {
            while (i + 1 < raw.size() && !field.endsWith(QLatin1Char('"'))) {
                field += QLatin1Char('\t') + raw.at(++i);
            }
        }
        out << unquote(field);
    }
    return out;
}

QDate parseDate(const QString &text)
{
    const QString t = text.trimmed();
    static const QRegularExpression us(QStringLiteral("^(\\d{1,2})/(\\d{1,2})/(\\d{2}|\\d{4})$"));
    static const QRegularExpression iso(QStringLiteral("^(\\d{4})-(\\d{1,2})-(\\d{1,2})$"));
    if (const QRegularExpressionMatch m = us.match(t); m.hasMatch()) {
        int year = m.captured(3).toInt();
        if (m.captured(3).size() == 2) {
            year += year < 70 ? 2000 : 1900;
        }
        return QDate(year, m.captured(1).toInt(), m.captured(2).toInt());
    }
    if (const QRegularExpressionMatch m = iso.match(t); m.hasMatch()) {
        return QDate(m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt());
    }
    return {};
}

QString normalizedType(const QString &text)
{
    QString t = text.trimmed().toUpper();
    t.remove(QRegularExpression(QStringLiteral("[\\s_\\-/]")));
    return t;
}

IifDocument parse(const QByteArray &bytes)
{
    IifDocument doc;
    const QString text = Csv::decode(bytes);
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    QHash<QString, QStringList> headers; // type → column names (after the type column)
    std::optional<IifTransaction> open;
    for (int n = 0; n < lines.size(); ++n) {
        const QString &line = lines.at(n);
        if (line.trimmed().isEmpty()) {
            continue;
        }
        const QStringList fields = splitLine(line);
        const QString first = fields.value(0).trimmed().toUpper();
        const int lineNo = n + 1;
        if (first.startsWith(QLatin1Char('!'))) {
            QStringList names;
            for (qsizetype i = 1; i < fields.size(); ++i) {
                names << fields.at(i).trimmed().toUpper();
            }
            headers.insert(first.mid(1), names);
            continue;
        }
        IifRecord record;
        record.type = first;
        record.line = lineNo;
        if (first != QLatin1String("ENDTRNS")) {
            // AGENT-NOTE: Intuit's Pro Timer IIF instructions show a
            // !TIMEACCT header followed by TIMEACT data rows. Other exports
            // use !TIMEACT; both name the same fields.
            const QString header = first == QLatin1String("TIMEACT") && !headers.contains(first)
                                       && headers.contains(QStringLiteral("TIMEACCT"))
                ? QStringLiteral("TIMEACCT") : first;
            if (!headers.contains(header)) {
                doc.problems << QStringLiteral("line %1: a %2 row before any !%2 header").arg(lineNo).arg(first);
                continue;
            }
            const QStringList &names = headers.value(header);
            for (qsizetype i = 0; i < names.size(); ++i) {
                if (!names.at(i).isEmpty()) {
                    record.fields.insert(names.at(i), fields.value(i + 1));
                }
            }
        }
        if (first == QLatin1String("TRNS")) {
            finish(doc, open, QStringLiteral("the transaction has no ENDTRNS row"));
            open = IifTransaction{record, {}, lineNo, lineNo, {}};
        } else if (first == QLatin1String("SPL")) {
            if (!open) {
                doc.problems << QStringLiteral("line %1: an SPL row outside a transaction").arg(lineNo);
                continue;
            }
            open->splits << record;
            open->lastLine = lineNo;
        } else if (first == QLatin1String("ENDTRNS")) {
            if (!open) {
                doc.problems << QStringLiteral("line %1: ENDTRNS without a TRNS row").arg(lineNo);
                continue;
            }
            open->lastLine = lineNo;
            finish(doc, open, {});
        } else {
            finish(doc, open, QStringLiteral("the transaction has no ENDTRNS row"));
            doc.lists << record;
        }
    }
    finish(doc, open, QStringLiteral("the transaction has no ENDTRNS row"));
    return doc;
}

} // namespace QindaOffice::Books::IifReader
