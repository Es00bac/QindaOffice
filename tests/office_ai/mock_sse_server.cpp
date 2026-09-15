// SPDX-License-Identifier: GPL-3.0-or-later
#include "mock_sse_server.h"

#include <QTcpSocket>
#include <QTimer>

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
