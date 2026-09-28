// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Clip-at-a-time transcription over HTTP (D-057): POST
// {base}/audio/transcriptions, multipart `file` (a WAV of the utterance),
// `model`, optional `language` and `prompt`, `response_format=json` —
// the OpenAI shape, which Groq (whisper-large-v3-turbo), OpenAI
// (gpt-4o-transcribe, whisper-1) and self-hosted OpenAI-compatible
// whisper servers (speaches, faster-whisper-server) all accept. Audio is
// cut by the same SpeechSegmenter local Whisper uses.
//
// Clips are sent one at a time, in order, so the transcript never arrives
// shuffled; a failed clip is reported (non-fatal) and skipped, except an
// auth failure, which is fatal.

#include "speech_recognizer.h"
#include "speech_segmenter.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QUrl>

class QNetworkReply;

namespace QindaOffice::Speech {

class HttpTranscription final : public SpeechRecognizer {
    Q_OBJECT
public:
    HttpTranscription(QString baseUrl, QString apiKey, QString model, QString language,
                      QObject *parent = nullptr);

    [[nodiscard]] QString displayName() const override;
    [[nodiscard]] bool offMachine() const override;
    void start() override;
    void feed(const QByteArray &pcm) override;
    void finish() override;
    void cancel() override;

    // A RIFF/WAVE header + the PCM (16 kHz mono s16le).
    [[nodiscard]] static QByteArray wavFile(const QByteArray &pcm);

private:
    void pump();

    QString m_baseUrl;
    QString m_apiKey;
    QString m_model;
    QString m_language;
    QNetworkAccessManager m_nam;
    SpeechSegmenter m_segmenter;
    QList<AudioClip> m_queue;
    QPointer<QNetworkReply> m_reply;
    QString m_lastText;
    bool m_finishing = false;
    bool m_cancelled = false;
};

} // namespace QindaOffice::Speech
