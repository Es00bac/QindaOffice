// SPDX-License-Identifier: GPL-3.0-or-later
#include "rfc822_writer.h"

#include <qindatk/mail_mime.h>

#include <QCryptographicHash>
#include <QLocale>
#include <QRegularExpression>
#include <QTimeZone>

namespace QindaOffice::Mail {

namespace {

constexpr int kMaximumDepth = 8;

bool asciiPrintable(const QString &text)
{
    for (const QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7e) return false;
    }
    return true;
}

QString singleLine(QString text)
{
    text.replace(QLatin1String("\r\n"), QLatin1String(" "));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text.trimmed();
}

QByteArray wrapBase64(const QByteArray &bytes)
{
    const QByteArray encoded = bytes.toBase64();
    QByteArray out;
    out.reserve(encoded.size() + encoded.size() / 76 * 2 + 2);
    for (qsizetype i = 0; i < encoded.size(); i += 76) out += encoded.mid(i, 76) + "\r\n";
    return out;
}

// Folds a long ASCII header value at spaces (RFC 5322 §2.2.3).
QByteArray foldAscii(const QByteArray &value, int used)
{
    QByteArray out;
    int column = used;
    for (const QByteArray &word : value.split(' ')) {
        if (!out.isEmpty()) {
            if (column + 1 + word.size() > 76) {
                out += "\r\n";
                column = 0;
            }
            out += ' ';
            ++column;
        }
        out += word;
        column += int(word.size());
    }
    return out;
}

QByteArray header(const char *name, const QString &text)
{
    const QString line = singleLine(text);
    if (line.isEmpty()) return {};
    const QByteArray key(name);
    const QByteArray value = asciiPrintable(line) ? foldAscii(line.toLatin1(), int(key.size()) + 2)
                                                  : QindaTK::Mime::encodeHeaderText(line);
    return key + ": " + value + "\r\n";
}

// One address list, entry by entry: a valid mailbox is formatted by the
// toolkit; an entry without a usable address (an Exchange-only recipient)
// stays visible as an empty RFC 5322 group, "Name:;".
QByteArray addressHeader(const char *name, const QString &list)
{
    QStringList entries;
    QString current;
    bool quoted = false;
    bool angle = false;
    for (const QChar c : list) {
        if (c == QLatin1Char('"')) quoted = !quoted;
        else if (!quoted && c == QLatin1Char('<')) angle = true;
        else if (!quoted && c == QLatin1Char('>')) angle = false;
        if (!quoted && !angle && (c == QLatin1Char(',') || c == QLatin1Char(';'))) {
            entries.append(current);
            current.clear();
        } else {
            current += c;
        }
    }
    entries.append(current);
    QByteArray joined;
    for (const QString &raw : std::as_const(entries)) {
        const QString entry = singleLine(raw);
        if (entry.isEmpty()) continue;
        QString error;
        const QList<QindaTK::Mime::Address> parsed = QindaTK::Mime::parseAddressList(entry, &error);
        QByteArray formatted;
        if (parsed.size() == 1) {
            formatted = QindaTK::Mime::formatAddress(parsed.first());
        } else {
            // RFC 5322 §3.4: group = display-name ":" ";" — a phrase, so a
            // name with specials ("Doe, John") is a quoted-string.
            QString label = entry;
            label.remove(QLatin1Char('"')).remove(QLatin1Char('\\')).remove(QLatin1Char(':')).remove(QLatin1Char(';'));
            label = label.trimmed();
            static const QString specials = QStringLiteral("()<>[]@,.");
            bool quote = false;
            for (const QChar c : std::as_const(label)) quote = quote || specials.contains(c);
            if (!asciiPrintable(label)) formatted = QindaTK::Mime::encodeHeaderText(label);
            else formatted = quote ? '"' + label.toLatin1() + '"' : label.toLatin1();
            formatted += ":;";
        }
        joined += (joined.isEmpty() ? QByteArray() : QByteArrayLiteral(",\r\n ")) + formatted;
    }
    return joined.isEmpty() ? QByteArray() : QByteArray(name) + ": " + joined + "\r\n";
}

QByteArray idHeader(const char *name, const QString &id)
{
    QString bare = singleLine(id);
    bare.remove(QLatin1Char('<')).remove(QLatin1Char('>'));
    if (bare.isEmpty() || !asciiPrintable(bare) || bare.contains(QLatin1Char(' '))) return {};
    return QByteArray(name) + ": <" + bare.toLatin1() + ">\r\n";
}

QByteArray dateValue(const QDateTime &date)
{
    const QDateTime when = date.isValid() ? date : QDateTime::fromSecsSinceEpoch(0, QTimeZone::UTC);
    const int offset = when.offsetFromUtc();
    const QDateTime local = when.toOffsetFromUtc(offset);
    const int minutes = std::abs(offset) / 60;
    const QString zone = QStringLiteral("%1%2%3")
                             .arg(offset < 0 ? QLatin1Char('-') : QLatin1Char('+'))
                             .arg(minutes / 60, 2, 10, QLatin1Char('0'))
                             .arg(minutes % 60, 2, 10, QLatin1Char('0'));
    return QLocale::c().toString(local, QStringLiteral("ddd, dd MMM yyyy HH:mm:ss")).toLatin1() + ' ' + zone.toLatin1();
}

// Text bodies: CRLF lines, 7bit when every line is short ASCII, else base64.
QByteArray textLeaf(const QString &text, const QByteArray &subtype)
{
    QString normalized = text;
    normalized.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    normalized.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QByteArray utf8 = normalized.toUtf8();
    bool sevenBit = true;
    for (const QByteArray &line : utf8.split('\n')) {
        if (line.size() > 998) sevenBit = false;
        for (const char c : line) {
            if (static_cast<unsigned char>(c) >= 0x80 || c == '\0') sevenBit = false;
        }
        if (!sevenBit) break;
    }
    const QByteArray head = "Content-Type: text/" + subtype + "; charset=utf-8\r\n";
    if (sevenBit) {
        QByteArray body = utf8;
        body.replace("\n", "\r\n");
        if (!body.endsWith("\r\n")) body += "\r\n";
        return head + "Content-Transfer-Encoding: 7bit\r\n\r\n" + body;
    }
    return head + "Content-Transfer-Encoding: base64\r\n\r\n" + wrapBase64(utf8);
}

QByteArray mimeTypeOf(const QString &type)
{
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9!#$&^_.+-]+/[A-Za-z0-9!#$&^_.+-]+$"));
    return valid.match(type).hasMatch() ? type.toLower().toLatin1() : QByteArrayLiteral("application/octet-stream");
}

QByteArray nameParams(const QString &rawName, QByteArray *disposition)
{
    QString name = singleLine(rawName);
    name.remove(QLatin1Char('"')).remove(QLatin1Char('\\'));
    if (name.isEmpty()) return {};
    if (asciiPrintable(name)) {
        *disposition += "; filename=\"" + name.toLatin1() + '"';
        return "; name=\"" + name.toLatin1() + '"';
    }
    *disposition += "; filename*=utf-8''" + name.toUtf8().toPercentEncoding();
    return "; name=\"" + QindaTK::Mime::encodeHeaderText(name) + '"';
}

class Writer {
public:
    explicit Writer(QByteArray seed)
        : m_seed(std::move(seed))
    {
    }

    QByteArray message(const MessageContent &message, int depth);

private:
    QByteArray boundary(const QList<QByteArray> &parts)
    {
        // Deterministic, and re-drawn in the (adversarial) case that a part
        // already contains it.
        for (int attempt = 0;; ++attempt) {
            const QByteArray digest = QCryptographicHash::hash(
                m_seed + QByteArray::number(++m_counter) + QByteArray::number(attempt), QCryptographicHash::Sha256);
            const QByteArray mark = "=_qindamail_" + digest.toHex().left(32);
            bool clash = false;
            for (const QByteArray &part : parts) clash = clash || part.contains(mark);
            if (!clash) return mark;
        }
    }

    QByteArray multipart(const QByteArray &subtype, const QList<QByteArray> &parts)
    {
        const QByteArray mark = boundary(parts);
        QByteArray out = "Content-Type: multipart/" + subtype + "; boundary=\"" + mark + "\"\r\n\r\n";
        for (const QByteArray &part : parts) out += "--" + mark + "\r\n" + part;
        return out + "--" + mark + "--\r\n";
    }

    QByteArray attachment(const MessageContent &owner, const MessageAttachment &part, int depth);

    QByteArray m_seed;
    int m_counter = 0;
};

QByteArray Writer::attachment(const MessageContent &owner, const MessageAttachment &part, int depth)
{
    QByteArray disposition = part.inlinePart && !part.contentId.isEmpty() ? "inline" : "attachment";
    QByteArray type = mimeTypeOf(part.mimeType);
    QByteArray body;
    QByteArray encoding = "base64";
    if (part.embedded != nullptr && depth < kMaximumDepth) {
        type = "message/rfc822";
        body = message(*part.embedded, depth + 1);
        encoding = "7bit";
    } else if (type == "message/rfc822" && !part.data.isEmpty()) {
        // RFC 2046 §5.2.1: never base64; carried as it came, in CRLF lines.
        body = part.data;
        body.replace("\r\n", "\n");
        body.replace("\n", "\r\n");
        if (!body.endsWith("\r\n")) body += "\r\n";
        encoding = "7bit";
        for (const char c : std::as_const(body)) {
            if (static_cast<unsigned char>(c) >= 0x80) encoding = "8bit";
        }
    } else {
        if (type == "message/rfc822") type = "application/octet-stream";
        body = wrapBase64(part.data);
    }
    if (type == "text/calendar" && !owner.calendarMethod.isEmpty() && asciiPrintable(owner.calendarMethod))
        type += "; method=" + owner.calendarMethod.toLatin1();
    QByteArray head = "Content-Type: " + type + nameParams(part.name, &disposition) + "\r\n";
    head += "Content-Disposition: " + disposition + "\r\n";
    if (!part.contentId.isEmpty()) head += idHeader("Content-ID", part.contentId);
    if (!part.contentLocation.isEmpty() && asciiPrintable(part.contentLocation))
        head += "Content-Location: " + singleLine(part.contentLocation).toLatin1() + "\r\n";
    return head + "Content-Transfer-Encoding: " + encoding + "\r\n\r\n" + body;
}

QByteArray Writer::message(const MessageContent &message, int depth)
{
    QByteArray head = "Date: " + dateValue(message.date) + "\r\n";
    QindaTK::Mime::Address from{message.fromName, message.fromAddress};
    if (QindaTK::Mime::validAddress(message.fromAddress)) head += "From: " + QindaTK::Mime::formatAddress(from) + "\r\n";
    else head += addressHeader("From", message.fromName.isEmpty() ? message.fromAddress : message.fromName);
    if (!message.senderAddress.isEmpty() && QindaTK::Mime::validAddress(message.senderAddress)) {
        head += "Sender: " + QindaTK::Mime::formatAddress({message.senderName, message.senderAddress}) + "\r\n";
    }
    head += addressHeader("Reply-To", message.replyTo);
    head += addressHeader("To", message.to);
    head += addressHeader("Cc", message.cc);
    head += addressHeader("Bcc", message.bcc);
    const QByteArray subject = header("Subject", message.subject);
    head += subject.isEmpty() ? QByteArrayLiteral("Subject: \r\n") : subject;
    head += idHeader("Message-ID", message.messageId.isEmpty() ? derivedMessageId(message) : message.messageId);
    head += idHeader("In-Reply-To", message.inReplyTo);
    const QString references = singleLine(message.references);
    if (!references.isEmpty() && asciiPrintable(references))
        head += "References: " + foldAscii(references.toLatin1(), 12) + "\r\n";
    for (const auto &extra : message.extraHeaders) {
        QByteArray value = extra.second;
        value.replace('\r', ' ').replace('\n', ' ');
        bool ascii = !extra.first.isEmpty();
        for (const char c : std::as_const(value)) ascii = ascii && static_cast<unsigned char>(c) < 0x80;
        if (ascii) head += extra.first + ": " + value.trimmed() + "\r\n";
    }
    head += "MIME-Version: 1.0\r\n";

    QList<QByteArray> inlineParts;
    QList<QByteArray> regular;
    for (const MessageAttachment &part : message.attachments) {
        const bool related = part.inlinePart && !part.contentId.isEmpty() && part.embedded == nullptr
            && !message.html.isEmpty();
        (related ? inlineParts : regular).append(attachment(message, part, depth));
    }
    QByteArray body;
    if (message.html.isEmpty()) {
        body = textLeaf(message.plain, "plain");
    } else {
        const QString plain = message.plain.isEmpty() ? QindaTK::Mime::htmlToPlain(message.html) : message.plain;
        body = multipart("alternative", {textLeaf(plain, "plain"), textLeaf(message.html, "html")});
        if (!inlineParts.isEmpty())
            body = multipart("related; type=\"multipart/alternative\"", QList<QByteArray>{body} + inlineParts);
    }
    if (!regular.isEmpty()) body = multipart("mixed", QList<QByteArray>{body} + regular);
    return head + body;
}

} // namespace

QString derivedMessageId(const MessageContent &message)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString &field : {message.subject, message.fromAddress, message.fromName, message.to,
                                 message.date.toUTC().toString(Qt::ISODate), message.plain, message.html})
        hash.addData(field.toUtf8() + '\0');
    return QString::fromLatin1(hash.result().toHex().left(40)) + QStringLiteral("@qindamail.invalid");
}

QByteArray writeRfc822(const MessageContent &message)
{
    const QString id = message.messageId.isEmpty() ? derivedMessageId(message) : message.messageId;
    Writer writer(id.toUtf8());
    return writer.message(message, 0);
}

} // namespace QindaOffice::Mail
