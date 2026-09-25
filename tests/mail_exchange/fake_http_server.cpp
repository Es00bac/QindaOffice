// SPDX-License-Identifier: GPL-3.0-or-later
#include "fake_http_server.h"

#include <QTcpSocket>
#include <QUrl>
#include <memory>

FakeHttpServer::FakeHttpServer(Handler handler, QObject *parent) : QObject(parent), m_handler(std::move(handler))
{
    m_server.listen(QHostAddress::LocalHost, 0);
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket *socket = m_server.nextPendingConnection()) {
            auto buffer = std::make_shared<QByteArray>();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                *buffer += socket->readAll();
                const qsizetype end = buffer->indexOf("\r\n\r\n");
                if (end < 0) return;
                Request request;
                const QList<QByteArray> lines = buffer->left(end).split('\n');
                for (qsizetype i = 1; i < lines.size(); ++i) {
                    const QByteArray line = lines.at(i).trimmed();
                    const qsizetype colon = line.indexOf(':');
                    if (colon > 0) request.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
                }
                const qsizetype length = request.headers.value("content-length").toLongLong();
                if (buffer->size() < end + 4 + length) return;
                const QList<QByteArray> first = lines.first().trimmed().split(' ');
                request.method = first.value(0);
                request.rawTarget = QString::fromLatin1(first.value(1));
                const QUrl target(QStringLiteral("http://127.0.0.1") + request.rawTarget);
                request.path = target.path(QUrl::FullyDecoded);
                request.query = QUrlQuery(target.query(QUrl::FullyDecoded));
                request.body = buffer->mid(end + 4, length);
                buffer->clear();
                respond(socket, request);
            });
        }
    });
}

QString FakeHttpServer::origin() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(port());
}

int FakeHttpServer::count(const QByteArray &method, const QString &pathPrefix) const
{
    int n = 0;
    for (const Request &request : requests) n += request.method == method && request.path.startsWith(pathPrefix);
    return n;
}

void FakeHttpServer::respond(QTcpSocket *socket, const Request &request)
{
    requests.append(request);
    Reply reply;
    if (!authorization.isEmpty() && request.headers.value("authorization") != authorization) {
        reply.status = 401;
        reply.headers.append(qMakePair(QByteArray("WWW-Authenticate"), QByteArray("Bearer")));
    } else {
        reply = m_handler(request);
    }
    QByteArray out = "HTTP/1.1 " + QByteArray::number(reply.status) + " Test\r\nConnection: close\r\n";
    for (const auto &[name, value] : reply.headers) out += name + ": " + value + "\r\n";
    out += "Content-Length: " + QByteArray::number(reply.body.size()) + "\r\n\r\n" + reply.body;
    socket->write(out);
    socket->flush();
    socket->disconnectFromHost();
}
