// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support (M76, header-only): a CardDAV server on 127.0.0.1 for
// tst_mail_carddav and tst_qindamail_carddav — never a real server.
//
//   /.well-known/carddav   301 → /dav/
//   /dav/                  current-user-principal /dav/principals/ada/
//   /dav/principals/ada/   addressbook-home-set /dav/books/ada/
//   /dav/books/ada/<book>/ address books: getctag, sync-token, the
//                          sync-collection (switchable) and addressbook-multiget
//                          reports, GET/PUT/DELETE with ETags, If-Match and
//                          If-None-Match: *
//
// Every request needs `Authorization: Basic user:password` (or `Bearer
// token`); `rejectNext` answers that many requests with 401 regardless.
// Sync tokens are "https://fake.test/sync/<n>" over a change log; a token
// older than `oldestToken` is refused with 403 valid-sync-token.

#include <QHash>
#include <QList>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <functional>
#include <memory>

namespace QindaOffice::Mail::Test {

class FakeCardDavServer final : public QTcpServer {
public:
    struct Card {
        QByteArray vcard;
        QString etag;
    };
    struct Book {
        QString name;
        QMap<QString, Card> cards;               // file name → card
        QList<QPair<int, QString>> log;          // (version, file name) of every change
        int version = 1;
    };
    struct Request {
        QByteArray method;
        QString path;
        QHash<QByteArray, QByteArray> headers;   // lower-case names
        QByteArray body;
    };

    QString user = QStringLiteral("ada");
    QString password = QStringLiteral("s3cret");
    QString bearer;                              // when set, Bearer this token is accepted too
    bool syncCollection = true;
    bool frozenCtag = false;                     // a server whose ctag never moves (forces the 412 path)
    int oldestToken = 0;
    int rejectNext = 0;
    QMap<QString, Book> books;                   // book path segment → book
    QList<Request> requests;

    FakeCardDavServer() { listen(QHostAddress::LocalHost, 0); }

    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(serverPort()); }
    QString bookPath(const QString &book) const { return QStringLiteral("/dav/books/ada/%1/").arg(book); }
    QString bookUrl(const QString &book) const { return base() + bookPath(book); }

    // Server-side edits (another device).
    QString putCard(const QString &book, const QString &file, const QByteArray &vcard)
    {
        Book &b = books[book];
        const QString etag = QStringLiteral("\"e%1\"").arg(++m_etags);
        b.cards.insert(file, {vcard, etag});
        b.log.append({++b.version, file});
        return etag;
    }
    void removeCard(const QString &book, const QString &file)
    {
        Book &b = books[book];
        b.cards.remove(file);
        b.log.append({++b.version, file});
    }
    int count(const QByteArray &method) const
    {
        int n = 0;
        for (const Request &r : requests) n += r.method == method ? 1 : 0;
        return n;
    }

protected:
    void incomingConnection(qintptr handle) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
            buffer->append(socket->readAll());
            serve(socket, buffer.get());
        });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }

private:
    struct Reply {
        int status = 200;
        QMap<QByteArray, QByteArray> headers;
        QByteArray body;
    };

    static QByteArray xml(const QByteArray &responses, const QByteArray &tail = {})
    {
        return "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<d:multistatus xmlns:d=\"DAV:\" "
               "xmlns:card=\"urn:ietf:params:xml:ns:carddav\" xmlns:cs=\"http://calendarserver.org/ns/\">"
            + responses + tail + "</d:multistatus>";
    }
    static QByteArray response(const QString &href, const QByteArray &props, const QByteArray &status = "HTTP/1.1 200 OK")
    {
        return "<d:response><d:href>" + href.toUtf8().toPercentEncoding("/:.-_~") + "</d:href><d:propstat><d:prop>" + props
            + "</d:prop><d:status>" + status + "</d:status></d:propstat></d:response>";
    }
    QByteArray token(const Book &b) const { return "https://fake.test/sync/" + QByteArray::number(b.version); }
    QByteArray bookProps(const Book &b) const
    {
        QByteArray reports = "<d:supported-report><d:report><card:addressbook-multiget/></d:report></d:supported-report>";
        if (syncCollection) reports += "<d:supported-report><d:report><d:sync-collection/></d:report></d:supported-report>";
        return "<d:resourcetype><d:collection/><card:addressbook/></d:resourcetype><d:displayname>" + b.name.toUtf8()
            + "</d:displayname><cs:getctag>ctag-" + (frozenCtag ? QByteArray("frozen") : QByteArray::number(b.version)) + "</cs:getctag>"
            + (syncCollection ? "<d:sync-token>" + token(b) + "</d:sync-token>" : QByteArray())
            + "<d:supported-report-set>" + reports + "</d:supported-report-set>";
    }

    bool authorized(const Request &r)
    {
        if (rejectNext > 0) {
            --rejectNext;
            return false;
        }
        const QByteArray header = r.headers.value("authorization");
        if (!bearer.isEmpty() && header == "Bearer " + bearer.toUtf8()) return true;
        return header == "Basic " + (user + QLatin1Char(':') + password).toUtf8().toBase64();
    }

    Reply handle(const Request &r)
    {
        if (!authorized(r)) return {401, {{"WWW-Authenticate", "Basic realm=\"fake\""}}, "unauthorized"};
        const QString path = r.path;
        if (path == QLatin1String("/.well-known/carddav")) return {301, {{"Location", "/dav/"}}, {}};
        if (r.method == "PROPFIND" && path == QLatin1String("/dav/"))
            return {207, {}, xml(response(path, "<d:current-user-principal><d:href>/dav/principals/ada/</d:href></d:current-user-principal>"
                                                "<d:resourcetype><d:collection/></d:resourcetype>"))};
        if (r.method == "PROPFIND" && path == QLatin1String("/dav/principals/ada/"))
            return {207, {}, xml(response(path, "<card:addressbook-home-set><d:href>/dav/books/ada/</d:href></card:addressbook-home-set>"
                                                "<d:resourcetype><d:principal/></d:resourcetype>"))};
        if (r.method == "PROPFIND" && path == QLatin1String("/dav/books/ada/")) {
            QByteArray all = response(path, "<d:resourcetype><d:collection/></d:resourcetype>");
            for (auto it = books.cbegin(); it != books.cend(); ++it) all += response(bookPath(it.key()), bookProps(it.value()));
            return {207, {}, xml(all)};
        }
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("^/dav/books/ada/([^/]+)/([^/]*)$")).match(path);
        if (!m.hasMatch() || !books.contains(m.captured(1))) return {404, {}, "not found"};
        const QString file = m.captured(2);
        if (file.isEmpty()) return collection(r, books[m.captured(1)]);
        return member(r, m.captured(1), file);
    }

    Reply collection(const Request &r, Book &b)
    {
        if (r.method == "PROPFIND") {
            QByteArray all = response(r.path, bookProps(b));
            if (r.headers.value("depth") == "1") {
                for (auto it = b.cards.cbegin(); it != b.cards.cend(); ++it)
                    all += response(r.path + it.key(), "<d:getetag>" + it->etag.toUtf8() + "</d:getetag><d:resourcetype/>");
            }
            return {207, {}, xml(all)};
        }
        if (r.method == "REPORT" && r.body.contains("sync-collection")) {
            if (!syncCollection) return {501, {}, "not implemented"};
            const QRegularExpressionMatch t = QRegularExpression(QStringLiteral("sync-token>([^<]*)<")).match(QString::fromUtf8(r.body));
            const QString sent = t.hasMatch() ? t.captured(1) : QString();
            int since = 0;
            if (!sent.isEmpty()) {
                since = sent.section(QLatin1Char('/'), -1).toInt();
                if (!sent.startsWith(QLatin1String("https://fake.test/sync/")) || since < oldestToken || since > b.version)
                    return {403, {}, "<d:error xmlns:d=\"DAV:\"><d:valid-sync-token/></d:error>"};
            }
            QByteArray all;
            QSet<QString> seen;
            for (const auto &[version, name] : b.log) {
                if (version <= since || seen.contains(name)) continue;
                seen.insert(name);
            }
            if (since == 0) for (auto it = b.cards.cbegin(); it != b.cards.cend(); ++it) seen.insert(it.key());
            for (const QString &name : std::as_const(seen)) {
                const QString href = r.path + name;
                if (b.cards.contains(name)) all += response(href, "<d:getetag>" + b.cards.value(name).etag.toUtf8() + "</d:getetag>");
                else if (since > 0) all += "<d:response><d:href>" + href.toUtf8() + "</d:href><d:status>HTTP/1.1 404 Not Found</d:status></d:response>";
            }
            return {207, {}, xml(all, "<d:sync-token>" + token(b) + "</d:sync-token>")};
        }
        if (r.method == "REPORT" && r.body.contains("addressbook-multiget")) {
            QByteArray all;
            const QRegularExpression href(QStringLiteral("<[A-Za-z]*:?href>([^<]*)</[A-Za-z]*:?href>"));
            for (auto it = href.globalMatch(QString::fromUtf8(r.body)); it.hasNext();) {
                const QString wanted = QUrl::fromPercentEncoding(it.next().captured(1).toUtf8());
                const QString name = wanted.section(QLatin1Char('/'), -1);
                if (!b.cards.contains(name)) {
                    all += "<d:response><d:href>" + wanted.toUtf8() + "</d:href><d:status>HTTP/1.1 404 Not Found</d:status></d:response>";
                    continue;
                }
                const Card &card = b.cards.value(name);
                all += response(wanted, "<d:getetag>" + card.etag.toUtf8() + "</d:getetag><card:address-data>"
                                            + QString::fromUtf8(card.vcard).toHtmlEscaped().toUtf8() + "</card:address-data>");
            }
            return {207, {}, xml(all)};
        }
        return {405, {}, "method not allowed"};
    }

    Reply member(const Request &r, const QString &book, const QString &file)
    {
        Book &b = books[book];
        const bool exists = b.cards.contains(file);
        const QByteArray match = r.headers.value("if-match");
        if (r.method == "GET") {
            if (!exists) return {404, {}, "not found"};
            return {200, {{"ETag", b.cards.value(file).etag.toUtf8()}, {"Content-Type", "text/vcard"}}, b.cards.value(file).vcard};
        }
        if (r.method == "PUT") {
            if (r.headers.value("if-none-match") == "*" && exists) return {412, {}, "exists"};
            if (!match.isEmpty() && (!exists || b.cards.value(file).etag.toUtf8() != match)) return {412, {}, "changed"};
            return {exists ? 204 : 201, {{"ETag", putCard(book, file, r.body).toUtf8()}}, {}};
        }
        if (r.method == "DELETE") {
            if (!exists) return {404, {}, "not found"};
            if (!match.isEmpty() && b.cards.value(file).etag.toUtf8() != match) return {412, {}, "changed"};
            b.cards.remove(file);
            b.log.append({++b.version, file});
            return {204, {}, {}};
        }
        return {405, {}, "method not allowed"};
    }

    void serve(QTcpSocket *socket, QByteArray *buffer)
    {
        const qsizetype end = buffer->indexOf("\r\n\r\n");
        if (end < 0) return;
        Request request;
        const QList<QByteArray> lines = buffer->left(end).split('\n');
        const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
        request.method = first.value(0);
        request.path = QUrl::fromPercentEncoding(QUrl(QString::fromLatin1(first.value(1))).path(QUrl::FullyEncoded).toUtf8());
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const qsizetype colon = lines.at(i).indexOf(':');
            if (colon > 0) request.headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
        }
        const qsizetype length = request.headers.value("content-length").toLongLong();
        if (buffer->size() < end + 4 + length) return;
        request.body = buffer->mid(end + 4, length);
        buffer->clear();
        requests.append(request);
        const Reply reply = handle(request);
        QByteArray out = "HTTP/1.1 " + QByteArray::number(reply.status) + " X\r\n";
        for (auto it = reply.headers.cbegin(); it != reply.headers.cend(); ++it) out += it.key() + ": " + it.value() + "\r\n";
        if (reply.status == 207) out += "Content-Type: application/xml; charset=utf-8\r\n";
        out += "Content-Length: " + QByteArray::number(reply.body.size()) + "\r\nConnection: close\r\n\r\n" + reply.body;
        socket->write(out);
        socket->disconnectFromHost();
    }

    int m_etags = 0;
};

} // namespace QindaOffice::Mail::Test
