// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A tiny POP3 or SMTP server on 127.0.0.1 for tst_qindamail_flow: enough of
// RFC 1939 (CAPA, USER/PASS, UIDL, LIST, RETR, DELE, QUIT) and RFC 5321
// (EHLO, AUTH PLAIN, MAIL, RCPT, DATA, RSET, QUIT) for the toolkit's
// providers to sync and send, with a password check so sign-in failures
// can be exercised. Plain text only, loopback only (the providers allow
// credentials in clear text to loopback, D-187 there). Runs on the test's
// event loop; the providers' blocking sockets live on worker threads.

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace MailTest {

class FakeMailServer : public QObject {
    Q_OBJECT
public:
    enum class Kind { Pop3, Smtp };

    FakeMailServer(Kind kind, QByteArray password, QObject *parent = nullptr)
        : QObject(parent)
        , m_kind(kind)
        , m_password(std::move(password))
    {
        connect(&m_server, &QTcpServer::newConnection, this, &FakeMailServer::accept);
        m_server.listen(QHostAddress::LocalHost);
    }

    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    QList<QByteArray> mailbox;      // POP3: raw messages (CRLF lines)
    QList<QByteArray> delivered;    // SMTP: DATA payloads
    QList<QByteArray> recipients;   // SMTP: RCPT TO arguments
    int logins = 0;                 // authentication attempts (either kind)

private:
    struct Session {
        QByteArray buffer;
        QByteArray data;
        bool inData = false;
        bool pendingPlain = false;
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
            socket->write(m_kind == Kind::Pop3 ? "+OK fake POP3 ready\r\n" : "220 localhost fake ESMTP\r\n");
        }
    }

    void read(QTcpSocket *socket, Session *session)
    {
        session->buffer += socket->readAll();
        qsizetype end;
        while ((end = session->buffer.indexOf("\r\n")) >= 0) {
            const QByteArray line = session->buffer.left(end);
            session->buffer.remove(0, end + 2);
            if (m_kind == Kind::Pop3) pop3(socket, line);
            else smtp(socket, session, line);
        }
    }

    void pop3(QTcpSocket *socket, const QByteArray &line)
    {
        const qsizetype space = line.indexOf(' ');
        const QByteArray verb = (space < 0 ? line : line.left(space)).toUpper();
        const QByteArray arg = space < 0 ? QByteArray() : line.mid(space + 1);
        if (verb == "CAPA") {
            socket->write("+OK\r\nUSER\r\nUIDL\r\n.\r\n");
        } else if (verb == "USER") {
            socket->write("+OK\r\n");
        } else if (verb == "PASS") {
            ++logins;
            socket->write(arg == m_password ? "+OK logged in\r\n" : "-ERR [AUTH] invalid password\r\n");
        } else if (verb == "UIDL" || verb == "LIST") {
            QByteArray out = "+OK\r\n";
            for (qsizetype i = 0; i < mailbox.size(); ++i) {
                out += QByteArray::number(i + 1) + ' '
                    + (verb == "UIDL" ? QByteArray("uid-") + QByteArray::number(i + 1)
                                      : QByteArray::number(mailbox.at(i).size()))
                    + "\r\n";
            }
            socket->write(out + ".\r\n");
        } else if (verb == "RETR") {
            const qsizetype n = arg.toInt() - 1;
            if (n < 0 || n >= mailbox.size()) {
                socket->write("-ERR no such message\r\n");
                return;
            }
            QByteArray body = mailbox.at(n);
            body.replace("\r\n.", "\r\n..");
            socket->write("+OK\r\n" + body + (body.endsWith("\r\n") ? "" : "\r\n") + ".\r\n");
        } else if (verb == "QUIT") {
            socket->write("+OK bye\r\n");
            socket->disconnectFromHost();
        } else if (verb == "DELE" || verb == "NOOP" || verb == "RSET" || verb == "STAT") {
            socket->write("+OK\r\n");
        } else {
            socket->write("-ERR unknown\r\n");
        }
    }

    void smtp(QTcpSocket *socket, Session *session, const QByteArray &line)
    {
        if (session->inData) {
            if (line == ".") {
                session->inData = false;
                delivered.append(session->data);
                session->data.clear();
                socket->write("250 queued\r\n");
            } else {
                session->data += (line.startsWith("..") ? line.mid(1) : line) + "\r\n";
            }
            return;
        }
        const auto checkPlain = [this, socket](const QByteArray &payload) {
            ++logins;
            const QList<QByteArray> parts = QByteArray::fromBase64(payload).split('\0');
            socket->write(parts.size() == 3 && parts.at(2) == m_password ? "235 2.7.0 ok\r\n"
                                                                         : "535 5.7.8 bad credentials\r\n");
        };
        if (session->pendingPlain) {
            session->pendingPlain = false;
            checkPlain(line);
            return;
        }
        const QByteArray upper = line.toUpper();
        if (upper.startsWith("EHLO") || upper.startsWith("HELO")) {
            socket->write("250-localhost\r\n250-AUTH PLAIN\r\n250 OK\r\n");
        } else if (upper.startsWith("AUTH PLAIN")) {
            const QByteArray payload = line.mid(10).trimmed();
            if (payload.isEmpty()) {
                session->pendingPlain = true;
                socket->write("334 \r\n");
            } else {
                checkPlain(payload);
            }
        } else if (upper.startsWith("MAIL FROM") || upper.startsWith("RSET") || upper.startsWith("NOOP")) {
            socket->write("250 ok\r\n");
        } else if (upper.startsWith("RCPT TO")) {
            recipients.append(line.mid(8));
            socket->write("250 ok\r\n");
        } else if (upper == "DATA") {
            session->inData = true;
            socket->write("354 go ahead\r\n");
        } else if (upper == "QUIT") {
            socket->write("221 bye\r\n");
            socket->disconnectFromHost();
        } else {
            socket->write("502 unknown\r\n");
        }
    }

    Kind m_kind;
    QByteArray m_password;
    QTcpServer m_server;
};

} // namespace MailTest
