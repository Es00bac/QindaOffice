// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Server-sent-events parser (plan section 5.3): pure byte-buffer machine,
// unit-tested against scripted servers that split chunks mid-line and
// mid-UTF-8. Feed append(); every complete event dispatches through the
// callback with its data lines joined and the last event type recorded.

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <functional>

namespace QindaOffice::Ai {

class SseParser {
public:
    using EventCallback = std::function<void(const QString &event, const QString &data)>;

    explicit SseParser(EventCallback callback) : m_callback(std::move(callback)) {}

    void append(const QByteArray &bytes);
    // Flushes: anything buffered without a trailing blank line is dropped
    // (an SSE stream ends with a complete event or [DONE]).
    void finish();

    [[nodiscard]] bool sawDone() const { return m_sawDone; }

private:
    void dispatchEvent();
    void dispatchLine(const QByteArray &line);

    QByteArray m_buffer;
    QString m_eventType;
    QStringList m_dataLines;
    EventCallback m_callback;
    bool m_sawDone = false;
};

} // namespace QindaOffice::Ai
