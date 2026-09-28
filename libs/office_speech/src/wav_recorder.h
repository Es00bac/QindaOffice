// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Keeps a live session's audio as a WAV file (M56, D-061: audio-linked
// notes — a section's [12:40] replays that moment). 16 kHz mono s16le,
// written as it arrives; the header's sizes are patched every few seconds
// and on close, so a crash still leaves a playable file.

#include <QFile>
#include <QString>

namespace QindaOffice::Speech {

class WavRecorder {
public:
    ~WavRecorder() { close(); }

    bool open(const QString &path, QString *error = nullptr);
    void append(const QByteArray &pcm);
    void close();
    [[nodiscard]] bool isOpen() const { return m_file.isOpen(); }
    [[nodiscard]] QString path() const { return m_file.fileName(); }
    [[nodiscard]] qint64 durationMs() const;

private:
    void patchHeader();

    QFile m_file;
    qint64 m_dataBytes = 0;
    qint64 m_sincePatch = 0;
};

} // namespace QindaOffice::Speech
