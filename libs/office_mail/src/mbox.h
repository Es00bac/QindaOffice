// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Unix mbox files (M64, D-072): the export format of Thunderbird, Apple
// Mail, Google Takeout and most Unix mail tools. A pull reader, so the app
// can import a large file a few messages per event-loop turn.
//
// AGENT-NOTE: mboxo and mboxrd (and Thunderbird's "From - <date>") differ
// only in how body lines starting with "From " were escaped; the files do
// not say which one they are. Separators are "From " lines at the start of
// the file or after an empty line (the RFC 4155 rule), and every body line
// matching ^>+From loses ONE ">" (mboxrd unescaping, which is also correct
// for mboxo's single level). A literal ">>From" in an mboxo body therefore
// reads back as ">From" — the accepted ambiguity of the format.
//
// AGENT-GUARD: untrusted input. A message is cut off at maxMessageBytes (and
// reported as oversized, skipped), the reader stops after maxMessages, and a
// line longer than the message bound is never buffered whole.

#include <QByteArray>
#include <QFile>
#include <QString>

#include <memory>

namespace QindaOffice::Mail {

class MboxReader {
public:
    struct Limits {
        qint64 maxMessageBytes = 64 * 1024 * 1024;
        int maxMessages = 100000;
    };

    MboxReader();
    explicit MboxReader(const Limits &limits);
    ~MboxReader();

    // Opens a file; false (with error()) when it cannot be read or does not
    // start like an mbox ("From " within the first bytes, after blank lines).
    bool open(const QString &path);
    // From memory (tests).
    bool openData(const QByteArray &data);

    // The next message's RFC 5322 bytes (the "From " line removed, ">From"
    // unescaped, line endings as in the file). False at the end, at the
    // message limit, or on a read error (see error()).
    bool next(QByteArray *message);

    [[nodiscard]] int messagesRead() const { return m_read; }
    [[nodiscard]] int oversized() const { return m_oversized; }
    [[nodiscard]] bool limitReached() const { return m_limitReached; }
    [[nodiscard]] qint64 position() const;
    [[nodiscard]] qint64 size() const;
    [[nodiscard]] QString error() const { return m_error; }

    // True when `head` (the first bytes of a file) looks like an mbox.
    static bool looksLikeMbox(const QByteArray &head);

private:
    bool readLine(QByteArray *line);
    bool start();

    Limits m_limits;
    std::unique_ptr<QIODevice> m_device;
    QByteArray m_pending;        // a separator line read ahead
    bool m_havePending = false;
    bool m_atEnd = false;
    bool m_limitReached = false;
    int m_read = 0;
    int m_oversized = 0;
    QString m_error;
};

} // namespace QindaOffice::Mail
