// SPDX-License-Identifier: GPL-3.0-or-later
#include "pcm_mixer.h"

#include "speech_recognizer.h"

#include <QtEndian>

#include <algorithm>

namespace QindaOffice::Speech {

QByteArray PcmMixer::take(qsizetype samples)
{
    QByteArray out(samples * 2, Qt::Uninitialized);
    for (qsizetype i = 0; i < samples; ++i) {
        int sum = 0;
        for (QByteArray &buffer : m_buffers) {
            if ((i + 1) * 2 <= buffer.size()) {
                sum += qFromLittleEndian<qint16>(buffer.constData() + i * 2);
            }
        }
        // Saturate rather than wrap: a wrapped peak is a loud click that
        // the recognizers hear as a consonant.
        qToLittleEndian<qint16>(qint16(std::clamp(sum, -32768, 32767)), out.data() + i * 2);
    }
    for (QByteArray &buffer : m_buffers) {
        buffer.remove(0, qMin(buffer.size(), samples * 2));
    }
    return out;
}

QByteArray PcmMixer::push(int stream, const QByteArray &pcm)
{
    m_buffers[stream == 0 ? 0 : 1].append(pcm);
    const qsizetype a = m_buffers[0].size() / 2;
    const qsizetype b = m_buffers[1].size() / 2;
    const qsizetype lagLimit = qsizetype(kMaxLagMs) * kSampleRate / 1000;
    qsizetype samples = qMin(a, b);
    // One side has run ahead by more than the lag limit: mix up to where
    // it is minus the limit, the silent side contributing nothing.
    samples = qMax(samples, qMax(a, b) - lagLimit);
    if (samples <= 0) {
        return {};
    }
    return take(samples);
}

QByteArray PcmMixer::drain()
{
    const qsizetype samples = qMax(m_buffers[0].size(), m_buffers[1].size()) / 2;
    return samples > 0 ? take(samples) : QByteArray();
}

} // namespace QindaOffice::Speech
