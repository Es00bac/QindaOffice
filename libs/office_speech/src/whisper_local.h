// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Local Whisper (D-057): nothing leaves the machine. Audio is cut into
// utterances (speech_segmenter.h) and each one is transcribed by the
// qindaoffice-whisper helper — faster-whisper on the CPU with the model
// Portage installs (app-accessibility/faster-whisper-models, default
// distil-small.en), the same engine and model Gabbee's local fallback uses.
//
// AGENT-NOTE: a helper process rather than a linked library because
// faster-whisper/CTranslate2 is what is packaged on these machines and it
// is Python; whisper.cpp is not installed anywhere. The frame protocol is
// documented in tools/qindaoffice-whisper (AGENT-CONTRACT there).
//
// Helper lookup: $QQO_WHISPER_HELPER, else the installed libexec path,
// else the source tree's tools/ (a build-tree run).

#include "speech_recognizer.h"
#include "speech_segmenter.h"

#include <QHash>

class QProcess;

namespace QindaOffice::Speech {

class WhisperLocal final : public SpeechRecognizer {
    Q_OBJECT
public:
    // A clip backlog beyond this means the CPU cannot keep up; the user is
    // told once (and the clips are still transcribed, just late).
    static constexpr int kBacklogWarning = 6;

    // `model` is a model name (looked up in the XDG data dirs) or a path;
    // `language` an ISO code or empty.
    WhisperLocal(QString model, QString language, QObject *parent = nullptr);
    ~WhisperLocal() override;

    [[nodiscard]] QString displayName() const override { return tr("Local Whisper"); }
    [[nodiscard]] bool offMachine() const override { return false; }
    void start() override;
    void feed(const QByteArray &pcm) override;
    void finish() override;
    void cancel() override;

    [[nodiscard]] static QString helperPath();

private:
    void send(const AudioClip &clip);
    void onLine(const QByteArray &line);
    void maybeFinished();

    QString m_model;
    QString m_language;
    QProcess *m_process = nullptr;
    SpeechSegmenter m_segmenter;
    bool m_ready = false;
    bool m_finishing = false;
    bool m_warnedBacklog = false;
    int m_nextId = 1;
    QList<AudioClip> m_waiting;        // before the model is loaded
    QHash<int, AudioClip> m_inFlight;  // sent, not yet answered
    QByteArray m_stdout;
    QString m_lastText;                // prompt for the next clip
};

} // namespace QindaOffice::Speech
