// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The IMAP commands M81 needs that QindaTK's ImapMailProvider does not
// offer (D-090): keywords (categories) read and stored per message, the
// header blocks a rule's conditions read, and UID COPY (a rule's "copy to
// folder"). ImapMailProvider only knows \Seen and \Flagged, downloads whole
// messages but keeps their text, and moves without copying.
//
// AGENT-NOTE: the exchange is QindaTK's own Protocol::Channel and
// Imap::Session (imap_session.h), as for ImapSourceFetcher: TLS, STARTTLS,
// PLAIN/LOGIN/XOAUTH2, the clear-text-only-to-loopback rule and redaction
// stay the toolkit's. Toolkit follow-up (D-090): keywords, header fetches
// and COPY on ImapMailProvider replace this class.
// AGENT-CONTRACT (AccountRuntime::run, ProviderCalls, MailOrganizer): it
// behaves like a provider — the runtime applies the IMAP credential through
// setSecret(secret, mechanism); every call answers ONCE with
// extrasFinished(requestId, result) on a later turn. Result keys: the
// provider contract (ok, state, error, retryable) plus
//   keywords:  keywords {uid → [keyword]}, permanentFlags [..],
//              allowsKeywords (the server takes new keywords: "\*")
//   store:     the same after the change (for the one UID)
//   headers:   headers {uid → header block text}
//   copy:      (nothing more)
// A UIDVALIDITY other than the caller's is `conflict` (sync first).
// AGENT-GUARD: the secret lives only in memory and is redacted from errors.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QindaOffice::Apps::Mail {

class ImapExtras : public QObject {
    Q_OBJECT
public:
    struct Server {
        QString host;
        int port = 993;
        QString security = QStringLiteral("tls");
        QString userName;
        int timeoutMs = 20000;
    };

    // One request (defined in imap_extras.cpp).
    struct Job;

    explicit ImapExtras(const Server &server, QObject *parent = nullptr);

    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism = QStringLiteral("auto"));
    Q_INVOKABLE void clearSecret();

    // FLAGS of `uids` (all of the mailbox's when empty, at most 5000).
    bool keywords(const QString &requestId, const QString &mailbox, qint64 uidValidity, const QList<qint64> &uids);
    // UID STORE +FLAGS / -FLAGS of keyword atoms on one message.
    bool storeKeywords(const QString &requestId, const QString &mailbox, qint64 uidValidity, qint64 uid,
                       const QStringList &add, const QStringList &remove);
    bool headers(const QString &requestId, const QString &mailbox, qint64 uidValidity, const QList<qint64> &uids);
    bool copy(const QString &requestId, const QString &mailbox, qint64 uidValidity, qint64 uid,
              const QString &targetMailbox);

signals:
    void extrasFinished(const QString &requestId, const QVariantMap &result);

private:
    bool start(const QString &requestId, const Job &job);
    void refuse(const QString &requestId, const QString &error);

    Server m_server;
    QString m_secret;
    QString m_mechanism = QStringLiteral("auto");
};

} // namespace QindaOffice::Apps::Mail
