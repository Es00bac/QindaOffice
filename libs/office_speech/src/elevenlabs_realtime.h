// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ElevenLabs Scribe realtime (D-057): one WebSocket per session,
//   wss://api.elevenlabs.io/v1/speech-to-text/realtime
//     ?model_id=scribe_v2_realtime&audio_format=pcm_16000
//     &commit_strategy=vad&vad_silence_threshold_secs=…
// with the key in the xi-api-key header. Audio goes up as
// {"message_type":"input_audio_chunk","audio_base_64":…,"sample_rate":16000}
// and comes back as partial_transcript / committed_transcript events.
//
// AGENT-NOTE: Gabbee (the desktop's dictation provider) speaks the same
// protocol with commit_strategy=manual because it is push-to-talk. Live
// notes are continuous, so the SERVER's VAD commits; a manual commit is
// also sent after kForceCommitMs of uncommitted audio so a speaker who
// never pauses still produces committed text.
//
// Resilience: audio is buffered (≤ kMaxBufferMs) while connecting; an
// unexpected close or session_time_limit_exceeded reconnects (≤ 5 times in
// a row, backing off) and replays the buffer, passing the tail of the
// transcript as previous_text so the model keeps context.

#include "speech_recognizer.h"

#include <QTimer>
#include <QUrl>

class QWebSocket;

namespace QindaOffice::Speech {

class ElevenLabsRealtime final : public SpeechRecognizer {
    Q_OBJECT
public:
    static constexpr int kMaxBufferMs = 30000;
    static constexpr int kForceCommitMs = 25000;

    // `endpoint` defaults to the production URL; tests point it at a local
    // QWebSocketServer. `language` is an ISO 639-1/3 code or empty (auto).
    ElevenLabsRealtime(QString apiKey, QString language, QObject *parent = nullptr);

    void setEndpoint(const QUrl &endpoint) { m_endpoint = endpoint; }

    [[nodiscard]] QString displayName() const override { return tr("ElevenLabs realtime"); }
    [[nodiscard]] bool offMachine() const override { return true; }
    void start() override;
    void feed(const QByteArray &pcm) override;
    void finish() override;
    void cancel() override;

    // The request URL (tests assert the query).
    [[nodiscard]] QUrl requestUrl() const;

private:
    void connectSocket();
    void onText(const QString &message);
    void onClosed();
    void sendChunk(const QByteArray &pcm, bool commit);
    void flushBuffer();

    QString m_apiKey;
    QString m_language;
    QUrl m_endpoint;
    QWebSocket *m_socket = nullptr;
    bool m_open = false;       // session_started received
    bool m_closeHandled = false; // per socket: onClosed ran
    bool m_finishing = false;
    bool m_cancelled = false;
    bool m_firstChunk = true;  // previous_text rides on it
    int m_reconnects = 0;
    QByteArray m_buffer;       // audio waiting for the session
    qint64 m_uncommittedMs = 0;
    qint64 m_segmentStartMs = -1;
    QString m_history;         // committed text tail, for previous_text
    QTimer m_finishTimer;
};

} // namespace QindaOffice::Speech
