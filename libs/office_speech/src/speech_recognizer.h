// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The recognizer contract (D-057): every speech-to-text backend takes the
// same audio and answers through the same signals, so the live-notes
// session never knows which one it is driving.
//
// AGENT-CONTRACT: audio is ALWAYS 16 kHz mono signed 16-bit little-endian
// PCM (kSampleRate / kBytesPerMs below). PcmCapture produces exactly that;
// ElevenLabs is asked for pcm_16000; the whisper helper and the HTTP
// backend wrap it as the same format. Timestamps are milliseconds of AUDIO
// fed since start(), not wall-clock time, so a stalled network does not
// skew them.

#include <QObject>
#include <QString>
#include <QStringList>

namespace QindaOffice::Speech {

inline constexpr int kSampleRate = 16000;
inline constexpr int kBytesPerMs = kSampleRate * 2 / 1000; // 32

struct SpeechSegment {
    QString text;
    qint64 startMs = 0; // audio time of the segment's first speech
    qint64 endMs = 0;   // audio time when it was committed
    // "Speaker 1", "Speaker 2", … when the backend tells speakers apart
    // (ElevenLabsSpeakers); empty otherwise.
    QString speaker{};
};

class SpeechRecognizer : public QObject {
    Q_OBJECT
public:
    explicit SpeechRecognizer(QObject *parent = nullptr) : QObject(parent) {}
    ~SpeechRecognizer() override = default;

    // A short name for the status line ("ElevenLabs realtime", …).
    [[nodiscard]] virtual QString displayName() const = 0;
    // True when audio leaves this machine (the panel's badge).
    [[nodiscard]] virtual bool offMachine() const = 0;

    // Connects / spawns. Audio fed before ready() is buffered by the
    // backend (bounded); nothing is dropped silently.
    virtual void start() = 0;
    virtual void feed(const QByteArray &pcm) = 0;
    // Flushes what was heard, emits the last committed() segments and then
    // finished(). Safe to call once; cancel() afterwards stops waiting.
    virtual void finish() = 0;
    // Stops immediately; no further signals except finished().
    virtual void cancel() = 0;

    // Names and jargon to recognise ("Jev", a lecturer's name). ElevenLabs
    // takes them as keyterms; the clip backends put them in the prompt.
    void setKeyterms(const QStringList &terms) { m_keyterms = terms; }
    [[nodiscard]] const QStringList &keyterms() const { return m_keyterms; }

    // Milliseconds of audio fed so far.
    [[nodiscard]] qint64 audioMs() const { return m_fedBytes / kBytesPerMs; }
    // Milliseconds of audio sent to a paid service (the usage meter, M56):
    // what was streamed or uploaded, overlaps and voice samples included;
    // 0 for a local engine.
    [[nodiscard]] virtual qint64 billedAudioMs() const { return offMachine() ? audioMs() : 0; }

signals:
    void ready();
    // The words heard since the last commit (replaces the previous partial).
    void partial(const QString &text);
    void committed(const QindaOffice::Speech::SpeechSegment &segment);
    // fatal: the backend cannot continue (bad key, missing model); the
    // session stops. Non-fatal errors are reported and recovered from.
    void failed(const QString &message, bool fatal);
    void finished();

protected:
    void countFed(qsizetype bytes) { m_fedBytes += bytes; }
    // The clip backends' prompt: the glossary, then the tail of what was
    // just said (continuity across clips).
    [[nodiscard]] QString clipPrompt(const QString &previous) const;
    // AGENT-GUARD: finished() exactly once, whatever path gets there first
    // (finish() timeout, socket close, cancel()).
    void emitFinishedOnce();

private:
    QStringList m_keyterms;
    qint64 m_fedBytes = 0;
    bool m_finishedEmitted = false;
};

} // namespace QindaOffice::Speech
