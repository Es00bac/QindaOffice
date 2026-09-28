// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Live audio from PipeWire as 16 kHz mono s16le (speech_recognizer.h).
//
// AGENT-NOTE: capture runs `pw-record … --raw -` as a child process, the
// same way Gabbee (the desktop's org.qindaqt.Voice1 provider) records.
// QtMultimedia's QAudioSource cannot open a sink MONITOR, and desktop
// audio — a YouTube lecture, a video call — is exactly that. pw-record
// resamples and downmixes for us, so every recognizer gets one format.
//
//   Microphone     pw-record --target <source or default>
//   DesktopAudio   pw-record -P '{ stream.capture.sink = true }'
//                            --target <default sink>   (its monitor)
//   Both           both, mixed by PcmMixer
//
// QQO_PW_RECORD names a replacement program (the tests use a script that
// writes a known PCM file); QQO_DEFAULT_SINK skips the pactl lookup.

#include "pcm_mixer.h"

#include <QObject>
#include <QPointer>

class QProcess;

namespace QindaOffice::Speech {

class PcmCapture : public QObject {
    Q_OBJECT
public:
    enum class Source { Microphone, DesktopAudio, Both };
    Q_ENUM(Source)

    explicit PcmCapture(QObject *parent = nullptr);
    ~PcmCapture() override;

    // Stable names for settings files: "microphone" | "desktop" | "both".
    [[nodiscard]] static QString sourceName(Source source);
    [[nodiscard]] static Source sourceFromName(const QString &name);

    // Starts the capture process(es). `microphone` optionally names a
    // PipeWire source node; empty = the default input. Returns false (and
    // emits failed()) when a process cannot be started.
    bool start(Source source, const QString &microphone = {});
    void stop();
    [[nodiscard]] bool running() const;

    // Paused capture keeps the processes (no re-negotiation, no gap in the
    // PipeWire graph) but drops the audio.
    void setPaused(bool paused) { m_paused = paused; }
    [[nodiscard]] bool paused() const { return m_paused; }

signals:
    // ~100 ms chunks of 16 kHz mono s16le.
    void pcm(const QByteArray &chunk);
    // 0..1 peak-ish level of the latest chunk (for the meter), also while
    // paused so the user sees the source is alive.
    void level(qreal value);
    void failed(const QString &message);

private:
    QProcess *spawn(int stream, const QStringList &arguments);
    void deliver(int stream, const QByteArray &bytes);
    void emitChunk(const QByteArray &chunk);
    [[nodiscard]] static QString defaultSink();

    QPointer<QProcess> m_processes[2];
    bool m_mixing = false;
    bool m_paused = false;
    bool m_stopping = false;
    PcmMixer m_mixer;
    QByteArray m_pending; // re-chunks to ~100 ms
};

} // namespace QindaOffice::Speech
