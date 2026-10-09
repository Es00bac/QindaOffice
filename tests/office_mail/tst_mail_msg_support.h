// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support for tst_mail_msg's .msg cases (tst_mail_msg.cpp,
// tst_mail_msg_parts.cpp): date, recipient and readMsg() helpers.

#include "cfb_writer_msg.h"
#include "message_content.h"
#include "msg_reader.h"

#include <QDateTime>
#include <QDebug>
#include <QTimeZone>

#include <memory>

namespace QindaOffice::Mail::Test {

inline QDateTime utc(int year, int month, int day, int hour, int minute, int second)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute, second), QTimeZone(QTimeZone::UTC));
}

// A recipient object: PidTagRecipientType, display name, address type,
// e-mail address and (optionally) PidTagSmtpAddress.
inline MsgProps recipient(quint32 type, const QString &name, const QString &addressType, const QString &email,
                          const QString &smtp = {})
{
    MsgProps props;
    props.longValue(0x0C15, type);
    if (!name.isEmpty()) props.unicode(0x3001, name);
    if (!addressType.isEmpty()) props.unicode(0x3002, addressType);
    if (!email.isEmpty()) props.unicode(0x3003, email);
    if (!smtp.isEmpty()) props.unicode(0x39FE, smtp);
    return props;
}

// readMsg() that is expected to succeed; a failure is logged with its reason.
inline bool readOk(const QByteArray &file, MessageContent *content)
{
    QString error;
    const bool ok = readMsg(file, content, &error);
    if (!ok) qWarning().noquote() << "readMsg failed:" << error;
    return ok;
}

// readMsg() that is expected to fail: its error, or a parenthesized note when
// the reader broke its contract (succeeded, failed silently, left content).
inline QString readFailure(const QByteArray &file)
{
    MessageContent content;
    content.subject = QStringLiteral("stale");
    QString error;
    if (readMsg(file, &content, &error)) return QStringLiteral("(succeeded)");
    if (!content.subject.isEmpty()) return QStringLiteral("(content not reset)");
    return error.isEmpty() ? QStringLiteral("(failed silently)") : error;
}

// A message whose attachment embeds a message … `depth` levels deep.
inline QByteArray nestedMessages(int depth)
{
    auto current = std::make_shared<MsgBuilder>();
    current->unicode(0x0037, QStringLiteral("level %1").arg(depth));
    for (int level = depth - 1; level >= 0; --level) {
        auto parent = std::make_shared<MsgBuilder>();
        parent->unicode(0x0037, QStringLiteral("level %1").arg(level));
        MsgAttachmentBuilder attachment;
        attachment.longValue(0x3705, 5);
        attachment.embedded = current;
        parent->attachments.append(attachment);
        current = parent;
    }
    return current->build();
}

} // namespace QindaOffice::Mail::Test
