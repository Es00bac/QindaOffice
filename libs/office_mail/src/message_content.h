// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One mail message as QindaMail's readers deliver it and its writer and
// renderer consume it (M64, D-072): the RFC 5322 reader (rfc822_reader.h),
// the Outlook .msg reader (msg_reader.h) and the RFC 5322 writer
// (rfc822_writer.h) all speak this value type, so an imported .msg becomes
// ordinary RFC 5322 source and every later step (store record, rendering,
// printing, Save as .eml) has one path.
//
// AGENT-CONTRACT (rfc822_reader.cpp, rfc822_writer.cpp, msg_reader.cpp,
// message_render.cpp, the QindaMail app): `html` is the body AS SENT —
// untrusted, never sanitized here; only message_render.cpp turns it into
// something displayable (through html_sanitizer.h). Address lists are
// RFC 5322 display strings ("Ada <ada@example.org>, bob@example.org") with
// encoded words already decoded. Ids carry no angle brackets.

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>

#include <memory>

namespace QindaOffice::Mail {

struct MessageContent;

struct MessageAttachment {
    QString name;                // decoded file name; may be empty for inline parts
    QString mimeType;            // lower-case "type/subtype", no parameters
    QString contentId;           // Content-ID / PidTagAttachContentId without <>
    QString contentLocation;     // Content-Location, when given
    QByteArray data;             // decoded payload (empty for an embedded message)
    bool inlinePart = false;     // Content-Disposition: inline, or hidden/rendered in the body
    // IMAP-style section number of the part in its RFC 5322 source ("2",
    // "1.2"), the numbering QindaTK::Mime::parse uses; empty when the
    // attachment did not come from RFC 5322 source.
    QString section;
    // An attached message (message/rfc822, or an Outlook embedded message
    // object). The writer serializes it as a message/rfc822 part.
    std::shared_ptr<const MessageContent> embedded;
};

struct MessageContent {
    QString subject;
    QString fromName;
    QString fromAddress;
    // Sender: when it differs from From (sent on behalf of).
    QString senderName;
    QString senderAddress;
    QString replyTo;
    QString to;
    QString cc;
    QString bcc;
    QString messageId;           // without <>
    QString inReplyTo;           // without <>
    QString references;          // as in the header: "<a@x> <b@y>"
    QDateTime date;              // sent (Date:, PidTagClientSubmitTime)
    QDateTime received;          // delivery time when known
    QString plain;               // text/plain body (may be empty)
    QString html;                // text/html body AS SENT (may be empty)
    // A text/calendar part (iMIP, RFC 6047) and its METHOD parameter.
    QString calendar;
    QString calendarMethod;
    QList<MessageAttachment> attachments;   // every non-body part, inline ones included
    // Headers the writer carries over verbatim (name, unfolded value), e.g.
    // X-Mailer, Importance; never the structural ones it writes itself.
    QList<QPair<QByteArray, QByteArray>> extraHeaders;
};

} // namespace QindaOffice::Mail
