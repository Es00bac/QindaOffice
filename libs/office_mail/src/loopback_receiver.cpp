// SPDX-License-Identifier: GPL-3.0-or-later
#include "loopback_receiver.h"

#include <QTcpSocket>

namespace QindaOffice::Mail {
namespace {

QByteArray pageHtml(const LoopbackReceiver::Page &page)
{
    const QString html = QStringLiteral(
        "<!doctype html><html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>QindaMail sign-in</title>"
        "<style>body{font-family:sans-serif;max-width:32em;margin:4em auto;padding:0 1em;line-height:1.5}"
        "h1{font-size:1.4em}</style></head><body><h1>%1</h1><p>%2</p></body></html>")
                             .arg(page.heading.toHtmlEscaped(), page.text.toHtmlEscaped());
    return html.toUtf8();
}

} // namespace

LoopbackReceiver::LoopbackReceiver(Handler handler, QObject *parent)
    : QObject(parent)
    , m_handler(std::move(handler))
{
    connect(&m_server, &QTcpServer::newConnection, this, &LoopbackReceiver::onNewConnection);
}

LoopbackReceiver::~LoopbackReceiver()
{
    close();
}

bool LoopbackReceiver::listen(QString *error)
{
    // AGENT-NOTE: RFC 8252 §8.3 — the IP literal, not "localhost", so the
    // redirect can never be resolved to another interface.
    if (m_server.listen(QHostAddress::LocalHost, 0)) return true;
    if (error) *error = QStringLiteral("Cannot open the sign-in return port: %1").arg(m_server.errorString());
    return false;
}

QUrl LoopbackReceiver::redirectUri() const
{
    return QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(m_server.serverPort()));
}

void LoopbackReceiver::close()
{
    m_server.close();
    // Connections that never sent a complete request are dropped; answered
    // ones finish on their own (see onNewConnection).
    const auto sockets = m_buffers.keys();
    m_buffers.clear();
    for (QTcpSocket *socket : sockets) {
        socket->abort();
        socket->deleteLater();
    }
}

void LoopbackReceiver::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        // AGENT-GUARD: unparented, so the answer page is still delivered when
        // the session drops this receiver right after the handler ran; the
        // socket deletes itself once the browser has read it.
        socket->setParent(nullptr);
        m_buffers.insert(socket, {});
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { m_buffers.remove(socket); });
    }
}

void LoopbackReceiver::respond(QTcpSocket *socket, int status, const QByteArray &reason, const QByteArray &html)
{
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n";
    response += "Content-Type: text/html; charset=utf-8\r\n";
    response += "Content-Length: " + QByteArray::number(html.size()) + "\r\n";
    response += "Cache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\n\r\n";
    response += html;
    m_buffers.remove(socket);
    socket->write(response);
    socket->disconnectFromHost();
}

void LoopbackReceiver::onReadyRead(QTcpSocket *socket)
{
    if (!m_buffers.contains(socket)) return;
    QByteArray &buffer = m_buffers[socket];
    buffer += socket->readAll();
    const qsizetype end = buffer.indexOf("\r\n\r\n");
    if (end < 0) {
        if (buffer.size() > kMaxRequestBytes) respond(socket, 431, "Request Header Fields Too Large", {});
        return;
    }
    const QByteArray requestLine = buffer.left(buffer.indexOf("\r\n"));
    const QList<QByteArray> parts = requestLine.split(' ');
    if (parts.size() != 3 || !parts.at(2).startsWith("HTTP/1.")) {
        respond(socket, 400, "Bad Request", {});
        return;
    }
    if (parts.at(0) != "GET") {
        respond(socket, 405, "Method Not Allowed", {});
        return;
    }
    const QUrl target(QString::fromLatin1(parts.at(1)), QUrl::StrictMode);
    // Browsers also ask for /favicon.ico; only the redirect path counts.
    if (!target.isValid() || target.path() != QLatin1String("/") || m_done) {
        respond(socket, 404, "Not Found", {});
        return;
    }
    const QUrlQuery query(target);
    if (!query.hasQueryItem(QStringLiteral("code")) && !query.hasQueryItem(QStringLiteral("error"))) {
        respond(socket, 404, "Not Found", {});
        return;
    }
    m_done = true;
    m_server.close(); // one callback per flow
    // Taken out of the pending set first: the handler may close() (or
    // deleteLater) this receiver, which must not drop the answer page.
    m_buffers.remove(socket);
    const Page page = m_handler(query);
    respond(socket, 200, "OK", pageHtml(page));
}

} // namespace QindaOffice::Mail
