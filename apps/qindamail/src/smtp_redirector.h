// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Outlook's "redirect it to people" for a mail rule (M81, D-090): the
// message goes on as it arrived — its own From, Subject and body — with a
// Resent-From / Resent-To / Resent-Date / Resent-Message-ID block in front
// (RFC 5322 §3.6.6), delivered over SMTP with the account's credential.
//
// AGENT-NOTE: QindaTK's SmtpMailProvider builds every message from a draft
// and cannot send prepared bytes, so this repeats its dialogue (RFC 5321:
// EHLO, STARTTLS, AUTH PLAIN/LOGIN/XOAUTH2, MAIL, RCPT, DATA) over the
// toolkit's own Protocol::Channel — the TLS policy and redaction stay the
// toolkit's. Toolkit follow-up (D-090): a "send prepared message" call on
// SmtpMailProvider replaces it.
// AGENT-CONTRACT (AccountRuntime::run "smtp", MailOrganizer): provider
// surface — setSecret(secret, mechanism); send() answers ONCE with
// redirectFinished(requestId, result) {ok, state, error, retryable,
// recipients, smtpCode} on a later turn. All recipients or none: a refused
// RCPT resets the transaction.
// AGENT-GUARD: the secret lives only in memory and is redacted; the
// message bytes never enter the transcript.

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QindaOffice::Apps::Mail {

class SmtpRedirector : public QObject {
    Q_OBJECT
public:
    struct Server {
        QString host;
        int port = 587;
        QString security = QStringLiteral("starttls");
        QString userName;
        QString fromAddress;     // MAIL FROM (the redirecting account)
        int timeoutMs = 30000;
    };

    explicit SmtpRedirector(const Server &server, QObject *parent = nullptr);

    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism = QStringLiteral("auto"));
    Q_INVOKABLE void clearSecret();

    // `message`: the original RFC 5322 bytes; the Resent block is added here.
    bool send(const QString &requestId, const QStringList &recipients, const QByteArray &message);

    // The bytes send() delivers (tests): the Resent block, then `message`.
    [[nodiscard]] static QByteArray resent(const QByteArray &message, const QString &from,
                                           const QStringList &recipients, const QString &messageId);

signals:
    void redirectFinished(const QString &requestId, const QVariantMap &result);

private:
    void refuse(const QString &requestId, const QString &error);

    Server m_server;
    QString m_secret;
    QString m_mechanism = QStringLiteral("auto");
};

} // namespace QindaOffice::Apps::Mail
