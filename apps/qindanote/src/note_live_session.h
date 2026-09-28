// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Live notes for one QindaNote window (D-057, docs/live-notes.md): audio
// from the microphone, the desktop's output or both (office_speech
// PcmCapture) → speech to text (ElevenLabs realtime, local Whisper or an
// OpenAI-compatible transcription endpoint) → LiveNotesEngine (office_ai)
// with the window's model catalog (Ornith on a llama-server, Claude, GPT,
// Kimi, GLM, DeepSeek, …) and an optional System One gate (TypeSafe Jev or
// a local Laya) → one markdown block on a notes page that keeps being
// revised. The QML panel (NoteLivePanel.qml) binds to this object.
//
// Privacy contract (the assistant's, extended): nothing is captured or
// sent before start(); the panel shows which stages leave the machine
// (audioOffMachine / modelOffMachine / gateOffMachine) before and during a
// session.
//
// AGENT-NOTE: the session holds the TARGET (section id, page id, block
// uid), never pointers (note_host.h's AGENT-GUARD); NoteHost resolves it
// on every read and write (note_host_live.cpp).
//
// Files: note_live_session.cpp (lifecycle and pipeline),
// note_live_settings.cpp (settings, persistence, keys).

#include "api_key_resolver.h"
#include "live_notes_engine.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace QindaOffice::Core {
class JsonSettings;
}
namespace QindaOffice::Ai {
class ModelCatalog;
class SystemOneClient;
}
namespace QindaOffice::Speech {
class PcmCapture;
class SpeechRecognizer;
}

namespace QindaOffice::Apps::Notes {

class NoteHost;

struct LiveTarget {
    QString sectionId;
    QString pageId;
    QString blockUid;
    [[nodiscard]] bool isValid() const { return !pageId.isEmpty() && !blockUid.isEmpty(); }
};

class LiveNotesSession : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by NoteHost.")
    // idle | starting | listening | paused | finishing
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    // microphone | desktop | both
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY settingsChanged)
    // elevenlabs | whisper | http
    Q_PROPERTY(QString engine READ engine WRITE setEngine NOTIFY settingsChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY settingsChanged)
    Q_PROPERTY(QString whisperModel READ whisperModel WRITE setWhisperModel NOTIFY settingsChanged)
    Q_PROPERTY(QString httpBaseUrl READ httpBaseUrl WRITE setHttpBaseUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString httpModel READ httpModel WRITE setHttpModel NOTIFY settingsChanged)
    Q_PROPERTY(QString keyterms READ keyterms WRITE setKeyterms NOTIFY settingsChanged)
    // [{id, name, offMachine}] from the model catalog
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QString modelId READ modelId WRITE setModelId NOTIFY settingsChanged)
    // off | jev | laya
    Q_PROPERTY(QString gate READ gate WRITE setGate NOTIFY settingsChanged)
    Q_PROPERTY(QString gateBaseUrl READ gateBaseUrl WRITE setGateBaseUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString gateModel READ gateModel WRITE setGateModel NOTIFY settingsChanged)
    // Per session: what the user says this is ("Biology 101, lecture 4").
    Q_PROPERTY(QString topic READ topic WRITE setTopic NOTIFY settingsChanged)
    Q_PROPERTY(bool audioOffMachine READ audioOffMachine NOTIFY settingsChanged)
    Q_PROPERTY(bool modelOffMachine READ modelOffMachine NOTIFY settingsChanged)
    Q_PROPERTY(bool gateOffMachine READ gateOffMachine NOTIFY settingsChanged)
    // Where each key was found ("" = none): see checkKeys().
    Q_PROPERTY(QVariantMap keyOrigins READ keyOrigins NOTIFY keyOriginsChanged)
    // Live feedback.
    Q_PROPERTY(qreal level READ level NOTIFY levelChanged)
    Q_PROPERTY(QString partialText READ partialText NOTIFY transcriptChanged)
    Q_PROPERTY(QString recentTranscript READ recentTranscript NOTIFY transcriptChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY statusChanged)
    Q_PROPERTY(bool updating READ updating NOTIFY updatingChanged)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY statusChanged)
    Q_PROPERTY(int segmentCount READ segmentCount NOTIFY transcriptChanged)
public:
    LiveNotesSession(NoteHost *host, Ai::ModelCatalog *catalog, QString settingsPath,
                     QObject *parent = nullptr);
    ~LiveNotesSession() override;

    [[nodiscard]] QString state() const { return m_state; }
    [[nodiscard]] bool active() const { return m_state != QLatin1String("idle"); }
    [[nodiscard]] QString source() const { return m_source; }
    void setSource(const QString &value);
    [[nodiscard]] QString engine() const { return m_engine; }
    void setEngine(const QString &value);
    [[nodiscard]] QString language() const { return m_language; }
    void setLanguage(const QString &value);
    [[nodiscard]] QString whisperModel() const { return m_whisperModel; }
    void setWhisperModel(const QString &value);
    [[nodiscard]] QString httpBaseUrl() const { return m_httpBaseUrl; }
    void setHttpBaseUrl(const QString &value);
    [[nodiscard]] QString httpModel() const { return m_httpModel; }
    void setHttpModel(const QString &value);
    [[nodiscard]] QString keyterms() const { return m_keyterms; }
    void setKeyterms(const QString &value);
    [[nodiscard]] QVariantList models() const;
    [[nodiscard]] QString modelId() const { return m_modelId; }
    void setModelId(const QString &value);
    [[nodiscard]] QString gate() const { return m_gate; }
    void setGate(const QString &value);
    [[nodiscard]] QString gateBaseUrl() const { return m_gateBaseUrl; }
    void setGateBaseUrl(const QString &value);
    [[nodiscard]] QString gateModel() const { return m_gateModel; }
    void setGateModel(const QString &value);
    [[nodiscard]] QString topic() const { return m_topic; }
    void setTopic(const QString &value);
    [[nodiscard]] bool audioOffMachine() const;
    [[nodiscard]] bool modelOffMachine() const;
    [[nodiscard]] bool gateOffMachine() const;
    [[nodiscard]] QVariantMap keyOrigins() const { return m_keyOrigins; }
    [[nodiscard]] qreal level() const { return m_level; }
    [[nodiscard]] QString partialText() const { return m_partial; }
    [[nodiscard]] QString recentTranscript() const;
    [[nodiscard]] QString statusText() const { return m_status; }
    [[nodiscard]] QString errorText() const { return m_error; }
    [[nodiscard]] bool updating() const { return m_engineCore != nullptr && m_engineCore->busy(); }
    [[nodiscard]] int updateCount() const;
    [[nodiscard]] int segmentCount() const;

    Q_INVOKABLE void start();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    // Stops listening, lets the last words and a final revision land.
    Q_INVOKABLE void stop();
    // Stops at once; notes written so far stay.
    Q_INVOKABLE void cancel();
    // Keys typed into the panel go to the keyring (never kept here).
    // `which`: "elevenlabs" | "transcription" | "gate".
    Q_INVOKABLE void storeKey(const QString &which, const QString &key);
    Q_INVOKABLE void checkKeys();
    Q_INVOKABLE void copyTranscript() const;

    // Test seams: skip PipeWire and the recognizer (segments then come in
    // through injectSegment), and read the engine.
    void setAudioEnabled(bool enabled) { m_audioEnabled = enabled; }
    void injectSegment(const QString &text, qint64 ms);
    [[nodiscard]] Ai::LiveNotesEngine *notesEngine() const { return m_engineCore; }
    [[nodiscard]] const LiveTarget &target() const { return m_target; }

signals:
    void stateChanged();
    void settingsChanged();
    void modelsChanged();
    void keyOriginsChanged();
    void levelChanged();
    void transcriptChanged();
    void statusChanged();
    void updatingChanged();

private:
    // note_live_session.cpp
    void setState(const QString &state);
    void setStatus(const QString &status, const QString &error = {});
    void launch(const QString &modelKey, const QString &speechKey, const QString &gateKey);
    Speech::SpeechRecognizer *createRecognizer(const QString &key);
    void onRecognizerFinished();
    void onEngineFinalized();
    void teardown();
    void storeTranscript();
    // note_live_settings.cpp
    void loadSettings();
    void saveSettings();
    [[nodiscard]] Ai::KeySources speechKeySources() const;
    [[nodiscard]] Ai::KeySources gateKeySources() const;

    NoteHost *m_host;
    Ai::ModelCatalog *m_catalog;
    std::unique_ptr<Core::JsonSettings> m_settings;
    QString m_state = QStringLiteral("idle");
    QString m_source = QStringLiteral("microphone");
    QString m_engine = QStringLiteral("elevenlabs");
    QString m_language;
    QString m_whisperModel = QStringLiteral("distil-small.en");
    QString m_httpBaseUrl;
    QString m_httpModel;
    QString m_keyterms;
    QString m_modelId;
    QString m_gate = QStringLiteral("off");
    QString m_gateBaseUrl;
    QString m_gateModel;
    QString m_topic;
    QVariantMap m_keyOrigins;
    qreal m_level = 0;
    QString m_partial;
    QString m_status;
    QString m_error;
    bool m_audioEnabled = true;
    int m_launchGeneration = 0;
    LiveTarget m_target;
    QPointer<Speech::PcmCapture> m_capture;
    QPointer<Speech::SpeechRecognizer> m_recognizer;
    QPointer<Ai::LiveNotesEngine> m_engineCore;
    QPointer<Ai::ChatProvider> m_provider;
    QPointer<Ai::SystemOneClient> m_gateClient;
};

} // namespace QindaOffice::Apps::Notes
