// SPDX-License-Identifier: GPL-3.0-or-later
#include "mock_sse_server.h"

#include <QTcpSocket>
#include <QTimer>
#include <QJsonDocument>
#include <QRegularExpression>
#include <memory>

namespace QindaOffice::Ai {

MockSseServer::MockSseServer(QObject *parent)
    : QTcpServer(parent)
{
    listen(QHostAddress::LocalHost);
}

void MockSseServer::incomingConnection(qintptr handle)
{
    auto *socket = new QTcpSocket(this);
    socket->setSocketDescriptor(handle);
    const auto input = std::make_shared<QByteArray>();
    const auto recorded = std::make_shared<bool>(false);
    connect(socket, &QTcpSocket::readyRead, this, [this, socket, input, recorded] {
        if (*recorded) { static_cast<void>(socket->readAll()); return; }
        *input += socket->readAll();
        if (input->size() > 1048576) { *recorded = true; return; }
        const auto end = input->indexOf("\r\n\r\n");
        if (end < 0) return;
        static const QRegularExpression length(QStringLiteral("Content-Length: ([0-9]+)"),
            QRegularExpression::CaseInsensitiveOption);
        const auto match = length.match(QString::fromLatin1(input->left(end)));
        if (!match.hasMatch()) return;
        const int bytes = match.captured(1).toInt();
        if (input->size() < end + 4 + bytes) return;
        const auto body = QJsonDocument::fromJson(input->mid(end + 4, bytes));
        if (body.isObject()) m_requests.append(body.object());
        *recorded = true;
    });
    // HTTP headers first (the providers ignore the body framing).
    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\n\r\n");
    int total = 0;
    for (const SseScriptStep &step : m_steps) {
        total += qMax(1, step.delayMs);
        QTimer::singleShot(total, socket, [socket, bytes = step.bytes] {
            if (socket->state() == QAbstractSocket::ConnectedState) {
                socket->write(bytes);
            }
        });
    }
    // End the stream: clients only finish when the connection closes.
    QTimer::singleShot(total + 100, socket, [this, socket] {
        if (!m_autoClose) {
            return;
        }
        if (socket->state() == QAbstractSocket::ConnectedState) {
            socket->disconnectFromHost();
        }
    });
}

} // namespace QindaOffice::Ai
