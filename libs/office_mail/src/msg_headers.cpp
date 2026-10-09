// SPDX-License-Identifier: GPL-3.0-or-later
// RFC 5322 helpers for the .msg reader (msg_headers.h).

#include "msg_headers.h"

#include <QStringDecoder>

namespace QindaOffice::Mail::MsgDetail {
namespace {

constexpr int kMaxFields = 10000;
constexpr int kMaxAddresses = 10000;
constexpr int kMaxCharsetLength = 64;

int hexValue(char16_t c)
{
    if (c >= u'0' && c <= u'9') return c - u'0';
    if (c >= u'a' && c <= u'f') return c - u'a' + 10;
    if (c >= u'A' && c <= u'F') return c - u'A' + 10;
    return -1;
}

bool hasWhitespace(QStringView text)
{
    for (const QChar c : text) {
        if (c.isSpace()) return true;
    }
    return false;
}

// Text without its RFC 5322 comments (outside quoted strings); the first
// comment's content goes to `*firstComment`.
QString stripComments(const QString &text, QString *firstComment)
{
    QString out;
    QString comment;
    bool quoted = false;
    bool captured = false;
    int depth = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (depth > 0) {
            if (c == u'\\' && i + 1 < text.size()) {
                if (!captured) comment += text.at(i + 1);
                ++i;
            } else if (c == u'(') {
                ++depth;
                if (!captured) comment += c;
            } else if (c == u')') {
                if (--depth == 0) captured = true;
                else if (!captured) comment += c;
            } else if (!captured) {
                comment += c;
            }
            continue;
        }
        if (quoted) {
            out += c;
            if (c == u'\\' && i + 1 < text.size()) out += text.at(++i);
            else if (c == u'"') quoted = false;
            continue;
        }
        if (c == u'(') {
            depth = 1;
            continue;
        }
        if (c == u'"') quoted = true;
        out += c;
    }
    if (firstComment) *firstComment = comment;
    return out;
}

QString unquote(const QString &text)
{
    QString out;
    bool quoted = false;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == u'"') {
            quoted = !quoted;
            continue;
        }
        if (quoted && c == u'\\' && i + 1 < text.size()) {
            out += text.at(++i);
            continue;
        }
        out += c;
    }
    return out;
}

QString displayName(const QString &part)
{
    return oneLine(decodeEncodedWords(unquote(stripComments(part, nullptr).trimmed())));
}

MailAddress parseMailbox(const QString &raw)
{
    MailAddress result;
    const QString text = raw.trimmed();
    qsizetype open = -1;
    bool quoted = false;
    int depth = 0;
    for (qsizetype i = 0; i < text.size() && open < 0; ++i) {
        const QChar c = text.at(i);
        if (quoted || depth > 0) {
            if (c == u'\\') ++i;
            else if (quoted && c == u'"') quoted = false;
            else if (!quoted && c == u'(') ++depth;
            else if (!quoted && c == u')') --depth;
            continue;
        }
        if (c == u'"') quoted = true;
        else if (c == u'(') depth = 1;
        else if (c == u'<') open = i;
    }
    if (open >= 0) {
        const qsizetype close = text.indexOf(u'>', open + 1);
        result.address = text.mid(open + 1, (close < 0 ? text.size() : close) - open - 1);
        result.name = displayName(text.left(open));
    } else {
        QString comment;
        result.address = stripComments(text, &comment);
        result.name = displayName(comment);
    }
    result.address = oneLine(result.address);
    if (!result.address.contains(u'@')) {
        // "undisclosed-recipients" or a bare display name: no addr-spec.
        if (result.name.isEmpty()) result.name = displayName(result.address);
        result.address.clear();
    }
    if (result.name.compare(result.address, Qt::CaseInsensitive) == 0) result.name.clear();
    return result;
}

// One encoded word starting at `start` ("=?"); false when it is malformed
// or its charset is unknown (the caller keeps it verbatim).
bool decodeWordAt(const QString &text, qsizetype start, QString *decoded, qsizetype *end)
{
    const qsizetype q1 = text.indexOf(u'?', start + 2);
    if (q1 < 0 || q1 == start + 2 || q1 - start - 2 > kMaxCharsetLength || q1 + 2 >= text.size()
        || text.at(q1 + 2) != u'?')
        return false;
    const char16_t encoding = text.at(q1 + 1).toUpper().unicode();
    if (encoding != u'B' && encoding != u'Q') return false;
    const qsizetype close = text.indexOf(QLatin1String("?="), q1 + 3);
    if (close < 0) return false;
    QString charset = text.mid(start + 2, q1 - start - 2);
    const QStringView payload = QStringView(text).mid(q1 + 3, close - q1 - 3);
    if (hasWhitespace(charset) || hasWhitespace(payload)) return false;
    const qsizetype star = charset.indexOf(u'*');   // RFC 2231 language suffix
    if (star >= 0) charset.truncate(star);
    QByteArray bytes;
    if (encoding == u'B') {
        bytes = QByteArray::fromBase64(payload.toLatin1());
    } else {
        for (qsizetype i = 0; i < payload.size(); ++i) {
            const char16_t c = payload.at(i).unicode();
            if (c >= 0x80) return false;
            if (c == u'_') {
                bytes += ' ';
            } else if (c == u'=' && i + 2 < payload.size() && hexValue(payload.at(i + 1).unicode()) >= 0
                       && hexValue(payload.at(i + 2).unicode()) >= 0) {
                bytes += char(hexValue(payload.at(i + 1).unicode()) * 16 + hexValue(payload.at(i + 2).unicode()));
                i += 2;
            } else {
                bytes += char(c);
            }
        }
    }
    QStringDecoder decoder(charset.toLatin1().constData());
    if (!decoder.isValid()) return false;
    *decoded = decoder.decode(bytes);
    *end = close + 2;
    return true;
}

QString quoteIfNeeded(const QString &name)
{
    static const QString kSpecials = QStringLiteral("()<>[]:;@\\,.\"");
    bool needed = false;
    for (const QChar c : name) {
        if (kSpecials.contains(c)) {
            needed = true;
            break;
        }
    }
    if (!needed) return name;
    QString quoted = QStringLiteral("\"");
    for (const QChar c : name) {
        if (c == u'"' || c == u'\\') quoted += u'\\';
        quoted += c;
    }
    return quoted + u'"';
}

} // namespace

HeaderBlock::HeaderBlock(const QString &text)
{
    const QList<QStringView> lines = QStringView(text).split(u'\n');
    for (QStringView line : lines) {
        if (line.endsWith(u'\r')) line.chop(1);
        if (line.trimmed().isEmpty()) {
            if (!m_fields.isEmpty()) break;   // the empty line ends the header block
            continue;
        }
        if (line.front() == u' ' || line.front() == u'\t') {
            if (!m_fields.isEmpty()) m_fields.last().second += line;   // RFC 5322 2.2.3 unfolding
            continue;
        }
        const qsizetype colon = line.indexOf(u':');
        if (colon <= 0) continue;   // e.g. "Microsoft Mail Internet Headers Version 2.0"
        if (m_fields.size() >= kMaxFields) break;
        m_fields.append({line.left(colon).trimmed().toString().toLower(), line.mid(colon + 1).toString()});
    }
    for (auto &field : m_fields) field.second = field.second.trimmed();
}

QString HeaderBlock::value(QStringView lowerName) const
{
    for (const auto &field : m_fields) {
        if (field.first == lowerName) return field.second;
    }
    return {};
}

QString decodeEncodedWords(const QString &text)
{
    QString out;
    qsizetype copied = 0;
    qsizetype search = 0;
    bool afterWord = false;
    while (search < text.size()) {
        const qsizetype start = text.indexOf(QLatin1String("=?"), search);
        if (start < 0) break;
        QString decoded;
        qsizetype end = 0;
        if (!decodeWordAt(text, start, &decoded, &end)) {
            search = start + 2;
            continue;
        }
        const QStringView gap = QStringView(text).mid(copied, start - copied);
        // RFC 2047 6.2: whitespace between adjacent encoded words is dropped.
        if (!(afterWord && gap.trimmed().isEmpty())) out += gap;
        out += decoded;
        copied = search = end;
        afterWord = true;
    }
    out += QStringView(text).mid(copied);
    return out;
}

QList<MailAddress> parseAddressList(const QString &value)
{
    QList<MailAddress> out;
    QString current;
    bool quoted = false;
    int angle = 0;
    int comment = 0;
    auto flush = [&] {
        if (out.size() < kMaxAddresses) {
            const MailAddress address = parseMailbox(current);
            if (!address.name.isEmpty() || !address.address.isEmpty()) out.append(address);
        }
        current.clear();
    };
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (quoted || comment > 0) {
            current += c;
            if (c == u'\\' && i + 1 < value.size()) current += value.at(++i);
            else if (quoted && c == u'"') quoted = false;
            else if (!quoted && c == u'(') ++comment;
            else if (!quoted && c == u')') --comment;
            continue;
        }
        if ((c == u',' || c == u';') && angle == 0) {
            flush();
            continue;
        }
        if (c == u':' && angle == 0) {   // "group name:" — its members follow
            current.clear();
            continue;
        }
        if (c == u'"') quoted = true;
        else if (c == u'(') comment = 1;
        else if (c == u'<') ++angle;
        else if (c == u'>' && angle > 0) --angle;
        current += c;
    }
    flush();
    return out;
}

QString formatAddress(const QString &name, const QString &address)
{
    const QString n = name.trimmed();
    const QString a = address.trimmed();
    if (a.isEmpty()) return quoteIfNeeded(n);
    if (n.isEmpty() || n.compare(a, Qt::CaseInsensitive) == 0) return a;
    return quoteIfNeeded(n) + QStringLiteral(" <") + a + u'>';
}

QString formatAddressList(const QList<MailAddress> &addresses)
{
    QStringList parts;
    for (const MailAddress &address : addresses) {
        const QString text = formatAddress(address.name, address.address);
        if (!text.isEmpty()) parts.append(text);
    }
    return parts.join(QStringLiteral(", "));
}

QDateTime parseRfc5322Date(const QString &value)
{
    QString text = stripComments(value, nullptr).simplified();
    static const struct {
        const char *zone;
        const char *offset;
    } kZones[] = {
        {"GMT", "+0000"}, {"UT", "+0000"}, {"UTC", "+0000"}, {"Z", "+0000"},
        {"EST", "-0500"}, {"EDT", "-0400"}, {"CST", "-0600"}, {"CDT", "-0500"},
        {"MST", "-0700"}, {"MDT", "-0600"}, {"PST", "-0800"}, {"PDT", "-0700"},
    };
    const qsizetype space = text.lastIndexOf(u' ');
    if (space > 0) {
        const QString zone = text.mid(space + 1).toUpper();
        for (const auto &entry : kZones) {
            if (zone == QLatin1String(entry.zone)) {
                text = text.left(space + 1) + QLatin1String(entry.offset);
                break;
            }
        }
    }
    const QDateTime time = QDateTime::fromString(text, Qt::RFC2822Date);
    return time.isValid() ? time.toUTC() : QDateTime();
}

QString oneLine(const QString &text)
{
    QString out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (const QChar c : text) {
        const char16_t u = c.unicode();
        if (u == u'\r' || u == u'\n' || u == u'\t') {
            pendingSpace = true;
            continue;
        }
        if (u < 0x20 || u == 0x7F) continue;
        if (pendingSpace && !out.isEmpty() && !out.endsWith(u' ') && u != u' ') out += u' ';
        pendingSpace = false;
        out += c;
    }
    return out.trimmed();
}

QString stripAngles(const QString &id)
{
    const QString text = oneLine(id);
    const qsizetype open = text.indexOf(u'<');
    if (open < 0) return text;
    const qsizetype close = text.indexOf(u'>', open + 1);
    return text.mid(open + 1, (close < 0 ? text.size() : close) - open - 1).trimmed();
}

} // namespace QindaOffice::Mail::MsgDetail
