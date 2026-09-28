// SPDX-License-Identifier: GPL-3.0-or-later
#include "speech_segmenter.h"

#include "speech_recognizer.h"

#include <QtEndian>

#include <cmath>

namespace QindaOffice::Speech {

namespace {

constexpr qsizetype kFrameBytes = SpeechSegmenter::kFrameMs * kBytesPerMs;
constexpr qsizetype kPreRollBytes = SpeechSegmenter::kPreRollMs * kBytesPerMs;
// Speech must be this many times the noise floor, and never quieter than
// kMinSpeechRms (a silent room's floor is near zero; a factor alone would
// call the hum speech).
constexpr double kOnsetFactor = 3.0;
constexpr double kMinSpeechRms = 0.012;

double frameRms(const QByteArray &frame)
{
    double sum = 0;
    const qsizetype samples = frame.size() / 2;
    for (qsizetype i = 0; i < samples; ++i) {
        const double s = qFromLittleEndian<qint16>(frame.constData() + i * 2) / 32768.0;
        sum += s * s;
    }
    return samples > 0 ? std::sqrt(sum / double(samples)) : 0.0;
}

} // namespace

QList<AudioClip> SpeechSegmenter::feed(const QByteArray &pcm)
{
    QList<AudioClip> out;
    m_carry.append(pcm);
    qsizetype offset = 0;
    while (m_carry.size() - offset >= kFrameBytes) {
        processFrame(m_carry.mid(offset, kFrameBytes), &out);
        offset += kFrameBytes;
    }
    m_carry.remove(0, offset);
    return out;
}

QList<AudioClip> SpeechSegmenter::flush()
{
    QList<AudioClip> out;
    if (m_inSpeech) {
        m_clip.append(m_carry);
        m_positionMs += m_carry.size() / kBytesPerMs;
        closeClip(&out);
    }
    m_carry.clear();
    return out;
}

void SpeechSegmenter::processFrame(const QByteArray &frame, QList<AudioClip> *out)
{
    const double rms = frameRms(frame);
    const bool loud = rms > qMax(kMinSpeechRms, m_noiseFloor * kOnsetFactor);
    const qint64 frameStart = m_positionMs;
    m_positionMs += kFrameMs;

    if (!m_inSpeech) {
        if (loud) {
            m_inSpeech = true;
            m_clip = m_preRoll + frame;
            m_clipStartMs = frameStart - m_preRoll.size() / kBytesPerMs;
            m_preRoll.clear();
            m_speechMs = kFrameMs;
            m_silenceMs = 0;
            return;
        }
        // Track the floor slowly upwards, quickly downwards.
        m_noiseFloor = rms < m_noiseFloor ? m_noiseFloor * 0.9 + rms * 0.1
                                          : m_noiseFloor * 0.995 + rms * 0.005;
        m_preRoll.append(frame);
        if (m_preRoll.size() > kPreRollBytes) {
            m_preRoll.remove(0, m_preRoll.size() - kPreRollBytes);
        }
        return;
    }

    m_clip.append(frame);
    if (loud) {
        m_speechMs += kFrameMs;
        m_silenceMs = 0;
    } else {
        m_silenceMs += kFrameMs;
    }
    const qint64 length = m_positionMs - m_clipStartMs;
    if (m_silenceMs >= kSilenceMs || length >= kMaxMs) {
        closeClip(out);
    }
}

void SpeechSegmenter::closeClip(QList<AudioClip> *out)
{
    if (m_speechMs >= kMinSpeechMs) {
        out->append({m_clip, m_clipStartMs, m_positionMs});
    }
    m_clip.clear();
    m_inSpeech = false;
    m_speechMs = 0;
    m_silenceMs = 0;
}

} // namespace QindaOffice::Speech
