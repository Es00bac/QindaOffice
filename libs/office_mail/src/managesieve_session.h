// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The ManageSieve dialogue (RFC 5804) QindaMail needs for automatic
// replies, over QindaTK's blocking Protocol::Channel on a worker thread:
// the capability greeting, STARTTLS (and the capabilities sent again after
// it), AUTHENTICATE PLAIN / LOGIN / XOAUTH2 / OAUTHBEARER, LISTSCRIPTS,
// GETSCRIPT, CHECKSCRIPT, PUTSCRIPT, SETACTIVE, DELETESCRIPT and LOGOUT.
// Server strings may be quoted or literals ({n}); client literals are the
// non-synchronizing {n+} RFC 5804 requires servers to accept.
//
// AGENT-NOTE: the toolkit has IMAP, POP3 and SMTP sessions but no
// ManageSieve; this mirrors its shape (imap_session.h) so the TLS policy,
// the clear-text-only-to-loopback rule and secret redaction stay the
// toolkit's Channel's.
// AGENT-GUARD: credentials are sent only when Channel::authAllowed() (TLS,
// a loopback test server, or an explicit allowance); scripts are written
// to the transcript as byte counts, never their text.
// Threading: one Session per job, on the job's thread; never shared.

#include <qindatk/protocol_client.h>

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace QindaOffice::Mail::ManageSieve {

struct Script {
    QString name;
    bool active = false;
};

class Session {
public:
    explicit Session(QindaTK::Protocol::Channel *channel) : m_channel(channel) {}

    // Greeting, STARTTLS when the endpoint asks for it, authentication.
    bool open(const QindaTK::Protocol::Endpoint &endpoint, const QindaTK::Protocol::Credentials &credentials);
    [[nodiscard]] QStringList sieveExtensions() const { return m_extensions; }
    [[nodiscard]] QString implementation() const { return m_implementation; }
    [[nodiscard]] bool supportsCheckScript() const { return !m_version.isEmpty(); }

    bool listScripts(QList<Script> *scripts);
    bool getScript(const QString &name, QString *script);
    // A script the server refuses is `false` with the server's reason in
    // lastStatus() and the channel still usable; I/O failures fail the channel.
    bool checkScript(const QString &script);
    bool putScript(const QString &name, const QString &script);
    bool setActive(const QString &name);           // "" deactivates every script
    bool deleteScript(const QString &name);
    void logout();

    [[nodiscard]] bool failed() const { return m_channel->failure() != QindaTK::Protocol::Failure::None; }
    [[nodiscard]] QString lastStatus() const { return m_lastStatus; }

private:
    struct Token {
        enum Kind { Atom, String, Code };
        Kind kind = Atom;
        QByteArray text;
    };
    // One response line with its literals; false on I/O failure.
    bool readTokens(QList<Token> *tokens);
    // Data lines until OK / NO / BYE; true for OK. `data` gets the lines before it.
    bool response(QList<QList<Token>> *data = nullptr, bool required = true);
    bool capabilities(const QList<QList<Token>> &lines);
    bool authenticate(const QindaTK::Protocol::Credentials &credentials);
    bool send(const QByteArray &line, const QString &shown = {});
    static QByteArray quotedArgument(const QString &text);
    static QByteArray literalArgument(const QString &text);

    QindaTK::Protocol::Channel *m_channel;
    QStringList m_extensions, m_sasl;
    QString m_implementation, m_version, m_lastStatus;
    bool m_startTls = false;
};

} // namespace QindaOffice::Mail::ManageSieve
