// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// ElevenLabs with speaker labels (D-060): who said what. Realtime Scribe
// does not tell speakers apart, batch Scribe does (diarize=true, up to 32
// speakers), so this recognizer sends the audio in overlapping windows —
// kStepMs of new audio plus kOverlapMs already sent — to
//   POST https://api.elevenlabs.io/v1/speech-to-text
//        model_id=scribe_v2, diarize=true, timestamps_granularity=word,
//        file_format=pcm_s16le_16
// and turns the returned words into one committed segment per speaker turn,
// labelled "Speaker 1", "Speaker 2", ….
//
// AGENT-NOTE (stitching): batch diarization numbers speakers per request,
// so "speaker_0" in one window need not be "speaker_0" in the next. The
// overlap decides: each local speaker is mapped to the global speaker it
// shares the most overlapped speaking time with (from the previous
// window's labelled words); a local speaker with no overlap is a new
// global one. Only words after the previous window's end are emitted, so
// nothing appears twice.
//
// AGENT-NOTE (voice samples): overlap alone only recognises someone who
// happened to speak inside it — a speaker who was quiet for those seconds
// came back as a new "Speaker 3". So every window is PREFIXED with a short
// sample of each known speaker's voice (≤ kSampleMs, up to kMaxSamples
// speakers, kGapMs of silence between): whichever local speaker the
// diarizer hears in sample k IS global speaker k. Words inside the samples
// are never emitted. The samples cost a few seconds of audio per request.
//
// Text therefore arrives every ~kStepMs, not live; there are no partials.

#include "speech_recognizer.h"

#include <QHash>
#include <QStringList>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QUrl>

class QNetworkReply;

namespace QindaOffice::Speech {

class ElevenLabsSpeakers final : public SpeechRecognizer {
    Q_OBJECT
public:
    static constexpr int kStepMs = 45000;
    static constexpr int kOverlapMs = 15000;
    static constexpr int kSampleMs = 4000;
    static constexpr int kGapMs = 400;
    static constexpr int kMaxSamples = 8;

    ElevenLabsSpeakers(QString apiKey, QString language, QObject *parent = nullptr);

    // Tests point at a local server and use short windows.
    void setEndpoint(const QUrl &endpoint) { m_endpoint = endpoint; }
    void setWindow(int stepMs, int overlapMs)
    {
        m_stepMs = stepMs;
        m_overlapMs = overlapMs;
    }

    [[nodiscard]] QString displayName() const override { return tr("ElevenLabs with speakers"); }
    [[nodiscard]] bool offMachine() const override { return true; }
    void start() override;
    void feed(const QByteArray &pcm) override;
    void finish() override;
    void cancel() override;

    struct Word {
        qint64 startMs = 0;
        qint64 endMs = 0;
        QString text;     // includes the spacing before it
        QString speaker;  // global label
    };
    // votes[local][global] = shared speaking time; each local speaker, in
    // order of appearance, takes its best untaken global label, or a new
    // "Speaker N" (exposed for tests).
    [[nodiscard]] static QHash<QString, QString> assignSpeakers(
        const QHash<QString, QHash<QString, qint64>> &votes, const QStringList &locals, int *nextSpeaker);

private:
    void maybeSend();
    void send(qint64 fromMs, qint64 toMs);
    void onReply(QNetworkReply *reply, qint64 fromMs, qint64 toMs);
    void updateSamples(const QList<Word> &words);

    QString m_apiKey;
    QString m_language;
    QUrl m_endpoint;
    int m_stepMs = kStepMs;
    int m_overlapMs = kOverlapMs;
    QNetworkAccessManager m_nam;
    QByteArray m_audio;          // PCM from m_audioStartMs on
    qint64 m_audioStartMs = 0;
    qint64 m_sentUntilMs = 0;    // audio time the last window reached
    QList<Word> m_previous;      // the last window's words, globally labelled
    struct Sample {
        QString speaker;
        QByteArray pcm;
    };
    QList<Sample> m_samples;     // one voice sample per known speaker
    struct SentSample {
        QString speaker;
        qint64 startMs = 0;      // request-relative
        qint64 endMs = 0;
    };
    QList<SentSample> m_sentSamples; // the running request's sample layout
    qint64 m_prefixMs = 0;           // its samples' total length
    int m_nextSpeaker = 1;
    QPointer<QNetworkReply> m_reply;
    bool m_finishing = false;
    bool m_cancelled = false;
};

} // namespace QindaOffice::Speech
