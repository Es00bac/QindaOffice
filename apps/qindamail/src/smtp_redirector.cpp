// SPDX-License-Identifier: GPL-3.0-or-later
// Redirect over SMTP with a Resent block (smtp_redirector.h).
#include "smtp_redirector.h"

#include <qindatk/mail_mime.h>
#include <qindatk/protocol_client.h>

#include <QDateTime>
#include <QLocale>
#include <QMetaObject>
#include <QTimeZone>
#include <QUuid>

#include <cctype>

namespace QindaOffice::Apps::Mail {

using namespace Qt::StringLiterals;
using QindaTK::Protocol::Failure;

namespace {

constexpr qsizetype kMaxMessage = 32 * 1024 * 1024;

QByteArray dotStuff(const QByteArray &message)
{
    QByteArray out;
    out.reserve(message.size() + message.size() / 64 + 8);
    bool lineStart = true;
    for (qsizetype i = 0; i < message.size(); ++i) {
        const char c = message.at(i);
        if (c == '\n' && (i == 0 || message.at(i - 1) != '\r')) out += '\r';   // bare LF → CRLF
        if (lineStart && c == '.') out += '.';
        out += c;
        lineStart = c == '\n';
    }
    if (!out.endsWith("\r\n")) out += "\r\n";
    return out + ".\r\n";
}

struct Dialogue {
    QindaTK::Protocol::Channel channel;
    int lastCode = 0;

    // One reply (multi-line folded); false on I/O failure or a malformed line.
    bool reply(int *code, QStringList *lines)
    {
        lines->clear();
        for (;;) {
            QByteArray line;
            if (!channel.readLine(&line)) return false;
            if (line.size() < 3 || !std::isdigit(uchar(line.at(0))) || lines->size() > 100) {
                channel.fail(Failure::Rejected, u"Malformed SMTP reply"_s);
                return false;
            }
            *code = lastCode = line.left(3).toInt();
            lines->append(QString::fromUtf8(line.mid(4)).trimmed());
            if (line.size() == 3 || line.at(3) == ' ') return true;
        }
    }
    bool expect(int wanted, const QString &step)
    {
        int code = 0;
        QStringList lines;
        if (!reply(&code, &lines)) return false;
        if (code != wanted && !(wanted < 10 && code / 100 == wanted)) {
            channel.fail(Failure::Rejected, u"%1 refused: %2 %3"_s.arg(step).arg(code).arg(lines.join(u' ')));
            return false;
        }
        return true;
    }
    bool ehlo(QStringList *capabilities)
    {
        int code = 0;
        QStringList lines;
        if (!channel.write("EHLO localhost\r\n") || !reply(&code, &lines)) return false;
        if (code != 250) {
            channel.fail(Failure::Rejected, u"EHLO refused: %1"_s.arg(code));
            return false;
        }
        capabilities->clear();
        for (qsizetype i = 1; i < lines.size(); ++i) capabilities->append(lines.at(i).toUpper());
        return true;
    }
};

QVariantMap run(const QindaTK::Protocol::Endpoint &endpoint, const QindaTK::Protocol::Credentials &credentials,
                const QString &from, const QStringList &recipients, const QByteArray &bytes)
{
    Dialogue d;
    d.channel.addSecret(credentials.secret);
    QStringList capabilities;
    bool delivered = false;
    const auto auth = [&]() -> bool {
        if (!credentials.isSet()) return true;
        if (!d.channel.authAllowed()) {
            d.channel.fail(Failure::Rejected, u"Refusing to send credentials over an unencrypted connection"_s);
            return false;
        }
        QStringList offered;
        for (const QString &capability : std::as_const(capabilities))
            if (capability.startsWith("AUTH "_L1) || capability.startsWith("AUTH="_L1))
                offered += capability.mid(5).split(u' ', Qt::SkipEmptyParts);
        QString mechanism = credentials.mechanism.toUpper();
        if (mechanism.isEmpty() || mechanism == "AUTO"_L1)
            mechanism = offered.contains(u"PLAIN"_s) ? u"PLAIN"_s : u"LOGIN"_s;
        if (!offered.contains(mechanism)) {
            d.channel.fail(Failure::Rejected, u"Server does not offer AUTH %1"_s.arg(mechanism));
            return false;
        }
        const QByteArray user = credentials.user.toUtf8(), secret = credentials.secret.toUtf8();
        if (mechanism == "PLAIN"_L1) {
            const QByteArray payload = (QByteArray(1, '\0') + user + QByteArray(1, '\0') + secret).toBase64();
            d.channel.addSecret(QString::fromLatin1(payload));
            return d.channel.write("AUTH PLAIN " + payload + "\r\n", u"AUTH PLAIN <redacted>"_s)
                && d.expect(235, u"Authentication"_s);
        }
        if (mechanism == "LOGIN"_L1) {
            return d.channel.write("AUTH LOGIN\r\n") && d.expect(334, u"AUTH LOGIN"_s)
                && d.channel.write(user.toBase64() + "\r\n", u"<user>"_s) && d.expect(334, u"AUTH LOGIN"_s)
                && d.channel.write(secret.toBase64() + "\r\n", u"<redacted>"_s) && d.expect(235, u"Authentication"_s);
        }
        const QByteArray payload = ("user=" + user + "\x01" "auth=Bearer " + secret + "\x01\x01").toBase64();
        d.channel.addSecret(QString::fromLatin1(payload));
        int code = 0;
        QStringList lines;
        if (!d.channel.write("AUTH XOAUTH2 " + payload + "\r\n", u"AUTH XOAUTH2 <redacted>"_s) || !d.reply(&code, &lines)) return false;
        if (code == 334 && d.channel.write("\r\n")) d.reply(&code, &lines);
        if (code != 235 && d.channel.failure() == Failure::None)
            d.channel.fail(Failure::Rejected, u"Authentication refused: %1"_s.arg(code));
        return code == 235;
    };
    const bool ready = d.channel.open(endpoint) && d.expect(2, u"Greeting"_s) && d.ehlo(&capabilities);
    bool tls = true;
    if (ready && endpoint.security == "starttls"_L1) {
        if (!capabilities.contains(u"STARTTLS"_s)) {
            d.channel.fail(Failure::Rejected, u"Server does not offer STARTTLS; nothing was sent without encryption"_s);
            tls = false;
        } else {
            tls = d.channel.write("STARTTLS\r\n") && d.expect(2, u"STARTTLS"_s) && d.channel.startTls() && d.ehlo(&capabilities);
        }
    }
    if (ready && tls && auth() && d.channel.write("MAIL FROM:<" + from.toLatin1() + ">\r\n") && d.expect(2, u"MAIL FROM"_s)) {
        QStringList refused;
        for (const QString &recipient : recipients) {
            int code = 0;
            QStringList lines;
            if (!d.channel.write("RCPT TO:<" + recipient.toLatin1() + ">\r\n") || !d.reply(&code, &lines)) break;
            if (code / 100 != 2) refused.append(u"%1 (%2)"_s.arg(recipient).arg(code));
        }
        if (d.channel.failure() == Failure::None && !refused.isEmpty()) {
            int code = 0;
            QStringList lines;
            if (d.channel.write("RSET\r\n")) d.reply(&code, &lines);
            d.channel.fail(Failure::Rejected, u"Server refused recipients: "_s + refused.join(u", "_s));
        } else if (d.channel.failure() == Failure::None && d.channel.write("DATA\r\n") && d.expect(354, u"DATA"_s)
                   && d.channel.write(dotStuff(bytes), u"<message %1 bytes>"_s.arg(bytes.size())) && d.expect(2, u"Message"_s)) {
            delivered = true;
            int code = 0;
            QStringList lines;
            if (d.channel.write("QUIT\r\n")) d.reply(&code, &lines);
        }
    }
    const Failure failure = delivered ? Failure::None
        : d.channel.failure() == Failure::None ? Failure::Rejected : d.channel.failure();
    QVariantMap result{{u"ok"_s, delivered}, {u"state"_s, QindaTK::Protocol::failureState(failure)},
                       {u"error"_s, delivered ? QString() : QindaTK::Protocol::redact(d.channel.error(), {credentials.secret})},
                       {u"retryable"_s, !delivered && (failure == Failure::Offline || (d.lastCode >= 400 && d.lastCode < 500))},
                       {u"smtpCode"_s, d.lastCode}, {u"recipients"_s, recipients}};
    d.channel.close();
    return result;
}

} // namespace

SmtpRedirector::SmtpRedirector(const Server &server, QObject *parent)
    : QObject(parent)
    , m_server(server)
{
}

void SmtpRedirector::setSecret(const QString &secret, const QString &mechanism)
{
    m_secret = secret;
    m_mechanism = mechanism.isEmpty() ? u"auto"_s : mechanism;
}

void SmtpRedirector::clearSecret()
{
    m_secret.clear();
}

QByteArray SmtpRedirector::resent(const QByteArray &message, const QString &from, const QStringList &recipients,
                                  const QString &messageId)
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QByteArray block = "Resent-From: <" + from.toLatin1() + ">\r\nResent-To: ";
    for (qsizetype i = 0; i < recipients.size(); ++i) block += (i == 0 ? "<" : ",\r\n <") + recipients.at(i).toLatin1() + '>';
    block += "\r\nResent-Date: " + QLocale::c().toString(now, u"ddd, dd MMM yyyy HH:mm:ss"_s).toLatin1() + " +0000\r\n";
    block += "Resent-Message-ID: <" + messageId.toLatin1() + ">\r\n";
    return block + message;
}

void SmtpRedirector::refuse(const QString &requestId, const QString &error)
{
    const QVariantMap result{{u"requestId"_s, requestId}, {u"ok"_s, false}, {u"state"_s, u"rejected"_s},
                             {u"error"_s, error}, {u"retryable"_s, false}};
    QMetaObject::invokeMethod(this, [this, requestId, result] { emit redirectFinished(requestId, result); },
                              Qt::QueuedConnection);
}

bool SmtpRedirector::send(const QString &requestId, const QStringList &recipients, const QByteArray &message)
{
    if (m_server.host.isEmpty() || !QindaTK::Mime::validAddress(m_server.fromAddress)) {
        refuse(requestId, tr("The account cannot send mail"));
        return false;
    }
    QStringList checked;
    for (const QString &recipient : recipients) {
        if (!QindaTK::Mime::validAddress(recipient.trimmed())) {
            refuse(requestId, tr("“%1” is not an e-mail address").arg(recipient));
            return false;
        }
        if (!checked.contains(recipient.trimmed(), Qt::CaseInsensitive)) checked.append(recipient.trimmed());
    }
    if (checked.isEmpty() || message.isEmpty() || message.size() > kMaxMessage) {
        refuse(requestId, checked.isEmpty() ? tr("The redirect has no recipient") : tr("The message cannot be redirected"));
        return false;
    }
    const QString domain = m_server.fromAddress.section(u'@', 1);
    const QByteArray bytes = resent(message, m_server.fromAddress, checked,
                                    QUuid::createUuid().toString(QUuid::WithoutBraces) + u'@' + domain);
    const QindaTK::Protocol::Endpoint endpoint{m_server.host, static_cast<quint16>(m_server.port), m_server.security,
                                               m_server.timeoutMs, false};
    const QindaTK::Protocol::Credentials credentials{m_server.userName, m_secret, m_mechanism};
    const QString from = m_server.fromAddress;
    QindaTK::Protocol::startWorker(this, [endpoint, credentials, from, checked, bytes] {
        return run(endpoint, credentials, from, checked, bytes);
    }, [this, requestId](const QVariantMap &done) {
        QVariantMap result = done;
        result.insert(u"requestId"_s, requestId);
        emit redirectFinished(requestId, result);
    });
    return true;
}

} // namespace QindaOffice::Apps::Mail
