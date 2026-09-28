// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// LiveNotesEngine (D-057, docs/live-notes.md): turns a growing transcript
// into notes that keep getting better. It owns WHEN to ask the model and
// WHAT to do with the answer; the words live in live_notes_prompt.h, the
// audio in office_speech, and the note page in the app.
//
// Cadence: an update runs when enough new substantive words have arrived
// (Options::minWords) and minIntervalMs has passed since the last one, or
// when anything new has waited maxIntervalMs. Never two at once — a slow
// model simply means fewer, bigger updates. Every fullRevisionEvery-th
// update (and the final one) rewrites from the whole transcript while
// the notes are still short enough for that (FULL vs TAIL, see
// live_notes_prompt.h).
//
// The gate (optional, SystemOneClient: TypeSafe Jev or a local Laya)
// judges each new stretch of transcript in ~0.3 s: substantive or filler,
// same topic or a new one, how important. Filler does not count towards
// an update (so ads, music and small talk cost no big-model calls), a
// topic change triggers one promptly, and important lines reach the
// model marked ★. A failing gate is switched off for a minute (fail open).
//
// AGENT-GUARD (user edits win): the notes are READ back from the page
// through the reader callback before each request and again when the
// reply arrives. If the user edited them in between, the reply is thrown
// away — their edit is kept and the next request carries it. The writer
// callback may also refuse (the block is open in the editor); the reply
// is then retried for a while and dropped if the notes changed.

#include "chat_types.h"
#include "live_notes_prompt.h"
#include "system_one_client.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>

#include <functional>

namespace QindaOffice::Ai {

class LiveNotesEngine : public QObject {
    Q_OBJECT
public:
    struct Options {
        int minWords = 45;
        int minIntervalMs = 20000;
        int maxIntervalMs = 75000;
        int topicChangeMinWords = 20;
        int fullRevisionEvery = 6;
        int fullTranscriptBudget = 90000; // chars; longer transcripts use TAIL
        int maxOutputTokens = 16000;
        int gateBatchWords = 25;
        QString topic;
        QStringList glossary; // correct spellings of names and terms
        int detail = LiveNotesPrompt::kDefaultDetail; // 1 (key points) .. 5
    };

    using Reader = std::function<QString()>;
    using Writer = std::function<bool(const QString &notes)>;

    explicit LiveNotesEngine(QObject *parent = nullptr);
    ~LiveNotesEngine() override;

    void setOptions(const Options &options) { m_options = options; }
    [[nodiscard]] const Options &options() const { return m_options; }
    // Not owned; may be replaced between updates. nullptr = no model.
    void setProvider(ChatProvider *provider) { m_provider = provider; }
    // Not owned; nullptr = no gate.
    void setGate(SystemOneClient *gate) { m_gate = gate; }
    void setNotesAccess(Reader reader, Writer writer);
    // The Detail slider, live: a change rewrites the notes at the new level
    // on the next tick (a "restyle"), even if nothing new was said.
    void setDetail(int detail);

    // Starts the cadence clock. Segments before start() are kept.
    void start();
    void addSegment(const QString &text, qint64 ms);
    // Runs a last (full, when it fits) update, then emits finalized().
    void finalize();
    void cancel();

    [[nodiscard]] bool busy() const { return m_run != nullptr; }
    [[nodiscard]] int updateCount() const { return m_updates; }
    [[nodiscard]] const QList<TranscriptLine> &transcript() const { return m_lines; }
    [[nodiscard]] QString transcriptText() const;
    [[nodiscard]] int pendingWords() const;
    // Test seam: evaluate the cadence now instead of on the 1 s tick.
    void tick();

signals:
    void busyChanged(bool busy);
    // A human-readable line for the panel ("Updating notes…", "Notes
    // updated at 12:40 — 3 lines", "Skipped a filler stretch").
    void status(const QString &text);
    void failed(const QString &message);
    void notesApplied(int updateCount);
    void finalized();

private:
    void maybeGate();
    void onGateAnswers(qsizetype from, qsizetype to,
                       const QHash<QString, SystemOneAnswer> &answers, const QString &error);
    void runUpdate(bool full, bool restyle = false);
    void onReply(const QString &text);
    void tryApplyPending();
    [[nodiscard]] bool fullFits() const;
    [[nodiscard]] qint64 elapsedMs() const;

    Options m_options;
    QPointer<ChatProvider> m_provider;
    QPointer<SystemOneClient> m_gate;
    Reader m_read;
    Writer m_write;
    QList<TranscriptLine> m_lines;
    qsizetype m_covered = 0;     // lines already in the notes
    qsizetype m_requestEnd = 0;  // lines the running request covers
    qsizetype m_gated = 0;       // lines the gate has judged (or skipped)
    bool m_gateBusy = false;
    qint64 m_gateOffUntilMs = -1;
    QPointer<ChatRun> m_run;
    bool m_runFull = false;
    bool m_runTail = false;
    bool m_runRestyle = false;
    bool m_restylePending = false;
    QString m_sentNotes;
    QString m_pending;           // an answer waiting for the writer
    int m_pendingRetries = 0;
    int m_updates = 0;
    bool m_finalizing = false;
    int m_finalAttempts = 0;     // bounds a final pass that keeps failing
    bool m_started = false;
    bool m_topicChangePending = false;
    QElapsedTimer m_clock;       // since start()
    qint64 m_lastUpdateMs = 0;
    QTimer m_tick;
};

} // namespace QindaOffice::Ai
