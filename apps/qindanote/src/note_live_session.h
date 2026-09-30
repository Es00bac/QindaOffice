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
// note_live_settings.cpp (settings, persistence, keys), note_live_extras.cpp
// (M56, D-061: the flag, catch-up, speaker names, learned key terms, prep
// material and series memory, the recording, silence handling, screen
// capture and the usage meter), note_session_drafts.cpp (M59: source-bound
// follow-up previews, acceptance and reviewed action-item handoff).

#include "api_key_resolver.h"
#include "live_notes_engine.h"
#include "live_notes_extras.h"

#include <QImage>
#include <QTimer>
#include <QUrl>

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
class WavRecorder;
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
    // The Detail slider: 1 key points, 2 concise (default), 3 balanced,
    // 4 detailed, 5 comprehensive (LiveNotesPrompt::Detail). Live: a change
    // during a session rewrites the notes at the new level.
    Q_PROPERTY(int detail READ detail WRITE setDetail NOTIFY settingsChanged)
    // The pre-session setup (D-059): NotesBriefing::toMap() keys — scenario,
    // focus, skip, custom, and the section switches. Live: a change during
    // a session applies from the next update.
    Q_PROPERTY(QVariantMap setup READ setup WRITE setSetup NOTIFY settingsChanged)
    // [{id, name, hint}] for the setup dialog's scenario picker.
    Q_PROPERTY(QVariantList scenarios READ scenarios CONSTANT)
    // M57 (D-063): [{id, name, hint}] for the layout and style pickers.
    Q_PROPERTY(QVariantList layouts READ layouts CONSTANT)
    Q_PROPERTY(QVariantList styles READ styles CONSTANT)
    // Who said what (D-060): with the ElevenLabs engine, label speakers
    // through batch Scribe diarization (text then arrives every ~45 s).
    Q_PROPERTY(bool speakerLabels READ speakerLabels WRITE setSpeakerLabels NOTIFY settingsChanged)
    // Start opens the setup dialog first (the dialog's "show every time").
    Q_PROPERTY(bool askBeforeStart READ askBeforeStart WRITE setAskBeforeStart NOTIFY settingsChanged)
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
    // ---- M56 (D-061) ----
    // Session options kept with the setup: keep the recording, capture the
    // screen for the model, continue from earlier pages of the section,
    // split a long session into pages by topic when it stops.
    Q_PROPERTY(bool keepRecording READ keepRecording WRITE setKeepRecording NOTIFY settingsChanged)
    Q_PROPERTY(bool captureScreen READ captureScreen WRITE setCaptureScreen NOTIFY settingsChanged)
    Q_PROPERTY(bool continueSeries READ continueSeries WRITE setContinueSeries NOTIFY settingsChanged)
    Q_PROPERTY(bool splitLongSessions READ splitLongSessions WRITE setSplitLongSessions NOTIFY settingsChanged)
    // Silence: pause sending audio after 20 s of quiet (resumes by itself),
    // stop the session after autoStopMinutes of it (0 = never).
    Q_PROPERTY(bool autoPause READ autoPause WRITE setAutoPause NOTIFY settingsChanged)
    Q_PROPERTY(int autoStopMinutes READ autoStopMinutes WRITE setAutoStopMinutes NOTIFY settingsChanged)
    // Prep material for the next session (files: .txt .md .pdf .html).
    Q_PROPERTY(QStringList prepFiles READ prepFiles WRITE setPrepFiles NOTIFY settingsChanged)
    Q_PROPERTY(bool silencePaused READ silencePaused NOTIFY statusChanged)
    Q_PROPERTY(int flagCount READ flagCount NOTIFY transcriptChanged)
    // [{label, name}] for the speakers heard so far (speaker labels on).
    Q_PROPERTY(QVariantList speakers READ speakers NOTIFY transcriptChanged)
    Q_PROPERTY(QString catchUpText READ catchUpText NOTIFY catchUpChanged)
    Q_PROPERTY(bool catchingUp READ catchingUp NOTIFY catchUpChanged)
    // {audioMinutes, requests, inputTokens, outputTokens, gateCalls, screens, terms}
    Q_PROPERTY(QVariantMap usage READ usage NOTIFY usageChanged)
    // A study page or the action-items hand-off is being made.
    Q_PROPERTY(bool working READ working NOTIFY workingChanged)
    Q_PROPERTY(QVariantList followUpKinds READ followUpKinds CONSTANT)
    Q_PROPERTY(QString draftKind READ draftKind NOTIFY draftChanged)
    Q_PROPERTY(QString draftTitle READ draftTitle NOTIFY draftChanged)
    Q_PROPERTY(QString draftText READ draftText NOTIFY draftChanged)
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
    [[nodiscard]] QVariantMap setup() const { return m_briefing.toMap(); }
    void setSetup(const QVariantMap &map);
    [[nodiscard]] QVariantList scenarios() const;
    Q_INVOKABLE QVariantMap suggestedSetup(const QString &scenario) const;
    [[nodiscard]] QVariantList layouts() const;
    [[nodiscard]] QVariantList styles() const;
    [[nodiscard]] bool askBeforeStart() const { return m_askBeforeStart; }
    [[nodiscard]] bool speakerLabels() const { return m_speakerLabels; }
    void setSpeakerLabels(bool on);
    void setAskBeforeStart(bool on);
    [[nodiscard]] int detail() const { return m_detail; }
    void setDetail(int value);
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

    // M56 (note_live_extras.cpp)
    [[nodiscard]] bool keepRecording() const { return m_keepRecording; }
    void setKeepRecording(bool on);
    [[nodiscard]] bool captureScreen() const { return m_captureScreen; }
    void setCaptureScreen(bool on);
    [[nodiscard]] bool continueSeries() const { return m_continueSeries; }
    void setContinueSeries(bool on);
    [[nodiscard]] bool splitLongSessions() const { return m_splitLongSessions; }
    void setSplitLongSessions(bool on);
    [[nodiscard]] bool autoPause() const { return m_autoPause; }
    void setAutoPause(bool on);
    [[nodiscard]] int autoStopMinutes() const { return m_autoStopMinutes; }
    void setAutoStopMinutes(int minutes);
    [[nodiscard]] QStringList prepFiles() const { return m_prepFiles; }
    void setPrepFiles(const QStringList &files);
    Q_INVOKABLE void addPrepFile(const QUrl &file);
    Q_INVOKABLE void removePrepFile(int index);
    [[nodiscard]] bool silencePaused() const { return m_silencePaused; }
    [[nodiscard]] int flagCount() const { return m_flagCount; }
    [[nodiscard]] QVariantList speakers() const;
    [[nodiscard]] QString catchUpText() const { return m_catchUp; }
    [[nodiscard]] bool catchingUp() const { return m_catchingUp; }
    [[nodiscard]] QVariantMap usage() const;
    [[nodiscard]] bool working() const { return m_working; }
    // "This matters": the moment being heard is flagged for the notes.
    Q_INVOKABLE void flag();
    Q_INVOKABLE void setSpeakerName(const QString &label, const QString &name);
    // A few bullets on the last `minutes` of the session.
    Q_INVOKABLE void catchUp(int minutes = 5);
    // Prepare reviewable follow-ups from the current page after capture.
    // These legacy entry points now create previews; only acceptDraft
    // adds a page or creates the QindaMail handoff.
    Q_INVOKABLE void makeStudyPage();
    Q_INVOKABLE void sendActionItemsToMail();
    [[nodiscard]] QVariantList followUpKinds() const;
    [[nodiscard]] QString draftKind() const { return m_draftKind; }
    [[nodiscard]] QString draftTitle() const { return m_draftTitle; }
    [[nodiscard]] QString draftText() const { return m_draftText; }
    Q_INVOKABLE void makeDraft(const QString &kind);
    Q_INVOKABLE bool acceptDraft();
    Q_INVOKABLE void clearDraft();
    // Text of a prep file (tests; .txt .md .pdf .html).
    [[nodiscard]] static QString readPrepFile(const QString &path, QString *error = nullptr);
    // Test seams: feed the silence logic a level; run its 1 s tick now.
    void observeLevel(qreal level);
    void silenceTick();

    // Test seams: skip PipeWire and the recognizer (segments then come in
    // through injectSegment), and read the engine.
    void setAudioEnabled(bool enabled) { m_audioEnabled = enabled; }
    void injectSegment(const QString &text, qint64 ms, const QString &speaker = {});
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
    void catchUpChanged();
    void usageChanged();
    void workingChanged();
    void draftChanged();

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
    // note_live_extras.cpp
    [[nodiscard]] Ai::SessionContext buildContext() const;
    void learnTerms(const QString &material);
    void startRecording();
    void stopRecording();
    void onPcm(const QByteArray &pcm);
    void captureScreenNow();
    void onScreenCaptured(const QString &path);
    // Resolves the notes model's key and hands a provider to `use` (or
    // reports why not); the provider is parented to this session.
    void withModel(const std::function<void(Ai::ChatProvider *provider)> &use);
    void setWorking(bool on);

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
    int m_detail = Ai::LiveNotesPrompt::kDefaultDetail;
    Ai::NotesBriefing m_briefing;
    bool m_askBeforeStart = true;
    bool m_speakerLabels = false;
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
    // M56
    bool m_keepRecording = true;
    bool m_captureScreen = false;
    bool m_continueSeries = false;
    bool m_splitLongSessions = false;
    bool m_autoPause = true;
    int m_autoStopMinutes = 15;
    QStringList m_prepFiles;
    bool m_silencePaused = false;
    qint64 m_quietMs = 0;            // continuous quiet so far
    int m_flagCount = 0;
    QHash<QString, QString> m_speakerNames;
    QString m_catchUp;
    bool m_catchingUp = false;
    bool m_working = false;
    int m_draftGeneration = 0;
    QString m_draftKind, m_draftTitle, m_draftText, m_draftRevision;
    QList<Ai::LiveNotesExtras::ActionItem> m_draftTasks;
    QPointer<Ai::ChatRun> m_draftRun;
    bool m_learnedFromTranscript = false;
    int m_learnedTerms = 0;
    std::unique_ptr<Speech::WavRecorder> m_recorder;
    QString m_recordingPath;
    QTimer m_silenceTimer;
    QTimer m_screenTimer;
    QImage m_lastScreen;             // 32×18 grey thumbnail of the last one sent
};

} // namespace QindaOffice::Apps::Notes
