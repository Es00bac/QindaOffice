// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Automatic replies for an IMAP account whose server offers ManageSieve
// (RFC 5804): reads and writes QindaMail's vacation block in the active
// Sieve script (sieve_script.h), keeping the user's own script around it.
// (M81, D-090; docs/mail.md#automatic-replies.)
//
// AGENT-CONTRACT (AccountRuntime::run, ProviderCalls, MailOrganizer): it
// behaves like a QindaTK provider — the runtime applies the account's IMAP
// credential through the write-only setSecret(secret, mechanism) ("auto",
// "plain", "login" = a password; "xoauth2" = an access token) and every
// request answers ONCE with vacationFinished(requestId, result) on a later
// event-loop turn. Result keys: the provider contract (requestId, ok,
// state accepted | offline | rejected, error, retryable, transcript) plus
// `available` (false: the server has no ManageSieve or no vacation
// extension — `reason` says which), `extensions`, `implementation`,
// `activeScript`, and for a read `settings` (AutoReply map; enabled false
// when QindaMail's block is absent) and `foreignVacation` (another
// program's reply is in the script); for a write `changed`.
// Writing: no active script → a new script "qindamail" made active; turning
// the reply off where that script would be empty → it is deactivated and
// deleted. CHECKSCRIPT runs before PUTSCRIPT when the server has it.
// AGENT-GUARD: the secret lives only in memory, is never a property and is
// redacted from every error and transcript.

#include "auto_reply.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QindaOffice::Mail {

class ManageSieveProvider : public QObject {
    Q_OBJECT
public:
    struct Server {
        QString host;
        int port = 4190;
        QString security = QStringLiteral("starttls");   // starttls | tls | none (loopback only)
        QString userName;
        int timeoutMs = 20000;
        bool allowInsecureAuth = false;
    };

    explicit ManageSieveProvider(const Server &server, QObject *parent = nullptr);
    ~ManageSieveProvider() override;

    [[nodiscard]] Server server() const { return m_server; }
    void setServer(const Server &server) { m_server = server; }

    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism = QStringLiteral("auto"));
    Q_INVOKABLE void clearSecret();

    bool readVacation(const QString &requestId);
    // `addresses` become the reply's :addresses (the user's own); `settings`
    // is an AutoReply map — enabled false removes QindaMail's block.
    bool writeVacation(const QString &requestId, const QVariantMap &settings, const QStringList &addresses);

    // The name of the script QindaMail creates when none is active.
    static constexpr const char kScriptName[] = "qindamail";

signals:
    void vacationFinished(const QString &requestId, const QVariantMap &result);

private:
    bool start(const QString &requestId, bool write, const AutoReply &reply, const QStringList &addresses);
    void refuse(const QString &requestId, const QString &error);

    Server m_server;
    QString m_secret;
    QString m_mechanism = QStringLiteral("auto");
};

} // namespace QindaOffice::Mail
