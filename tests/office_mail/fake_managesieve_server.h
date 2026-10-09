// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A small ManageSieve server (RFC 5804) on 127.0.0.1 for the M81 tests
// (D-090): the capability greeting, AUTHENTICATE PLAIN (initial response)
// and LOGIN (challenges), LISTSCRIPTS, GETSCRIPT (a literal), CHECKSCRIPT
// and PUTSCRIPT (client {n+} literals), SETACTIVE, DELETESCRIPT, LOGOUT.
// Its "Sieve check" refuses a script that uses an extension it does not
// `require`, or requires one the server lacks — enough to prove QindaMail's
// require line and block are right. Plain text, loopback only; every
// command is recorded in `commands` (script bodies as byte counts).

#include <QByteArray>
#include <QHostAddress>
#include <QMap>
#include <QObject>
#include <QRegularExpression>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>

namespace MailTest {

// AGENT-NOTE: no Q_OBJECT (lambdas only), so other test directories can
// include it without running moc over this header.
class FakeManageSieveServer : public QObject {
public:
    explicit FakeManageSieveServer(QByteArray password, QObject *parent = nullptr)
        : QObject(parent)
        , m_password(std::move(password))
    {
        connect(&m_server, &QTcpServer::newConnection, this, &FakeManageSieveServer::accept);
        m_server.listen(QHostAddress::LocalHost);
    }

    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    QMap<QString, QByteArray> scripts;
    QString active;
    QStringList extensions{QStringLiteral("fileinto"), QStringLiteral("vacation"), QStringLiteral("date"),
                           QStringLiteral("relational"), QStringLiteral("envelope")};
    QByteArray sasl = "PLAIN LOGIN";
    bool offerStartTls = false;
    bool version = true;
    QList<QByteArray> commands;
    int checks = 0;

private:
    struct Session {
        QByteArray buffer;
        bool authenticated = false;
        int loginStep = 0;
        QByteArray loginUser;
    };

    void accept()
    {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            auto *session = new Session;
            connect(socket, &QTcpSocket::disconnected, socket, [socket, session] {
                delete session;
                socket->deleteLater();
            });
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, session] { read(socket, session); });
            socket->write(greeting());
        }
    }

    QByteArray greeting() const
    {
        QByteArray out = "\"IMPLEMENTATION\" \"Fake Pigeonhole\"\r\n\"SIEVE\" \"" + extensions.join(QLatin1Char(' ')).toUtf8()
            + "\"\r\n\"SASL\" \"" + sasl + "\"\r\n";
        if (offerStartTls) out += "\"STARTTLS\"\r\n";
        if (version) out += "\"VERSION\" \"1.0\"\r\n";
        return out + "OK \"Ready\"\r\n";
    }

    // One complete command (with its literal) at the front of the buffer, or false.
    static bool takeCommand(QByteArray &buffer, QByteArray *line, QByteArray *literal)
    {
        const qsizetype end = buffer.indexOf("\r\n");
        if (end < 0) return false;
        QByteArray first = buffer.left(end);
        static const QRegularExpression marker(QStringLiteral("\\{(\\d+)\\+?\\}$"));
        const QRegularExpressionMatch m = marker.match(QString::fromUtf8(first));
        if (!m.hasMatch()) {
            *line = first;
            literal->clear();
            buffer.remove(0, end + 2);
            return true;
        }
        const qsizetype size = m.captured(1).toLongLong();
        const qsizetype after = buffer.indexOf("\r\n", end + 2 + size);
        if (buffer.size() < end + 2 + size || after < 0) return false;
        *literal = buffer.mid(end + 2, size);
        *line = first.left(first.size() - m.capturedLength()).trimmed();
        buffer.remove(0, after + 2);
        return true;
    }

    static QList<QByteArray> quotedArgs(const QByteArray &line)
    {
        QList<QByteArray> out;
        for (qsizetype i = 0; i < line.size(); ++i) {
            if (line.at(i) != '"') continue;
            QByteArray value;
            for (++i; i < line.size() && line.at(i) != '"'; ++i) {
                if (line.at(i) == '\\' && i + 1 < line.size()) ++i;
                value += line.at(i);
            }
            out.append(value);
        }
        return out;
    }

    // The server's Sieve check: every used extension required and offered.
    QByteArray check(const QByteArray &script) const
    {
        const QString text = QString::fromUtf8(script);
        QStringList required;
        static const QRegularExpression requireRe(QStringLiteral("require\\s*(\\[[^\\]]*\\]|\"[^\"]*\")"));
        for (auto it = requireRe.globalMatch(text); it.hasNext();) {
            static const QRegularExpression word(QStringLiteral("\"([^\"]+)\""));
            for (auto w = word.globalMatch(it.next().captured(1)); w.hasNext();) required.append(w.next().captured(1));
        }
        for (const QString &extension : std::as_const(required))
            if (!extensions.contains(extension)) return "NO \"line 1: unsupported extension " + extension.toUtf8() + "\"";
        const auto uses = [&text](const char *command) {
            return text.contains(QRegularExpression(QStringLiteral("(^|[;{}\\s])%1\\s").arg(QLatin1String(command))));
        };
        if (uses("vacation") && !required.contains(QStringLiteral("vacation"))) return "NO \"vacation used without require\"";
        if (uses("currentdate") && !required.contains(QStringLiteral("date"))) return "NO \"currentdate used without require\"";
        if (text.contains(QStringLiteral(":value")) && !required.contains(QStringLiteral("relational")))
            return "NO \":value used without require\"";
        if (text.contains(QStringLiteral("SYNTAX ERROR"))) return "NO \"line 3: syntax error\"";
        return "OK";
    }

    void read(QTcpSocket *socket, Session *session)
    {
        session->buffer += socket->readAll();
        QByteArray line, literal;
        while (takeCommand(session->buffer, &line, &literal)) handle(socket, session, line, literal);
    }

    void handle(QTcpSocket *socket, Session *session, const QByteArray &line, const QByteArray &literal)
    {
        if (session->loginStep > 0) {
            const QByteArray value = QByteArray::fromBase64(quotedArgs(line).value(0));
            commands.append("<login answer>");
            if (session->loginStep == 1) {
                session->loginUser = value;
                session->loginStep = 2;
                socket->write("\"UGFzc3dvcmQ6\"\r\n");
            } else {
                session->loginStep = 0;
                session->authenticated = value == m_password;
                socket->write(session->authenticated ? "OK \"Logged in\"\r\n" : "NO \"Authentication failed\"\r\n");
            }
            return;
        }
        const QByteArray upper = line.toUpper();
        const QList<QByteArray> args = quotedArgs(line);
        commands.append(upper.startsWith("AUTHENTICATE") ? QByteArray("AUTHENTICATE ") + args.value(0)
                        : literal.isEmpty()              ? line
                                                         : line + " {" + QByteArray::number(literal.size()) + "}");
        if (upper.startsWith("AUTHENTICATE")) {
            if (args.value(0).toUpper() == "LOGIN") {
                session->loginStep = 1;
                socket->write("\"VXNlcm5hbWU6\"\r\n");
                return;
            }
            const QList<QByteArray> fields = QByteArray::fromBase64(args.value(1)).split('\0');
            session->authenticated = args.value(0).toUpper() == "PLAIN" && fields.size() == 3 && fields.at(2) == m_password;
            socket->write(session->authenticated ? "OK \"Logged in\"\r\n" : "NO \"Authentication failed\"\r\n");
        } else if (upper == "LOGOUT") {
            socket->write("OK \"Bye\"\r\n");
            socket->disconnectFromHost();
        } else if (upper == "CAPABILITY") {
            socket->write(greeting());
        } else if (!session->authenticated) {
            socket->write("NO \"Authenticate first\"\r\n");
        } else if (upper == "LISTSCRIPTS") {
            for (auto it = scripts.cbegin(); it != scripts.cend(); ++it)
                socket->write("\"" + it.key().toUtf8() + "\"" + (it.key() == active ? " ACTIVE" : "") + "\r\n");
            socket->write("OK \"Listscripts completed\"\r\n");
        } else if (upper.startsWith("GETSCRIPT")) {
            const QString name = QString::fromUtf8(args.value(0));
            if (!scripts.contains(name)) {
                socket->write("NO (NONEXISTENT) \"There is no script by that name\"\r\n");
            } else {
                const QByteArray body = scripts.value(name);
                socket->write("{" + QByteArray::number(body.size()) + "}\r\n" + body + "\r\nOK \"Getscript completed\"\r\n");
            }
        } else if (upper.startsWith("CHECKSCRIPT")) {
            ++checks;
            socket->write(check(literal) + "\r\n");
        } else if (upper.startsWith("PUTSCRIPT")) {
            const QByteArray verdict = check(literal);
            if (verdict == "OK") scripts.insert(QString::fromUtf8(args.value(0)), literal);
            socket->write(verdict + "\r\n");
        } else if (upper.startsWith("SETACTIVE")) {
            const QString name = QString::fromUtf8(args.value(0));
            if (!name.isEmpty() && !scripts.contains(name)) {
                socket->write("NO (NONEXISTENT) \"No such script\"\r\n");
            } else {
                active = name;
                socket->write("OK\r\n");
            }
        } else if (upper.startsWith("DELETESCRIPT")) {
            const QString name = QString::fromUtf8(args.value(0));
            if (name == active) {
                socket->write("NO (ACTIVE) \"You may not delete an active script\"\r\n");
            } else {
                scripts.remove(name);
                socket->write("OK\r\n");
            }
        } else {
            socket->write("NO \"Unknown command\"\r\n");
        }
    }

    QTcpServer m_server;
    QByteArray m_password;
};

} // namespace MailTest
