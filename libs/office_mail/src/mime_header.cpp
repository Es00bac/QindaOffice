// SPDX-License-Identifier: GPL-3.0-or-later
#include "mime_header.h"

#include <qindatk/mail_mime.h>

#include <QList>
#include <algorithm>

namespace QindaOffice::Mail::MimeHeader {

Split splitHeaderBody(const QByteArray &raw)
{
    // AGENT-GUARD: the same rule as QindaTK::Mime::parse (mail_mime_parse.cpp):
    // a different split would number sections differently from the records
    // the toolkit providers write.
    const qsizetype crlf = raw.indexOf("\r\n\r\n");
    const qsizetype lf = raw.indexOf("\n\n");
    if (crlf < 0 && lf < 0) return {raw, {}};
    if (crlf >= 0 && (lf < 0 || crlf < lf)) return {raw.left(crlf), raw.mid(crlf + 4)};
    return {raw.left(lf), raw.mid(lf + 2)};
}

Fields parseFields(const QByteArray &head, int maxFields)
{
    Fields fields;
    QByteArray name;
    QByteArray current;
    const auto flush = [&] {
        if (!name.isEmpty() && fields.size() < maxFields)
            fields.append({name.trimmed().toLower(), current.trimmed()});
        name.clear();
        current.clear();
    };
    qsizetype start = 0;
    while (start <= head.size()) {
        qsizetype end = head.indexOf('\n', start);
        if (end < 0) end = head.size();
        QByteArray line = head.mid(start, end - start);
        start = end + 1;
        if (line.endsWith('\r')) line.chop(1);
        if (!line.isEmpty() && (line.at(0) == ' ' || line.at(0) == '\t')) {
            if (!name.isEmpty()) current += ' ' + line.trimmed();
        } else {
            flush();
            const qsizetype colon = line.indexOf(':');
            if (colon > 0 && !line.left(colon).trimmed().contains(' ')) {
                name = line.left(colon);
                current = line.mid(colon + 1);
            }
        }
        if (end >= head.size()) break;
    }
    flush();
    return fields;
}

QByteArray value(const Fields &fields, const char *name, const QByteArray &fallback)
{
    const QByteArray key(name);
    for (const auto &field : fields) {
        if (field.first == key) return field.second;
    }
    return fallback;
}

namespace {

QString percentDecode(const QString &value, const QString &charset)
{
    return QindaTK::Mime::decodeText(QByteArray::fromPercentEncoding(value.toLatin1()), charset);
}

} // namespace

Structured parseStructured(const QByteArray &raw)
{
    // RFC 2045 parameters with quoted strings, RFC 2231 extended
    // (name*=charset'lang'value) and continued (name*0*, name*1) forms.
    Structured out;
    QStringList pieces;
    QString current;
    bool quoted = false;
    bool escaped = false;
    for (const QChar c : QString::fromUtf8(raw)) {
        if (escaped) {
            current += c;
            escaped = false;
        } else if (quoted && c == QLatin1Char('\\')) {
            escaped = true;
        } else if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (!quoted && c == QLatin1Char(';')) {
            pieces.append(current.trimmed());
            current.clear();
        } else {
            current += c;
        }
    }
    pieces.append(current.trimmed());
    out.main = pieces.isEmpty() ? QString() : pieces.takeFirst().toLower();
    struct Segment {
        int index;
        bool extended;
        QString value;
    };
    QMap<QString, QList<Segment>> continued;
    for (const QString &piece : std::as_const(pieces)) {
        const qsizetype eq = piece.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;
        QString key = piece.left(eq).trimmed().toLower();
        const QString value = piece.mid(eq + 1).trimmed();
        const bool extended = key.endsWith(QLatin1Char('*'));
        if (extended) key.chop(1);
        int index = -1;
        const qsizetype star = key.indexOf(QLatin1Char('*'));
        if (star > 0) {
            bool ok = false;
            index = key.mid(star + 1).toInt(&ok);
            if (!ok || index < 0 || index > 999) continue;
            key = key.left(star);
        }
        if (continued.value(key).size() < 1000) continued[key].append({index, extended, value});
    }
    for (auto it = continued.begin(); it != continued.end(); ++it) {
        QList<Segment> segments = it.value();
        std::stable_sort(segments.begin(), segments.end(),
                         [](const Segment &a, const Segment &b) { return a.index < b.index; });
        QString charset;
        QString joined;
        bool anyExtended = false;
        for (qsizetype i = 0; i < segments.size(); ++i) {
            QString value = segments.at(i).value;
            if (segments.at(i).extended && i == 0) {
                const QStringList parts = value.split(QLatin1Char('\''));
                if (parts.size() >= 3) {
                    charset = parts.at(0);
                    value = parts.mid(2).join(QLatin1Char('\''));
                }
            }
            if (segments.at(i).extended) {
                anyExtended = true;
                joined += percentDecode(value, charset);
            } else {
                joined += value;
            }
        }
        out.params.insert(it.key(), anyExtended ? joined : QindaTK::Mime::decodeHeaderText(joined.toUtf8()));
    }
    return out;
}

QString bareId(const QByteArray &raw)
{
    QString id = QString::fromLatin1(raw).trimmed();
    const qsizetype open = id.indexOf(QLatin1Char('<'));
    const qsizetype close = id.indexOf(QLatin1Char('>'), open + 1);
    if (open >= 0 && close > open) id = id.mid(open + 1, close - open - 1);
    return id.trimmed();
}

} // namespace QindaOffice::Mail::MimeHeader
