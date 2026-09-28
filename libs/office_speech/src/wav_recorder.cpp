// SPDX-License-Identifier: GPL-3.0-or-later
#include "wav_recorder.h"

#include "http_transcription.h"
#include "speech_recognizer.h"

#include <QDir>
#include <QFileInfo>
#include <QtEndian>

namespace QindaOffice::Speech {

namespace {
constexpr qint64 kPatchEveryBytes = 5LL * 1000 * kBytesPerMs; // every 5 s of audio
}

bool WavRecorder::open(const QString &path, QString *error)
{
    close();
    QDir().mkpath(QFileInfo(path).absolutePath());
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error != nullptr) {
            *error = m_file.errorString();
        }
        return false;
    }
    m_dataBytes = 0;
    m_sincePatch = 0;
    m_file.write(HttpTranscription::wavFile(QByteArray())); // the 44-byte header
    return true;
}

void WavRecorder::append(const QByteArray &pcm)
{
    if (!m_file.isOpen() || pcm.isEmpty()) {
        return;
    }
    m_file.write(pcm);
    m_dataBytes += pcm.size();
    m_sincePatch += pcm.size();
    if (m_sincePatch >= kPatchEveryBytes) {
        patchHeader();
    }
}

void WavRecorder::patchHeader()
{
    const qint64 at = m_file.pos();
    char size[4];
    qToLittleEndian<quint32>(quint32(36 + m_dataBytes), size);
    m_file.seek(4);
    m_file.write(size, 4);
    qToLittleEndian<quint32>(quint32(m_dataBytes), size);
    m_file.seek(40);
    m_file.write(size, 4);
    m_file.seek(at);
    m_file.flush();
    m_sincePatch = 0;
}

void WavRecorder::close()
{
    if (!m_file.isOpen()) {
        return;
    }
    patchHeader();
    m_file.close();
}

qint64 WavRecorder::durationMs() const
{
    return m_dataBytes / kBytesPerMs;
}

} // namespace QindaOffice::Speech
