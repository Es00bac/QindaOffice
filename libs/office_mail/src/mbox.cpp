// SPDX-License-Identifier: GPL-3.0-or-later
#include "mbox.h"

#include <QBuffer>
#include <QFile>

namespace QindaOffice::Mail {

namespace {

constexpr qint64 kLineChunk = 64 * 1024;

bool isSeparator(const QByteArray &line)
{
    return line.startsWith("From ");
}

// A separator after the first carries a ctime-style date ("From - Sat Jan  3
// 01:05:34 1996", "From x@y Sat Jan 03 01:05:34 +0000 2015"), so a body line
// "From the start…" after an empty line in an unescaped (mboxo) file does
// not split the message. AGENT-NOTE: the time "hh:mm" is what every writer
// we know of (Thunderbird, Postfix, Google Takeout, Apple Mail) puts there.
bool isLaterSeparator(const QByteArray &line)
{
    if (!isSeparator(line)) return false;
    for (qsizetype i = 5; i + 3 < line.size(); ++i) {
        const auto digit = [&line](qsizetype at) { return line.at(at) >= '0' && line.at(at) <= '9'; };
        if (line.at(i + 1) == ':' && digit(i) && digit(i + 2) && digit(i + 3)) return true;
    }
    return false;
}

bool isBlank(const QByteArray &line)
{
    return line == "\n" || line == "\r\n" || line.isEmpty();
}

// mboxrd: ">From ", ">>From ", … lose one ">".
void unescape(QByteArray *line)
{
    qsizetype i = 0;
    while (i < line->size() && line->at(i) == '>') ++i;
    if (i > 0 && line->mid(i, 5) == "From ") line->remove(0, 1);
}

} // namespace

MboxReader::MboxReader()
    : MboxReader(Limits{})
{
}

MboxReader::MboxReader(const Limits &limits)
    : m_limits(limits)
{
}

MboxReader::~MboxReader() = default;

bool MboxReader::looksLikeMbox(const QByteArray &head)
{
    qsizetype i = 0;
    while (i < head.size() && (head.at(i) == '\n' || head.at(i) == '\r')) ++i;
    return head.mid(i, 5) == "From ";
}

bool MboxReader::open(const QString &path)
{
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        m_error = QStringLiteral("Cannot read %1: %2").arg(path, file->errorString());
        return false;
    }
    m_device = std::move(file);
    return start();
}

bool MboxReader::openData(const QByteArray &data)
{
    auto buffer = std::make_unique<QBuffer>();
    buffer->setData(data);
    buffer->open(QIODevice::ReadOnly);
    m_device = std::move(buffer);
    return start();
}

bool MboxReader::start()
{
    if (!looksLikeMbox(m_device->peek(4096))) {
        m_error = QStringLiteral("This file is not an mbox mailbox (it does not start with a \"From \" line)");
        m_device.reset();
        return false;
    }
    QByteArray line;
    while (readLine(&line)) {
        if (isSeparator(line)) {
            m_havePending = true;
            m_pending = line;
            return true;
        }
    }
    m_error = QStringLiteral("The mailbox is empty");
    return false;
}

qint64 MboxReader::position() const
{
    return m_device != nullptr ? m_device->pos() : 0;
}

qint64 MboxReader::size() const
{
    return m_device != nullptr ? m_device->size() : 0;
}

bool MboxReader::readLine(QByteArray *line)
{
    // A line is read in bounded chunks; an overlong line arrives as several
    // pieces, each of which ends without '\n' except the last.
    line->clear();
    if (m_device == nullptr || m_atEnd) return false;
    *line = m_device->readLine(kLineChunk);
    if (line->isEmpty()) {
        m_atEnd = true;
        return false;
    }
    return true;
}

bool MboxReader::next(QByteArray *message)
{
    message->clear();
    if (!m_havePending || m_device == nullptr) return false;
    if (m_read >= m_limits.maxMessages) {
        m_limitReached = true;
        return false;
    }
    for (;;) {
        // The separator (m_pending) is dropped; collect up to the next one.
        m_havePending = false;
        QByteArray body;
        bool oversized = false;
        bool previousBlank = false;
        bool lineStart = true;
        QByteArray line;
        while (readLine(&line)) {
            const bool completeLine = line.endsWith('\n');
            if (lineStart && previousBlank && isLaterSeparator(line)) {
                m_pending = line;
                m_havePending = true;
                break;
            }
            if (lineStart) unescape(&line);
            previousBlank = lineStart && isBlank(line);
            lineStart = completeLine;
            if (oversized) continue;
            if (body.size() + line.size() > m_limits.maxMessageBytes) {
                oversized = true;
                body.clear();
                continue;
            }
            body += line;
        }
        if (oversized) {
            ++m_oversized;
            if (!m_havePending) return false;
            continue;
        }
        // The blank line before a separator belongs to the separator.
        if (m_havePending) {
            if (body.endsWith("\r\n")) body.chop(2);
            else if (body.endsWith('\n')) body.chop(1);
        }
        ++m_read;
        *message = body;
        return true;
    }
}

} // namespace QindaOffice::Mail
