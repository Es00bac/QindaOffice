// SPDX-License-Identifier: GPL-3.0-or-later
// LiveNotesSession's lifecycle (note_live_session.h): start() resolves the
// three keys (notes model, speech, gate) in parallel, then launch() wires
// capture → recognizer → engine → the page; stop() drains the recognizer,
// lets the engine run its final revision and stores the transcript.

#include "note_live_session.h"

#include "note_host.h"

#include "elevenlabs_realtime.h"
#include "elevenlabs_speakers.h"
#include "http_transcription.h"
#include "model_catalog.h"
#include "pcm_capture.h"
#include "system_one_client.h"
#include "wav_recorder.h"
#include "whisper_local.h"

#include <QClipboard>
#include <QGuiApplication>

namespace QindaOffice::Apps::Notes {

using Speech::SpeechRecognizer;

void LiveNotesSession::setState(const QString &state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

void LiveNotesSession::setStatus(const QString &status, const QString &error)
{
    m_status = status;
    m_error = error;
    emit statusChanged();
}

int LiveNotesSession::updateCount() const
{
    return m_engineCore != nullptr ? m_engineCore->updateCount() : 0;
}

int LiveNotesSession::segmentCount() const
{
    return m_engineCore != nullptr ? int(m_engineCore->transcript().size()) : 0;
}

QString LiveNotesSession::recentTranscript() const
{
    if (m_engineCore == nullptr) {
        return {};
    }
    const QList<Ai::TranscriptLine> &lines = m_engineCore->transcript();
    return Ai::LiveNotesPrompt::formatLines(lines.mid(qMax<qsizetype>(0, lines.size() - 6)));
}

void LiveNotesSession::start()
{
    if (active()) {
        return;
    }
    const auto config = m_catalog != nullptr ? m_catalog->configFor(m_modelId) : std::nullopt;
    if (!config.has_value()) {
        setStatus({}, tr("Choose the model that writes the notes (Settings ▸ AI providers…)."));
        return;
    }
    m_target = m_host->beginLiveNotes(m_topic);
    if (!m_target.isValid()) {
        setStatus({}, tr("Could not create a page for the notes."));
        return;
    }
    setState(QStringLiteral("starting"));
    setStatus(tr("Starting…"));
    const int generation = ++m_launchGeneration;
    struct Keys {
        QString model, speech, gate;
        int pending = 3;
    };
    auto keys = std::make_shared<Keys>();
    QPointer<LiveNotesSession> self(this);
    const auto done = [self, keys, generation] {
        if (--keys->pending > 0 || self == nullptr || generation != self->m_launchGeneration
            || self->m_state != QLatin1String("starting")) {
            return;
        }
        self->launch(keys->model, keys->speech, keys->gate);
    };
    Ai::ApiKeyResolver::resolve(Ai::ModelCatalog::keySources(*config), this,
                                [keys, done](const QString &key, const QString &) {
                                    keys->model = key;
                                    done();
                                });
    Ai::ApiKeyResolver::resolve(speechKeySources(), this,
                                [keys, done](const QString &key, const QString &) {
                                    keys->speech = key;
                                    done();
                                });
    Ai::ApiKeyResolver::resolve(gateKeySources(), this,
                                [keys, done](const QString &key, const QString &) {
                                    keys->gate = key;
                                    done();
                                });
}

SpeechRecognizer *LiveNotesSession::createRecognizer(const QString &key)
{
    if (m_engine == QLatin1String("whisper")) {
        return new Speech::WhisperLocal(m_whisperModel, m_language, this);
    }
    if (m_engine == QLatin1String("http")) {
        return new Speech::HttpTranscription(m_httpBaseUrl, key, m_httpModel, m_language, this);
    }
    if (m_speakerLabels) {
        // D-060: batch Scribe with diarization, in overlapping windows.
        auto *speakers = new Speech::ElevenLabsSpeakers(key, m_language, this);
        const QString endpoint = qEnvironmentVariable("QQO_ELEVENLABS_BATCH_URL");
        if (!endpoint.isEmpty()) {
            speakers->setEndpoint(QUrl(endpoint));
        }
        return speakers;
    }
    auto *eleven = new Speech::ElevenLabsRealtime(key, m_language, this);
    const QString endpoint = qEnvironmentVariable("QQO_ELEVENLABS_REALTIME_URL");
    if (!endpoint.isEmpty()) {
        eleven->setEndpoint(QUrl(endpoint)); // tests: a local WebSocket server
    }
    return eleven;
}

void LiveNotesSession::launch(const QString &modelKey, const QString &speechKey,
                              const QString &gateKey)
{
    m_provider = m_catalog->createProvider(m_modelId, modelKey, this);
    if (m_provider == nullptr) {
        setStatus({}, tr("The notes model is not available."));
        teardown();
        return;
    }
    if (m_gate != QLatin1String("off")) {
        if (m_gate == QLatin1String("jev") && gateKey.isEmpty()) {
            setStatus(tr("No TypeSafe key: running without the Jev gate."));
        } else {
            m_gateClient = new Ai::SystemOneClient(m_gateBaseUrl, gateKey, m_gateModel, this);
        }
    }

    m_engineCore = new Ai::LiveNotesEngine(this);
    QStringList glossary;
    for (const QString &term : m_keyterms.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        glossary.append(term.trimmed());
    }
    Ai::LiveNotesEngine::Options options;
    options.topic = m_topic;
    options.glossary = glossary;
    options.detail = m_detail;
    options.briefing = m_briefing;
    options.context = buildContext(); // prep material, earlier sessions (M56)
    m_engineCore->setOptions(options);
    m_engineCore->setProvider(m_provider);
    m_engineCore->setGate(m_gateClient);
    const LiveTarget target = m_target;
    NoteHost *host = m_host;
    m_engineCore->setNotesAccess([host, target] { return host->liveNotesText(target); },
                                 [host, target](const QString &notes) {
                                     return host->applyLiveNotes(target, notes);
                                 });
    connect(m_engineCore, &Ai::LiveNotesEngine::status, this,
            [this](const QString &text) { setStatus(text, m_error); });
    connect(m_engineCore, &Ai::LiveNotesEngine::failed, this,
            [this](const QString &text) { setStatus(m_status, text); });
    connect(m_engineCore, &Ai::LiveNotesEngine::busyChanged, this,
            &LiveNotesSession::updatingChanged);
    connect(m_engineCore, &Ai::LiveNotesEngine::notesApplied, this, [this] {
        m_error.clear();
        storeTranscript();
        emit statusChanged();
    });
    connect(m_engineCore, &Ai::LiveNotesEngine::finalized, this,
            &LiveNotesSession::onEngineFinalized);
    connect(m_engineCore, &Ai::LiveNotesEngine::usageChanged, this, &LiveNotesSession::usageChanged);
    m_speakerNames.clear();
    m_flagCount = 0;
    m_learnedTerms = 0;
    m_learnedFromTranscript = false;
    m_catchUp.clear();
    m_quietMs = 0;
    m_silencePaused = false;
    m_lastScreen = QImage();
    // Teach the recognizer and the notes the vocabulary of the prep
    // material and the earlier sessions (M56).
    learnTerms(options.context.reference + QStringLiteral("\n") + options.context.previousNotes);
    startRecording();
    m_silenceTimer.start();
    const auto config = m_catalog->configFor(m_modelId);
    const bool readsImages = config.has_value()
        && (config->kind == QLatin1String("openai") || config->kind == QLatin1String("anthropic"));
    if (m_captureScreen && m_source != QLatin1String("microphone")) {
        if (readsImages) {
            m_screenTimer.start();
            QTimer::singleShot(3000, this, &LiveNotesSession::captureScreenNow);
        } else {
            setStatus(m_status, tr("Screen capture needs a model that reads images (an HTTP provider "
                                   "such as Claude, GPT or Gemini); it is off for this session."));
        }
    }

    if (m_audioEnabled) {
        m_recognizer = createRecognizer(speechKey);
        m_recognizer->setKeyterms(glossary);
        connect(m_recognizer, &SpeechRecognizer::committed, this,
                [this](const Speech::SpeechSegment &segment) {
                    injectSegment(segment.text, segment.startMs, segment.speaker);
                    m_partial.clear();
                    emit transcriptChanged();
                });
        connect(m_recognizer, &SpeechRecognizer::partial, this, [this](const QString &text) {
            m_partial = text;
            emit transcriptChanged();
        });
        connect(m_recognizer, &SpeechRecognizer::ready, this, [this] {
            setStatus(tr("Listening with %1.").arg(m_recognizer->displayName()));
        });
        connect(m_recognizer, &SpeechRecognizer::failed, this,
                [this](const QString &message, bool fatal) {
                    setStatus(m_status, message);
                    if (fatal) {
                        stop();
                    }
                });
        connect(m_recognizer, &SpeechRecognizer::finished, this,
                &LiveNotesSession::onRecognizerFinished);
        m_capture = new Speech::PcmCapture(this);
        // The session sits between capture and recognizer: it keeps the
        // recording and holds back audio while silence-paused (M56).
        connect(m_capture, &Speech::PcmCapture::pcm, this, &LiveNotesSession::onPcm);
        connect(m_capture, &Speech::PcmCapture::level, this, &LiveNotesSession::observeLevel);
        connect(m_capture, &Speech::PcmCapture::failed, this, [this](const QString &message) {
            setStatus(m_status, message);
            stop();
        });
        m_recognizer->start();
        if (m_state != QLatin1String("starting")) {
            return; // the recognizer failed fatally inside start()
        }
        if (!m_capture->start(Speech::PcmCapture::sourceFromName(m_source))) {
            return; // failed() → stop()
        }
    }
    m_engineCore->start();
    setState(QStringLiteral("listening"));
    if (!m_audioEnabled) {
        setStatus(tr("Listening."));
    }
}

void LiveNotesSession::injectSegment(const QString &text, qint64 ms, const QString &speaker)
{
    if (m_engineCore == nullptr) {
        return;
    }
    m_engineCore->addSegment(text, ms, speaker);
    emit transcriptChanged();
}

void LiveNotesSession::pause()
{
    if (m_state != QLatin1String("listening")) {
        return;
    }
    if (m_capture != nullptr) {
        m_capture->setPaused(true);
    }
    setState(QStringLiteral("paused"));
    setStatus(tr("Paused. Nothing is being recorded."));
}

void LiveNotesSession::resume()
{
    if (m_state != QLatin1String("paused")) {
        return;
    }
    if (m_capture != nullptr) {
        m_capture->setPaused(false);
    }
    setState(QStringLiteral("listening"));
    setStatus(tr("Listening."));
}

void LiveNotesSession::stop()
{
    if (m_state == QLatin1String("idle") || m_state == QLatin1String("finishing")) {
        return;
    }
    const bool launched = m_engineCore != nullptr;
    setState(QStringLiteral("finishing"));
    if (!launched) {
        // Still resolving keys (or launch failed): nothing to drain.
        ++m_launchGeneration;
        onEngineFinalized();
        return;
    }
    setStatus(tr("Finishing: the last words and a final revision…"), m_error);
    if (m_capture != nullptr) {
        m_capture->stop();
    }
    if (m_recognizer != nullptr) {
        m_recognizer->finish(); // → finished → onRecognizerFinished
    } else {
        onRecognizerFinished();
    }
}

void LiveNotesSession::onRecognizerFinished()
{
    if (m_state != QLatin1String("finishing")) {
        // The recognizer ended by itself (a lost connection): wind down.
        stop();
        return;
    }
    m_partial.clear();
    emit transcriptChanged();
    if (m_engineCore != nullptr) {
        m_engineCore->finalize(); // → finalized → onEngineFinalized
    } else {
        onEngineFinalized();
    }
}

void LiveNotesSession::onEngineFinalized()
{
    storeTranscript();
    stopRecording();
    if (m_splitLongSessions && m_host->liveNotesText(m_target).size() >= Ai::LiveNotesPrompt::kTailModeChars) {
        m_host->splitLiveNotes(m_target); // one page per topic, an index page (M56)
    }
    const int updates = updateCount();
    m_host->endLiveNotes(m_target, updates > 0);
    teardown();
    setState(QStringLiteral("idle"));
    setStatus(updates > 0 ? tr("Finished — the notes were revised %n time(s).", "", updates)
                          : tr("Finished."),
              m_error);
}

void LiveNotesSession::cancel()
{
    if (!active()) {
        return;
    }
    ++m_launchGeneration;
    if (m_recognizer != nullptr) {
        m_recognizer->disconnect(this);
        m_recognizer->cancel();
    }
    if (m_capture != nullptr) {
        m_capture->stop();
    }
    if (m_engineCore != nullptr) {
        m_engineCore->disconnect(this);
        m_engineCore->cancel();
    }
    storeTranscript();
    stopRecording();
    m_host->endLiveNotes(m_target, updateCount() > 0);
    teardown();
    setState(QStringLiteral("idle"));
    setStatus(tr("Stopped."));
}

void LiveNotesSession::teardown()
{
    m_catchingUp = false;
    m_catchUp.clear();
    emit catchUpChanged();
    m_silenceTimer.stop();
    m_screenTimer.stop();
    m_silencePaused = false;
    for (QObject *object : std::initializer_list<QObject *>{
             m_capture.data(), m_recognizer.data(), m_engineCore.data(), m_provider.data(),
             m_gateClient.data()}) {
        if (object != nullptr) {
            object->disconnect(this);
            object->deleteLater();
        }
    }
    m_capture = nullptr;
    m_recognizer = nullptr;
    m_engineCore = nullptr;
    m_provider = nullptr;
    m_gateClient = nullptr;
    m_level = 0;
    emit levelChanged();
    emit updatingChanged();
}

void LiveNotesSession::storeTranscript()
{
    if (m_engineCore != nullptr && m_target.isValid() && !m_engineCore->transcript().isEmpty()) {
        m_host->storeLiveTranscript(m_target, m_engineCore->transcriptText());
    }
}

void LiveNotesSession::copyTranscript() const
{
    // The running engine has every line; the block only what was stored.
    const QString text = m_engineCore != nullptr ? m_engineCore->transcriptText()
                                                 : m_host->liveTranscript(m_target);
    if (!text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text);
    }
}

} // namespace QindaOffice::Apps::Notes
