// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Cuts a continuous 16 kHz mono stream into utterances for the backends
// that transcribe one clip at a time (local whisper, HTTP transcription).
// ElevenLabs realtime does its own voice-activity detection and does not
// use this.
//
// Energy-based, deliberately simple: an adaptive noise floor, speech when
// a 30 ms frame is well above it, an utterance ends after kSilenceMs of
// quiet or at kMaxMs (a lecturer who never pauses still gets transcribed,
// in 20 s pieces). kPreRollMs of audio before the onset is kept so the
// first consonant is not clipped. Pure: the tests feed synthetic tones.

#include <QByteArray>
#include <QList>

namespace QindaOffice::Speech {

struct AudioClip {
    QByteArray pcm;
    qint64 startMs = 0;
    qint64 endMs = 0;
};

class SpeechSegmenter {
public:
    static constexpr int kFrameMs = 30;
    static constexpr int kSilenceMs = 700;
    static constexpr int kMinSpeechMs = 350;
    static constexpr int kMaxMs = 20000;
    static constexpr int kPreRollMs = 240;

    // Feeds audio; returns the clips that closed during it.
    [[nodiscard]] QList<AudioClip> feed(const QByteArray &pcm);
    // Closes the open clip (end of session); empty when it was too short.
    [[nodiscard]] QList<AudioClip> flush();
    // True while an utterance is open (for "listening…" feedback).
    [[nodiscard]] bool inSpeech() const { return m_inSpeech; }

private:
    void processFrame(const QByteArray &frame, QList<AudioClip> *out);
    void closeClip(QList<AudioClip> *out);

    QByteArray m_carry;     // < one frame
    QByteArray m_preRoll;   // last kPreRollMs of non-speech audio
    QByteArray m_clip;
    qint64 m_clipStartMs = 0;
    qint64 m_positionMs = 0;
    int m_speechMs = 0;
    int m_silenceMs = 0;
    bool m_inSpeech = false;
    double m_noiseFloor = 0.004; // RMS; adapts during non-speech
};

} // namespace QindaOffice::Speech
