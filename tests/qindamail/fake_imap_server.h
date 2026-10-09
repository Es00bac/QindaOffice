// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A tiny IMAP4rev1 server on 127.0.0.1 for the M64 tests (D-072): enough of
// RFC 3501 for QindaTK's ImapMailProvider to sign in (AUTHENTICATE PLAIN with
// SASL-IR), SELECT INBOX, sync (UID FETCH … (UID FLAGS), (UID RFC822.SIZE),
// (UID FLAGS BODY.PEEK[])) and download sections — including the HEADER and
// TEXT sections QindaMail fetches to keep a message's source. Plain text,
// loopback only, one INBOX; every command line is recorded in `commands`.

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>

namespace MailTest {

class FakeImapServer : public QObject {
    Q_OBJECT
public:
    struct Message {
        qint64 uid = 0;
        QByteArray source;
        bool seen = false;
    };

    explicit FakeImapServer(QByteArray password, QObject *parent = nullptr)
        : QObject(parent)
        , m_password(std::move(password))
    {
        connect(&m_server, &QTcpServer::newConnection, this, &FakeImapServer::accept);
        m_server.listen(QHostAddress::LocalHost);
    }

    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    QList<Message> inbox;
    QList<QByteArray> commands;
    qint64 uidValidity = 4242;

private:
    struct Session {
        QByteArray buffer;
        bool authenticated = false;
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
            socket->write("* OK [CAPABILITY IMAP4rev1 AUTH=PLAIN SASL-IR] fake IMAP ready\r\n");
        }
    }

    void read(QTcpSocket *socket, Session *session)
    {
        session->buffer += socket->readAll();
        qsizetype end = 0;
        while ((end = session->buffer.indexOf("\r\n")) >= 0) {
            const QByteArray line = session->buffer.left(end);
            session->buffer.remove(0, end + 2);
            handle(socket, session, line);
        }
    }

    static QByteArray literal(const QByteArray &bytes) { return "{" + QByteArray::number(bytes.size()) + "}\r\n" + bytes; }

    void handle(QTcpSocket *socket, Session *session, const QByteArray &line)
    {
        const qsizetype space = line.indexOf(' ');
        const QByteArray tag = line.left(space);
        const QByteArray rest = space < 0 ? QByteArray() : line.mid(space + 1);
        const QByteArray upper = rest.toUpper();
        commands.append(upper.startsWith("AUTHENTICATE") ? QByteArray("AUTHENTICATE") : rest);
        const auto ok = [&](const QByteArray &what) { socket->write(tag + " OK " + what + " completed\r\n"); };
        if (upper == "CAPABILITY") {
            socket->write("* CAPABILITY IMAP4rev1 AUTH=PLAIN SASL-IR\r\n");
            ok("CAPABILITY");
        } else if (upper.startsWith("AUTHENTICATE PLAIN ")) {
            const QList<QByteArray> fields = QByteArray::fromBase64(rest.mid(19)).split('\0');
            session->authenticated = fields.size() == 3 && fields.at(2) == m_password;
            socket->write(session->authenticated ? tag + " OK AUTHENTICATE completed\r\n"
                                                 : tag + " NO [AUTHENTICATIONFAILED] Invalid credentials\r\n");
        } else if (upper == "LOGOUT") {
            socket->write("* BYE logging out\r\n" + tag + " OK LOGOUT completed\r\n");
            socket->disconnectFromHost();
        } else if (!session->authenticated) {
            socket->write(tag + " BAD Authenticate first\r\n");
        } else if (upper.startsWith("SELECT ")) {
            socket->write("* FLAGS (\\Seen \\Flagged \\Deleted)\r\n* " + QByteArray::number(inbox.size()) + " EXISTS\r\n"
                          "* OK [UIDVALIDITY " + QByteArray::number(uidValidity) + "] UIDs valid\r\n");
            socket->write(tag + " OK [READ-WRITE] SELECT completed\r\n");
        } else if (upper.startsWith("UID FETCH ")) {
            fetch(socket, rest.mid(10));
            ok("FETCH");
        } else {
            socket->write(tag + " BAD Unknown command\r\n");
        }
    }

    void fetch(QTcpSocket *socket, const QByteArray &arguments)
    {
        const qsizetype space = arguments.indexOf(' ');
        const QList<QByteArray> range = arguments.left(space).split(':');
        const QByteArray items = arguments.mid(space + 1).toUpper();
        qint64 maxUid = 0;
        for (const Message &message : std::as_const(inbox)) maxUid = std::max(maxUid, message.uid);
        qint64 low = range.value(0).toLongLong();
        const qint64 high = range.size() < 2 ? low : range.value(1) == "*" ? maxUid : range.value(1).toLongLong();
        if (range.value(1) == "*" && low > maxUid) low = maxUid;
        for (qsizetype i = 0; i < inbox.size(); ++i) {
            const Message &message = inbox.at(i);
            if (message.uid < std::min(low, high) || message.uid > std::max(low, high)) continue;
            QByteArray response = "* " + QByteArray::number(i + 1) + " FETCH (UID " + QByteArray::number(message.uid);
            if (items.contains("FLAGS")) response += message.seen ? " FLAGS (\\Seen)" : " FLAGS ()";
            if (items.contains("RFC822.SIZE")) response += " RFC822.SIZE " + QByteArray::number(message.source.size());
            static const QRegularExpression section(QStringLiteral("BODY\\.PEEK\\[([^\\]]*)\\]"));
            const QRegularExpressionMatch match = section.match(QString::fromLatin1(items));
            if (match.hasMatch()) {
                const QByteArray name = match.captured(1).toLatin1();
                const qsizetype split = message.source.indexOf("\r\n\r\n");
                const QByteArray body = name.isEmpty() ? message.source
                    : name == "HEADER"                 ? message.source.left(split + 4)
                    : name == "TEXT"                   ? message.source.mid(split + 4)
                                                       : QByteArray();
                response += " BODY[" + name + "] " + literal(body);
            }
            socket->write(response + ")\r\n");
        }
    }

    QTcpServer m_server;
    QByteArray m_password;
};

} // namespace MailTest
