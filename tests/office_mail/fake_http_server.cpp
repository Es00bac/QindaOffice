// SPDX-License-Identifier: GPL-3.0-or-later
#include "fake_http_server.h"

#include <QTcpSocket>

#include <memory>

namespace QindaOffice::Mail::Test {

QString FakeRequest::form(const QString &key) const
{
    const QUrlQuery q(QString::fromLatin1(body));
    return q.queryItemValue(key, QUrl::FullyDecoded);
}

FakeHttpServer::FakeHttpServer(QObject *parent)
    : QTcpServer(parent)
{
    listen(QHostAddress::LocalHost, 0);
}

void FakeHttpServer::route(const QString &path, const FakeResponse &response)
{
    m_routes.insert(path, [response](const FakeRequest &) { return response; });
}

void FakeHttpServer::route(const QString &path, Handler handler)
{
    m_routes.insert(path, std::move(handler));
}

QUrl FakeHttpServer::url(const QString &pathAndQuery) const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(pathAndQuery));
}

QStringList FakeHttpServer::requestPaths() const
{
    QStringList paths;
    for (const FakeRequest &r : m_requests) paths.append(r.path);
    return paths;
}

void FakeHttpServer::incomingConnection(qintptr handle)
{
    auto *socket = new QTcpSocket(this);
    socket->setSocketDescriptor(handle);
    auto buffer = std::make_shared<QByteArray>();
    connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
        buffer->append(socket->readAll());
        serve(socket, buffer.get());
    });
    connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

void FakeHttpServer::serve(QTcpSocket *socket, QByteArray *buffer)
{
    const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
    if (headerEnd < 0) return;
    FakeRequest request;
    const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
    request.method = requestLine.value(0);
    const QUrl target(QString::fromLatin1(requestLine.value(1)));
    request.path = target.path();
    request.query = QUrlQuery(target);
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const qsizetype colon = lines.at(i).indexOf(':');
        if (colon > 0) request.headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
    }
    const qsizetype length = request.headers.value("content-length").toLongLong();
    if (buffer->size() < headerEnd + 4 + length) return; // body not complete yet
    request.body = buffer->mid(headerEnd + 4, length);
    buffer->clear();
    m_requests.append(request);

    const auto it = m_routes.constFind(request.path);
    FakeResponse response = it != m_routes.constEnd() ? (*it)(request) : FakeResponse{404, "text/plain", "not found"};
    if (response.hang) return;
    QByteArray reply = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n";
    reply += "Content-Type: " + response.contentType + "\r\n";
    reply += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
    reply += "Connection: close\r\n\r\n" + response.body;
    socket->write(reply);
    socket->disconnectFromHost();
}

} // namespace QindaOffice::Mail::Test
