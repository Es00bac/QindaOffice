// SPDX-License-Identifier: GPL-3.0-or-later
#include "sse_parser.h"

namespace QindaOffice::Ai {

void SseParser::append(const QByteArray &bytes)
{
    m_buffer.append(bytes);
    int start = 0;
    while (true) {
        const int newline = m_buffer.indexOf('\n', start);
        if (newline < 0) {
            break;
        }
        QByteArray line = m_buffer.mid(start, newline - start);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        dispatchLine(line);
        start = newline + 1;
    }
    m_buffer.remove(0, start);
}

void SseParser::finish()
{
    if (!m_buffer.isEmpty()) {
        QByteArray line = m_buffer;
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        dispatchLine(line);
        m_buffer.clear();
    }
    if (!m_dataLines.isEmpty() || !m_eventType.isEmpty()) {
        dispatchEvent();
    }
}

void SseParser::dispatchLine(const QByteArray &line)
{
    if (line.isEmpty()) {
        dispatchEvent();
        return;
    }
    if (line.startsWith(':')) {
        return; // comment / keep-alive
    }
    const int colon = line.indexOf(':');
    const QByteArray field = colon < 0 ? line : line.left(colon);
    QByteArray value = colon < 0 ? QByteArray() : line.mid(colon + 1);
    if (value.startsWith(' ')) {
        value.remove(0, 1);
    }
    if (field == "event") {
        m_eventType = QString::fromUtf8(value);
    } else if (field == "data") {
        m_dataLines.append(QString::fromUtf8(value));
    }
    // id:/retry: are tracked by neither consumers nor this v1 parser.
}

void SseParser::dispatchEvent()
{
    if (m_dataLines.isEmpty()) {
        m_eventType.clear();
        return;
    }
    const QString data = m_dataLines.join(QLatin1Char('\n'));
    const QString type = m_eventType.isEmpty() ? QStringLiteral("message") : m_eventType;
    m_eventType.clear();
    m_dataLines.clear();
    if (data == QLatin1String("[DONE]")) {
        m_sawDone = true;
    }
    m_callback(type, data);
}

} // namespace QindaOffice::Ai
