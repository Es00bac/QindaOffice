// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The whole RFC 5322 source of one IMAP message (M64, D-072), which the
// reading pane, the printer and Save Message As need and QindaTK's
// ImapMailProvider cannot give: its downloads accept numbered part sections
// only (`^\d+(\.\d+)*$`) and its sync keeps just the plain text.
//
// AGENT-NOTE: the exchange is QindaTK's own — Protocol::Channel and
// Imap::Session (imap_session.h, installed and exported by the toolkit):
// TLS/STARTTLS, PLAIN/LOGIN/XOAUTH2, the clear-text-only-to-loopback rule and
// secret redaction all stay the toolkit's. This object only adds
// `UID FETCH <uid> (UID BODY.PEEK[])` on a worker thread. Toolkit follow-up
// (D-072): a whole-message fetch on ImapMailProvider replaces it.
//
// AGENT-CONTRACT (AccountRuntime::run, ProviderCalls): it behaves like a
// provider — the runtime applies the credential through the write-only
// setSecret(secret, mechanism) and the answer is ONE
// sourceFinished(requestId, result) with the provider result keys plus
// `source` (the bytes), always on a later event-loop turn.
// AGENT-GUARD: the secret lives only in memory, is never a property and is
// redacted from every error.

#include <QObject>
#include <QString>
#include <QVariantMap>

namespace QindaOffice::Apps::Mail {

class ImapSourceFetcher : public QObject {
    Q_OBJECT
public:
    struct Server {
        QString host;
        int port = 993;
        QString security = QStringLiteral("tls");
        QString userName;
        int timeoutMs = 20000;
    };

    explicit ImapSourceFetcher(const Server &server, QObject *parent = nullptr);

    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism = QStringLiteral("auto"));
    Q_INVOKABLE void clearSecret();

    // Starts the download; false (and a rejected answer on the next turn)
    // for an invalid request. `maximumBytes` bounds the message size the
    // server reports before the body is read.
    bool fetch(const QString &requestId, const QString &mailbox, qint64 uidValidity, qint64 uid,
               qint64 maximumBytes);

signals:
    void sourceFinished(const QString &requestId, const QVariantMap &result);

private:
    Server m_server;
    QString m_secret;
    QString m_mechanism = QStringLiteral("auto");
};

} // namespace QindaOffice::Apps::Mail
