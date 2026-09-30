// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Scripted SSE test server (plan section 5.3): a QTcpServer that replays
// byte chunks with delays, splitting lines and multi-byte UTF-8 sequences
// across chunk boundaries. The client must reassemble events exactly.

#include <QTcpServer>
#include <QJsonObject>

namespace QindaOffice::Ai {

struct SseScriptStep {
    QByteArray bytes;
    int delayMs = 0;
};

class MockSseServer final : public QTcpServer {
    Q_OBJECT
public:
    explicit MockSseServer(QObject *parent = nullptr);

    // Sets the reply bytes the next connection will receive, in order.
    void setScript(QVector<SseScriptStep> steps) { m_steps = std::move(steps); }
    // Streams that stay open (cancel tests) disable the auto-close.
    void setAutoClose(bool enabled) { m_autoClose = enabled; }
    [[nodiscard]] quint16 port() const { return serverPort(); }
    [[nodiscard]] QList<QJsonObject> requests() const { return m_requests; }

protected:
    void incomingConnection(qintptr handle) override;

private:
    QVector<SseScriptStep> m_steps;
    bool m_autoClose = true;
    QList<QJsonObject> m_requests;
};

} // namespace QindaOffice::Ai
