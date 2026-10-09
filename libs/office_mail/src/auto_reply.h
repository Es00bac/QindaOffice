// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Outlook's Automatic Replies (out of office) as one value for the three
// ways QindaMail can set them (M81, D-090; docs/mail.md#automatic-replies):
// Microsoft Graph mailboxSettings, Exchange Web Services OOF and, for an
// IMAP server with ManageSieve (RFC 5804), a Sieve vacation script
// (RFC 5230). The host picks the way per account; this file only holds
// the settings.
//
// AGENT-CONTRACT (AutoRepliesDialog.qml, MailOrganizer, the mail_exchange
// adapter — which reads the same map keys without linking office_mail):
// {enabled, scheduled, start, end (ISO-8601 UTC), internalMessage,
// externalMessage (plain text), externalAudience "none" | "contacts" |
// "all", subject, days}. Sieve sends `internalMessage` to everyone (it
// knows no organization), with `subject` and once per sender every `days`.

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QindaOffice::Mail {

struct AutoReply {
    bool enabled = false;
    bool scheduled = false;
    QDateTime start, end;            // UTC
    QString internalMessage;
    QString externalMessage;
    QString externalAudience = QStringLiteral("all");
    QString subject;
    int days = 1;
};

namespace AutoReplies {

inline constexpr int kMaxMessageLength = 16 * 1024;

AutoReply fromVariantMap(const QVariantMap &map);
QVariantMap toVariantMap(const AutoReply &reply);
// One problem per line ("end: …").
QStringList validate(const AutoReply &reply);

} // namespace AutoReplies
} // namespace QindaOffice::Mail
