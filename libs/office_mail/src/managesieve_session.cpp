// SPDX-License-Identifier: GPL-3.0-or-later
// The ManageSieve dialogue (managesieve_session.h).
#include "managesieve_session.h"

namespace QindaOffice::Mail::ManageSieve {

using QindaTK::Protocol::Failure;
using namespace Qt::StringLiterals;

namespace {

constexpr qint64 kMaxLiteral = 4 * 1024 * 1024;   // a script, a SASL challenge

} // namespace

bool Session::send(const QByteArray &line, const QString &shown)
{
    return m_channel->write(line + "\r\n", shown.isEmpty() ? QString() : shown);
}

QByteArray Session::quotedArgument(const QString &text)
{
    QByteArray out = text.toUtf8();
    out.replace('\\', "\\\\").replace('"', "\\\"");
    out.replace('\r', ' ').replace('\n', ' ');
    return '"' + out + '"';
}

QByteArray Session::literalArgument(const QString &text)
{
    const QByteArray bytes = text.toUtf8();
    return '{' + QByteArray::number(bytes.size()) + "+}\r\n" + bytes;
}

bool Session::readTokens(QList<Token> *tokens)
{
    tokens->clear();
    QByteArray line;
    for (;;) {
        if (!m_channel->readLine(&line)) return false;
        bool literal = false;
        qsizetype i = 0;
        const qsizetype n = line.size();
        while (i < n) {
            const char c = line.at(i);
            if (c == ' ' || c == '\t') {
                ++i;
            } else if (c == '"') {
                QByteArray text;
                for (++i; i < n && line.at(i) != '"'; ++i) {
                    if (line.at(i) == '\\' && i + 1 < n) ++i;
                    text += line.at(i);
                }
                ++i;
                tokens->append({Token::String, text});
            } else if (c == '{') {
                const qsizetype close = line.indexOf('}', i);
                QByteArray number = close < 0 ? QByteArray() : line.mid(i + 1, close - i - 1);
                if (number.endsWith('+')) number.chop(1);
                bool ok = false;
                const qint64 count = number.toLongLong(&ok);
                if (close != n - 1 || !ok || count < 0 || count > kMaxLiteral) {
                    m_channel->fail(Failure::Rejected, u"Malformed ManageSieve literal"_s);
                    return false;
                }
                QByteArray bytes;
                if (!m_channel->readBytes(count, &bytes)) return false;
                tokens->append({Token::String, bytes});
                literal = true;
                break;
            } else if (c == '(') {
                const qsizetype close = line.indexOf(')', i);
                const qsizetype stop = close < 0 ? n : close;
                tokens->append({Token::Code, line.mid(i + 1, stop - i - 1)});
                i = stop + 1;
            } else {
                const qsizetype start = i;
                while (i < n && line.at(i) != ' ' && line.at(i) != '(') ++i;
                tokens->append({Token::Atom, line.mid(start, i - start)});
            }
        }
        // After a literal the logical line goes on with the next physical one.
        if (!literal) return true;
    }
}

bool Session::response(QList<QList<Token>> *data, bool required)
{
    for (int lines = 0; lines < 100000; ++lines) {
        QList<Token> tokens;
        if (!readTokens(&tokens)) return false;
        if (tokens.isEmpty()) continue;
        const Token &first = tokens.first();
        const QByteArray status = first.kind == Token::Atom ? first.text.toUpper() : QByteArray();
        if (status == "OK" || status == "NO" || status == "BYE") {
            QStringList text;
            for (qsizetype i = 1; i < tokens.size(); ++i) text.append(QString::fromUtf8(tokens.at(i).text));
            m_lastStatus = text.join(u' ');
            if (status == "BYE") {
                m_channel->fail(Failure::Rejected, u"The ManageSieve server closed the session: "_s + m_lastStatus);
                return false;
            }
            if (status == "NO" && required) {
                m_channel->fail(Failure::Rejected, u"The ManageSieve server refused: "_s + m_lastStatus);
                return false;
            }
            return status == "OK";
        }
        if (data != nullptr) data->append(tokens);
    }
    m_channel->fail(Failure::Rejected, u"The ManageSieve answer is too long"_s);
    return false;
}

bool Session::capabilities(const QList<QList<Token>> &lines)
{
    m_extensions.clear();
    m_sasl.clear();
    m_startTls = false;
    m_version.clear();
    for (const QList<Token> &line : lines) {
        if (line.isEmpty()) continue;
        const QByteArray name = line.first().text.toUpper();
        const QString value = line.size() > 1 ? QString::fromUtf8(line.at(1).text) : QString();
        if (name == "SIEVE") m_extensions = value.toLower().split(u' ', Qt::SkipEmptyParts);
        else if (name == "SASL") m_sasl = value.toUpper().split(u' ', Qt::SkipEmptyParts);
        else if (name == "STARTTLS") m_startTls = true;
        else if (name == "IMPLEMENTATION") m_implementation = value;
        else if (name == "VERSION") m_version = value;
    }
    return true;
}

bool Session::open(const QindaTK::Protocol::Endpoint &endpoint, const QindaTK::Protocol::Credentials &credentials)
{
    if (!m_channel->open(endpoint)) return false;
    QList<QList<Token>> lines;
    if (!response(&lines)) return false;
    capabilities(lines);
    if (endpoint.security == "starttls"_L1) {
        if (!m_startTls) {
            // AGENT-GUARD: never fall back to clear text when TLS was asked for.
            m_channel->fail(Failure::Rejected, u"The ManageSieve server does not offer STARTTLS; nothing was sent"_s);
            return false;
        }
        lines.clear();
        if (!send("STARTTLS") || !response() || !m_channel->startTls() || !response(&lines)) return false;
        capabilities(lines);   // RFC 5804 §2.2: sent again after the handshake
    }
    return !credentials.isSet() || authenticate(credentials);
}

bool Session::authenticate(const QindaTK::Protocol::Credentials &credentials)
{
    if (!m_channel->authAllowed()) {
        m_channel->fail(Failure::Rejected, u"Refusing to send credentials over an unencrypted connection"_s);
        return false;
    }
    const QString wanted = credentials.mechanism.toUpper();
    const bool token = wanted == "XOAUTH2"_L1 || wanted == "BEARER"_L1 || wanted == "OAUTHBEARER"_L1;
    QString mechanism;
    if (token) mechanism = m_sasl.contains(u"OAUTHBEARER"_s) ? u"OAUTHBEARER"_s : u"XOAUTH2"_s;
    else mechanism = m_sasl.contains(u"PLAIN"_s) || !m_sasl.contains(u"LOGIN"_s) ? u"PLAIN"_s : u"LOGIN"_s;
    if (!m_sasl.isEmpty() && !m_sasl.contains(mechanism)) {
        m_channel->fail(Failure::Rejected, u"The ManageSieve server does not offer AUTHENTICATE %1"_s.arg(mechanism));
        return false;
    }
    const QByteArray user = credentials.user.toUtf8(), secret = credentials.secret.toUtf8();
    QByteArray initial;
    if (mechanism == "PLAIN"_L1) initial = (QByteArray(1, '\0') + user + QByteArray(1, '\0') + secret).toBase64();
    else if (mechanism == "XOAUTH2"_L1) initial = ("user=" + user + "\x01" "auth=Bearer " + secret + "\x01\x01").toBase64();
    else if (mechanism == "OAUTHBEARER"_L1)
        initial = ("n,a=" + user + ",\x01" "auth=Bearer " + secret + "\x01\x01").toBase64();
    if (!initial.isEmpty()) m_channel->addSecret(QString::fromLatin1(initial));
    QByteArray command = "AUTHENTICATE " + quotedArgument(mechanism);
    if (!initial.isEmpty()) command += " \"" + initial + '"';
    if (!send(command, u"AUTHENTICATE \"%1\" <redacted>"_s.arg(mechanism))) return false;
    for (int step = 0; step < 4; ++step) {
        QList<Token> tokens;
        if (!readTokens(&tokens)) return false;
        if (tokens.isEmpty()) continue;
        if (tokens.first().kind == Token::String) {
            // A server challenge: LOGIN's prompts, or a token error to acknowledge.
            QByteArray answer;
            if (mechanism == "LOGIN"_L1) answer = (step == 0 ? user : secret).toBase64();
            else if (mechanism == "OAUTHBEARER"_L1) answer = QByteArray("\x01").toBase64();
            if (!answer.isEmpty()) m_channel->addSecret(QString::fromLatin1(answer));
            if (!send('"' + answer + '"', u"<redacted>"_s)) return false;
            continue;
        }
        const QByteArray status = tokens.first().text.toUpper();
        QStringList text;
        for (qsizetype i = 1; i < tokens.size(); ++i) text.append(QString::fromUtf8(tokens.at(i).text));
        m_lastStatus = QindaTK::Protocol::redact(text.join(u' '), {credentials.secret});
        if (status == "OK") return true;
        m_channel->fail(Failure::Rejected, u"AUTHENTICATE failed: "_s + m_lastStatus);
        return false;
    }
    m_channel->fail(Failure::Rejected, u"AUTHENTICATE did not finish"_s);
    return false;
}

bool Session::listScripts(QList<Script> *scripts)
{
    QList<QList<Token>> lines;
    if (!send("LISTSCRIPTS") || !response(&lines)) return false;
    scripts->clear();
    for (const QList<Token> &line : std::as_const(lines)) {
        if (line.isEmpty() || line.first().kind != Token::String) continue;
        Script script;
        script.name = QString::fromUtf8(line.first().text);
        script.active = line.size() > 1 && line.at(1).text.toUpper() == "ACTIVE";
        scripts->append(script);
    }
    return true;
}

bool Session::getScript(const QString &name, QString *script)
{
    QList<QList<Token>> lines;
    if (!send("GETSCRIPT " + quotedArgument(name)) || !response(&lines)) return false;
    script->clear();
    if (!lines.isEmpty() && !lines.first().isEmpty()) *script = QString::fromUtf8(lines.first().first().text);
    return true;
}

bool Session::checkScript(const QString &script)
{
    return send("CHECKSCRIPT " + literalArgument(script), u"CHECKSCRIPT {%1 bytes}"_s.arg(script.toUtf8().size()))
        && response(nullptr, false);
}

bool Session::putScript(const QString &name, const QString &script)
{
    return send("PUTSCRIPT " + quotedArgument(name) + ' ' + literalArgument(script),
                u"PUTSCRIPT %1 {%2 bytes}"_s.arg(QString::fromUtf8(quotedArgument(name))).arg(script.toUtf8().size()))
        && response(nullptr, false);
}

bool Session::setActive(const QString &name)
{
    return send("SETACTIVE " + quotedArgument(name)) && response(nullptr, false);
}

bool Session::deleteScript(const QString &name)
{
    return send("DELETESCRIPT " + quotedArgument(name)) && response(nullptr, false);
}

void Session::logout()
{
    if (!failed() && send("LOGOUT")) response(nullptr, false);
}

} // namespace QindaOffice::Mail::ManageSieve
