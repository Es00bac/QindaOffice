// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A tiny IMAP4rev1 server on 127.0.0.1 for the M64 tests (D-072): enough of
// RFC 3501 for QindaTK's ImapMailProvider to sign in (AUTHENTICATE PLAIN with
// SASL-IR), SELECT INBOX, sync (UID FETCH … (UID FLAGS), (UID RFC822.SIZE),
// (UID FLAGS BODY.PEEK[])) and download sections — including the HEADER and
// TEXT sections QindaMail fetches to keep a message's source. Plain text,
// loopback only; every command line is recorded in `commands`.
// M81 (D-090): other mailboxes (`folders`), \Flagged and keywords in FLAGS,
// PERMANENTFLAGS with or without "\*" (`keywords`), UID STORE ±FLAGS, UID
// MOVE and UID COPY (MOVE and UIDPLUS advertised), UID EXPUNGE / EXPUNGE —
// what rules, categories and follow-up flags send.

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QMap>
#include <QObject>
#include <QRegularExpression>
#include <QStringList>
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
        bool flagged = false;
        QStringList keywords{};          // initialised, so {uid, source, seen} draws no -Wmissing-field-initializers
        bool deleted = false;
    };

    explicit FakeImapServer(QByteArray password, QObject *parent = nullptr)
        : QObject(parent)
        , m_password(std::move(password))
    {
        connect(&m_server, &QTcpServer::newConnection, this, &FakeImapServer::accept);
        m_server.listen(QHostAddress::LocalHost);
    }

    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }
    // The messages of a mailbox ("INBOX" is `inbox`).
    QList<Message> &box(const QString &name) { return name.compare(QStringLiteral("INBOX"), Qt::CaseInsensitive) == 0 ? inbox : folders[name]; }

    QList<Message> inbox;
    QMap<QString, QList<Message>> folders;
    QList<QByteArray> commands;
    qint64 uidValidity = 4242;
    bool keywords = true;         // PERMANENTFLAGS has "\*": new keywords are stored
    qint64 nextUid = 1000;

private:
    struct Session {
        QByteArray buffer;
        bool authenticated = false;
        QString selected = QStringLiteral("INBOX");
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
            socket->write("* OK [CAPABILITY IMAP4rev1 AUTH=PLAIN SASL-IR MOVE UIDPLUS] fake IMAP ready\r\n");
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
    static QString mailboxName(const QByteArray &argument) { return QString::fromUtf8(argument).remove(QLatin1Char('"')).trimmed(); }

    static QByteArray flagsOf(const Message &message)
    {
        QStringList flags;
        if (message.seen) flags << QStringLiteral("\\Seen");
        if (message.flagged) flags << QStringLiteral("\\Flagged");
        if (message.deleted) flags << QStringLiteral("\\Deleted");
        flags += message.keywords;
        return "FLAGS (" + flags.join(QLatin1Char(' ')).toUtf8() + ")";
    }

    void handle(QTcpSocket *socket, Session *session, const QByteArray &line)
    {
        const qsizetype space = line.indexOf(' ');
        const QByteArray tag = line.left(space);
        const QByteArray rest = space < 0 ? QByteArray() : line.mid(space + 1);
        const QByteArray upper = rest.toUpper();
        commands.append(upper.startsWith("AUTHENTICATE") ? QByteArray("AUTHENTICATE") : rest);
        const auto ok = [&](const QByteArray &what) { socket->write(tag + " OK " + what + " completed\r\n"); };
        if (upper == "CAPABILITY") {
            socket->write("* CAPABILITY IMAP4rev1 AUTH=PLAIN SASL-IR MOVE UIDPLUS\r\n");
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
            session->selected = mailboxName(rest.mid(7));
            socket->write("* FLAGS (\\Seen \\Flagged \\Deleted)\r\n* " + QByteArray::number(box(session->selected).size())
                          + " EXISTS\r\n* OK [UIDVALIDITY " + QByteArray::number(uidValidity) + "] UIDs valid\r\n"
                          + "* OK [PERMANENTFLAGS (\\Seen \\Flagged \\Deleted" + (keywords ? " \\*" : "") + ")] Limited\r\n");
            socket->write(tag + " OK [READ-WRITE] SELECT completed\r\n");
        } else if (upper.startsWith("UID FETCH ")) {
            fetch(socket, session, rest.mid(10));
            ok("FETCH");
        } else if (upper.startsWith("UID STORE ")) {
            store(socket, tag, session, rest.mid(10));
        } else if (upper.startsWith("UID MOVE ") || upper.startsWith("UID COPY ")) {
            const bool move = upper.startsWith("UID MOVE ");
            const QList<QByteArray> parts = rest.mid(9).split(' ');
            const qint64 uid = parts.value(0).toLongLong();
            QList<Message> &from = box(session->selected);
            QList<Message> &to = box(mailboxName(rest.mid(9 + parts.value(0).size() + 1)));
            for (qsizetype i = 0; i < from.size(); ++i) {
                if (from.at(i).uid != uid) continue;
                Message copy = from.at(i);
                copy.uid = ++nextUid;
                to.append(copy);
                if (move) from.removeAt(i);
                break;
            }
            ok(move ? "MOVE" : "COPY");
        } else if (upper.startsWith("UID EXPUNGE") || upper == "EXPUNGE") {
            QList<Message> &list = box(session->selected);
            list.removeIf([](const Message &m) { return m.deleted; });
            ok("EXPUNGE");
        } else {
            socket->write(tag + " BAD Unknown command\r\n");
        }
    }

    void store(QTcpSocket *socket, const QByteArray &tag, Session *session, const QByteArray &arguments)
    {
        static const QRegularExpression re(QStringLiteral("^(\\d+) ([+-])FLAGS(\\.SILENT)? \\(([^)]*)\\)$"),
                                           QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = re.match(QString::fromUtf8(arguments));
        if (!m.hasMatch()) {
            socket->write(tag + " BAD STORE syntax\r\n");
            return;
        }
        const qint64 uid = m.captured(1).toLongLong();
        const bool add = m.captured(2) == QLatin1String("+");
        const QStringList flags = m.captured(4).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &flag : flags) {
            if (!flag.startsWith(QLatin1Char('\\')) && add && !keywords) {
                socket->write(tag + " NO [CANNOT] Keywords are not supported\r\n");
                return;
            }
        }
        for (Message &message : box(session->selected)) {
            if (message.uid != uid) continue;
            for (const QString &flag : flags) {
                if (flag.compare(QStringLiteral("\\Seen"), Qt::CaseInsensitive) == 0) message.seen = add;
                else if (flag.compare(QStringLiteral("\\Flagged"), Qt::CaseInsensitive) == 0) message.flagged = add;
                else if (flag.compare(QStringLiteral("\\Deleted"), Qt::CaseInsensitive) == 0) message.deleted = add;
                else if (add && !message.keywords.contains(flag, Qt::CaseInsensitive)) message.keywords.append(flag);
                else if (!add) message.keywords.removeIf([&flag](const QString &k) { return k.compare(flag, Qt::CaseInsensitive) == 0; });
            }
        }
        socket->write(tag + " OK STORE completed\r\n");
    }

    void fetch(QTcpSocket *socket, Session *session, const QByteArray &arguments)
    {
        const qsizetype space = arguments.indexOf(' ');
        const QByteArray set = arguments.left(space);
        const QByteArray items = arguments.mid(space + 1).toUpper();
        const QList<Message> &list = box(session->selected);
        qint64 maxUid = 0;
        for (const Message &message : list) maxUid = std::max(maxUid, message.uid);
        // "a:b", "a:*" or a comma list.
        const auto wanted = [&](qint64 uid) {
            for (const QByteArray &piece : set.split(',')) {
                const QList<QByteArray> range = piece.split(':');
                qint64 low = range.value(0).toLongLong();
                const qint64 high = range.size() < 2 ? low : range.value(1) == "*" ? maxUid : range.value(1).toLongLong();
                if (range.value(1) == "*" && low > maxUid) low = maxUid;
                if (uid >= std::min(low, high) && uid <= std::max(low, high)) return true;
            }
            return false;
        };
        for (qsizetype i = 0; i < list.size(); ++i) {
            const Message &message = list.at(i);
            if (!wanted(message.uid)) continue;
            QByteArray response = "* " + QByteArray::number(i + 1) + " FETCH (UID " + QByteArray::number(message.uid);
            if (items.contains("FLAGS")) response += ' ' + flagsOf(message);
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
