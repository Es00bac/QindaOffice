// SPDX-License-Identifier: GPL-3.0-or-later
#include "rfc822_reader.h"

#include "mime_header.h"

#include <qindatk/mail_mime.h>

#include <QHash>
#include <QRegularExpression>

namespace QindaOffice::Mail {

namespace {

namespace MH = MimeHeader;

// AGENT-GUARD: the toolkit's MaximumDepth (mail_mime_parse.cpp). A deeper
// multipart is a leaf there, so it must be a leaf here too, or the section
// numbers stop agreeing.
constexpr int kMaximumDepth = 8;
constexpr int kMaximumParts = 2000;

// What one walk collects. `bodyTaken` stops later inline text from replacing
// the body a multipart/mixed already has.
struct Walk {
    MessageContent *message = nullptr;
    int parts = 0;
    bool plainTaken = false;
    bool htmlTaken = false;
};

// QindaTK's splitMultipart, unchanged in behaviour (see the guard above).
QList<QByteArray> splitMultipart(const QByteArray &body, const QByteArray &boundary)
{
    QList<QByteArray> parts;
    const QByteArray delimiter = "--" + boundary;
    qsizetype start = body.startsWith(delimiter) ? 0 : body.indexOf("\n" + delimiter);
    if (start < 0) return parts;
    if (!body.startsWith(delimiter)) start += 1;
    for (;;) {
        if (body.mid(start + delimiter.size(), 2) == "--") break;
        const qsizetype lineEnd = body.indexOf('\n', start);
        if (lineEnd < 0) break;
        const qsizetype contentStart = lineEnd + 1;
        const qsizetype next = body.indexOf("\n" + delimiter, lineEnd);
        if (next < 0) {
            parts.append(body.mid(contentStart));
            break;
        }
        qsizetype contentEnd = next;
        if (contentEnd > contentStart && body.at(contentEnd - 1) == '\r') --contentEnd;
        parts.append(body.mid(contentStart, std::max<qsizetype>(0, contentEnd - contentStart)));
        start = next + 1;
        if (parts.size() > kMaximumParts) break;
    }
    return parts;
}

QString normalizeNewlines(QString text)
{
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

QString childSection(const QString &section, qsizetype index)
{
    return section.isEmpty() ? QString::number(index + 1) : section + QLatin1Char('.') + QString::number(index + 1);
}

QString escapeHtml(const QString &text)
{
    return text.toHtmlEscaped();
}

void walk(const MH::Fields &fields, const QByteArray &body, const QString &section, int depth, Walk &walk);

void walkLeaf(const MH::Fields &fields, const QByteArray &body, const QString &section, const MH::Structured &type,
              Walk &state)
{
    MessageContent &message = *state.message;
    const QString leafSection = section.isEmpty() ? QStringLiteral("1") : section;
    const QString mimeType = type.main.isEmpty() ? QStringLiteral("text/plain") : type.main;
    const QString encoding = QString::fromLatin1(MH::value(fields, "content-transfer-encoding")).trimmed().toLower();
    const MH::Structured disposition = MH::parseStructured(MH::value(fields, "content-disposition"));
    QString name = disposition.params.value(QStringLiteral("filename"));
    if (name.isEmpty()) name = type.params.value(QStringLiteral("name"));
    const QString charset = type.params.value(QStringLiteral("charset"));
    const bool isPlain = mimeType == QLatin1String("text/plain");
    const bool isHtml = mimeType == QLatin1String("text/html");
    // AGENT-NOTE: the toolkit's attachment rule (named, disposition
    // attachment, or not a text body) — so the parts listed here are the
    // parts its records list, section for section.
    const bool attachment = disposition.main == QLatin1String("attachment") || !name.isEmpty() || !(isPlain || isHtml);
    const bool rfc822 = mimeType == QLatin1String("message/rfc822");
    // A message/rfc822 part is never base64/QP by RFC 2046 §5.2.1, but
    // decodeTransfer leaves 7bit/8bit/binary alone either way.
    const QByteArray decoded = QindaTK::Mime::decodeTransfer(body, encoding);
    if (!attachment) {
        const QString text = QindaTK::Mime::decodeText(decoded, charset);
        if (isPlain) {
            if (!state.plainTaken) {
                message.plain = normalizeNewlines(text);
                state.plainTaken = true;
            } else {
                // Inline text after the body (a list footer, a forwarded
                // note) belongs to the body, as mail clients show it.
                message.plain += QStringLiteral("\n\n") + normalizeNewlines(text);
                if (state.htmlTaken) {
                    message.html += QStringLiteral("<div style=\"white-space: pre-wrap\">")
                        + escapeHtml(normalizeNewlines(text)) + QStringLiteral("</div>");
                }
            }
        } else if (!state.htmlTaken) {
            message.html = text;
            state.htmlTaken = true;
        }
        return;
    }
    if (mimeType == QLatin1String("text/calendar") && message.calendar.isEmpty()) {
        message.calendar = QindaTK::Mime::decodeText(decoded, charset);
        message.calendarMethod = type.params.value(QStringLiteral("method")).toUpper();
    }
    MessageAttachment part;
    part.section = leafSection;
    part.mimeType = mimeType;
    part.contentId = MH::bareId(MH::value(fields, "content-id"));
    part.contentLocation = QindaTK::Mime::decodeHeaderText(MH::value(fields, "content-location"));
    part.inlinePart = disposition.main == QLatin1String("inline") && name.isEmpty() && !part.contentId.isEmpty();
    if (name.isEmpty() && rfc822) {
        const QString subject = rfc822Subject(decoded).trimmed();
        if (!subject.isEmpty()) name = subject + QStringLiteral(".eml");
    }
    part.name = name.isEmpty() ? QStringLiteral("part-%1").arg(leafSection) : name;
    part.data = decoded;
    message.attachments.append(part);
}

void walkAlternative(const QList<QByteArray> &parts, const QString &section, int depth, Walk &state)
{
    // RFC 2046 §5.1.4: the LAST alternative the reader can show is the
    // richest. Each alternative is walked on its own; the last one with
    // HTML gives the HTML, the last one with text gives the text, and the
    // parts (inline images, a calendar) of the chosen ones are kept.
    MessageContent &message = *state.message;
    struct Child {
        MessageContent content;
    };
    QList<Child> children;
    for (qsizetype i = 0; i < parts.size() && state.parts < kMaximumParts; ++i) {
        const MH::Split child = MH::splitHeaderBody(parts.at(i));
        Child walked;
        Walk inner{&walked.content, state.parts, false, false};
        walk(MH::parseFields(child.head), child.body, childSection(section, i), depth + 1, inner);
        state.parts = inner.parts;
        children.append(walked);
    }
    qsizetype htmlChild = -1;
    qsizetype plainChild = -1;
    for (qsizetype i = 0; i < children.size(); ++i) {
        if (!children.at(i).content.html.isEmpty()) htmlChild = i;
        if (!children.at(i).content.plain.isEmpty()) plainChild = i;
    }
    for (qsizetype i = 0; i < children.size(); ++i) {
        const MessageContent &content = children.at(i).content;
        if (i == htmlChild && !state.htmlTaken) {
            message.html = content.html;
            state.htmlTaken = true;
        }
        if (i == plainChild && !state.plainTaken) {
            message.plain = content.plain;
            state.plainTaken = true;
        }
        if (message.calendar.isEmpty() && !content.calendar.isEmpty()) {
            message.calendar = content.calendar;
            message.calendarMethod = content.calendarMethod;
        }
        // Every part stays listed: the toolkit lists them too, and a part of
        // an alternative nobody shows is still a file the user may want.
        message.attachments.append(content.attachments);
    }
}

void walk(const MH::Fields &fields, const QByteArray &body, const QString &section, int depth, Walk &state)
{
    ++state.parts;
    const MH::Structured type = MH::parseStructured(MH::value(fields, "content-type", "text/plain"));
    const QString mimeType = type.main.isEmpty() ? QStringLiteral("text/plain") : type.main;
    const QString boundary = type.params.value(QStringLiteral("boundary"));
    if (!mimeType.startsWith(QLatin1String("multipart/")) || depth >= kMaximumDepth || boundary.isEmpty()) {
        walkLeaf(fields, body, section, type, state);
        return;
    }
    const QList<QByteArray> parts = splitMultipart(body, boundary.toUtf8());
    if (mimeType == QLatin1String("multipart/alternative")) {
        walkAlternative(parts, section, depth, state);
        return;
    }
    qsizetype root = 0;
    if (mimeType == QLatin1String("multipart/related")) {
        // RFC 2387 §3.2: the root is the part named by `start`, else the first.
        const QString start = MH::bareId(type.params.value(QStringLiteral("start")).toUtf8());
        for (qsizetype i = 0; !start.isEmpty() && i < parts.size(); ++i) {
            const MH::Split child = MH::splitHeaderBody(parts.at(i));
            if (MH::bareId(MH::value(MH::parseFields(child.head), "content-id")) == start) root = i;
        }
    }
    const qsizetype first = state.message->attachments.size();
    for (qsizetype i = 0; i < parts.size() && state.parts < kMaximumParts; ++i) {
        const MH::Split child = MH::splitHeaderBody(parts.at(i));
        walk(MH::parseFields(child.head), child.body, childSection(section, i), depth + 1, state);
    }
    if (mimeType == QLatin1String("multipart/related")) {
        // Every non-root part of a related set is a resource of the root.
        const QString rootSection = childSection(section, root);
        for (qsizetype i = first; i < state.message->attachments.size(); ++i) {
            MessageAttachment &part = state.message->attachments[i];
            if (part.section != rootSection && !part.section.startsWith(rootSection + QLatin1Char('.'))
                && !part.contentId.isEmpty()) {
                part.inlinePart = true;
            }
        }
    }
}

void firstAddress(const QString &decoded, QString *name, QString *address)
{
    const qsizetype open = decoded.indexOf(QLatin1Char('<'));
    const qsizetype close = decoded.indexOf(QLatin1Char('>'), open + 1);
    if (open >= 0 && close > open) {
        *address = decoded.mid(open + 1, close - open - 1).trimmed();
        QString display = decoded.left(open).trimmed();
        if (display.size() >= 2 && display.startsWith(QLatin1Char('"')) && display.endsWith(QLatin1Char('"')))
            display = display.mid(1, display.size() - 2);
        *name = display;
    } else {
        *address = decoded.section(QLatin1Char(','), 0, 0).trimmed();
        name->clear();
    }
}

QDateTime parseDate(const QByteArray &raw)
{
    QString date = QString::fromLatin1(raw).simplified();
    static const QRegularExpression comment(QStringLiteral("\\s*\\([^)]*\\)\\s*$"));
    date.remove(comment);
    QDateTime parsed = QDateTime::fromString(date, Qt::RFC2822Date);
    if (!parsed.isValid()) {
        // "Tue, 8 Oct 2026 10:00:00 GMT" and other obsolete zone names.
        static const QRegularExpression zone(QStringLiteral("\\s+(UT|GMT|Z|EST|EDT|CST|CDT|MST|MDT|PST|PDT)$"));
        const QRegularExpressionMatch match = zone.match(date);
        if (match.hasMatch()) {
            static const QHash<QString, QString> offsets{
                {QStringLiteral("UT"), QStringLiteral("+0000")}, {QStringLiteral("GMT"), QStringLiteral("+0000")},
                {QStringLiteral("Z"), QStringLiteral("+0000")}, {QStringLiteral("EST"), QStringLiteral("-0500")},
                {QStringLiteral("EDT"), QStringLiteral("-0400")}, {QStringLiteral("CST"), QStringLiteral("-0600")},
                {QStringLiteral("CDT"), QStringLiteral("-0500")}, {QStringLiteral("MST"), QStringLiteral("-0700")},
                {QStringLiteral("MDT"), QStringLiteral("-0600")}, {QStringLiteral("PST"), QStringLiteral("-0800")},
                {QStringLiteral("PDT"), QStringLiteral("-0700")}};
            date = date.left(match.capturedStart()) + QLatin1Char(' ') + offsets.value(match.captured(1));
            parsed = QDateTime::fromString(date, Qt::RFC2822Date);
        }
    }
    return parsed;
}

// Headers the writer may carry over verbatim (message_content.h).
bool carriedHeader(const QByteArray &name)
{
    static const QList<QByteArray> kept{"importance", "priority", "x-priority", "x-msmail-priority", "sensitivity",
                                        "x-mailer", "user-agent", "organization", "list-id", "list-unsubscribe",
                                        "thread-topic", "thread-index", "disposition-notification-to",
                                        "return-receipt-to", "keywords", "comments"};
    return kept.contains(name);
}

} // namespace

MessageContent readRfc822(const QByteArray &raw)
{
    MessageContent message;
    const MH::Split split = MH::splitHeaderBody(raw);
    const MH::Fields fields = MH::parseFields(split.head);
    const auto text = [&fields](const char *name) {
        return QindaTK::Mime::decodeHeaderText(MH::value(fields, name));
    };
    message.subject = text("subject");
    firstAddress(text("from"), &message.fromName, &message.fromAddress);
    firstAddress(text("sender"), &message.senderName, &message.senderAddress);
    if (message.senderAddress.compare(message.fromAddress, Qt::CaseInsensitive) == 0) {
        message.senderName.clear();
        message.senderAddress.clear();
    }
    message.replyTo = text("reply-to");
    message.to = text("to");
    message.cc = text("cc");
    message.bcc = text("bcc");
    message.messageId = MH::bareId(MH::value(fields, "message-id"));
    message.inReplyTo = MH::bareId(MH::value(fields, "in-reply-to"));
    message.references = QString::fromLatin1(MH::value(fields, "references")).simplified();
    message.date = parseDate(MH::value(fields, "date"));
    for (const auto &field : fields) {
        if (carriedHeader(field.first) || field.first.startsWith("x-ms-") || field.first.startsWith("x-original-"))
            message.extraHeaders.append(field);
    }
    Walk state{&message, 0, false, false};
    walk(fields, split.body, QString(), 0, state);
    // A cid: the HTML points at is inline even without a disposition.
    if (!message.html.isEmpty()) {
        for (MessageAttachment &part : message.attachments) {
            if (!part.contentId.isEmpty()
                && message.html.contains(QStringLiteral("cid:") + part.contentId, Qt::CaseInsensitive)) {
                part.inlinePart = true;
            }
        }
    }
    return message;
}

QString rfc822Subject(const QByteArray &raw)
{
    const MH::Split split = MH::splitHeaderBody(raw.left(256 * 1024));
    return QindaTK::Mime::decodeHeaderText(MH::value(MH::parseFields(split.head), "subject"));
}

} // namespace QindaOffice::Mail
