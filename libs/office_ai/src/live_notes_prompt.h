// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The words and the text surgery of live notes (D-057, docs/live-notes.md).
// Pure functions — no network, no timers — so the tests pin the prompt
// shape, the reply clean-up and the long-notes merge exactly.
//
// Two update shapes:
//   FULL  the model gets the whole notes + transcript and returns the
//         whole revised notes. Used while the notes are short
//         (< kTailModeChars) — it lets the model reorganise freely.
//   TAIL  the notes are long, so rewriting them all every minute would
//         cost minutes of output. The model sees the earlier sections as
//         an outline only and returns `## Summary`, the revised LAST
//         sections plus any new ones, and `## Action items`; mergeTail()
//         splices those back around the untouched middle.
//
// AGENT-CONTRACT (prompt ↔ merge): the section names "Summary" and
// "Action items" are how mergeTail() finds its pieces. Change them in
// both places or neither.

#include "live_notes_briefing.h"

#include <QString>
#include <QStringList>

namespace QindaOffice::Ai {

struct TranscriptLine {
    qint64 ms = 0;
    QString text;
    bool filler = false;   // the gate judged it non-substantive
    bool key = false;      // the gate judged it important (★)
    bool newTopic = false; // the gate saw a topic change here (⤷)
    QString speaker{};     // "Speaker 2" when the recognizer labels speakers
};

// Markdown notes split at level-2 headings.
struct NotesParts {
    QString preamble;       // title and anything before the first "## "
    QString summary;        // the "## Summary" section, heading included
    QStringList sections;   // every other "## " section, in order
    QString actions;        // the "## Action items" section
};

namespace LiveNotesPrompt {

inline constexpr int kTailModeChars = 9000;
inline constexpr int kTailSections = 2;
inline constexpr int kContextChars = 1500;

// How much the notes keep (the panel's Detail slider). AGENT-CONTRACT: the
// numbers are persisted (live-notes.json "detail") and shown by
// NoteLivePanel.qml; keep 1..5 and their order.
enum Detail { KeyPoints = 1, Concise = 2, Balanced = 3, Detailed = 4, Comprehensive = 5 };
inline constexpr int kDefaultDetail = Concise;
[[nodiscard]] int clampDetail(int detail);
// The detail level's writing brief, spliced into the system prompt.
[[nodiscard]] QString detailBrief(int detail);

// The rules, the detail level's brief and the user's session briefing.
[[nodiscard]] QString systemPrompt(int detail = kDefaultDetail, const NotesBriefing &briefing = {});
[[nodiscard]] QString formatTime(qint64 ms);
// "[mm:ss] ★ text" lines (fillers marked "(filler)").
[[nodiscard]] QString formatLines(const QList<TranscriptLine> &lines);

struct Request {
    QString notes;                          // current notes (may carry user edits)
    QList<TranscriptLine> context;          // already-covered lines (tail used)
    QList<TranscriptLine> fresh;            // lines not yet in the notes
    QString topic;                          // optional user hint
    QStringList glossary;                   // correct spellings (mishearings)
    qint64 elapsedMs = 0;
    bool fullRevision = false;              // all lines in `fresh`, rewrite
    // The detail level changed: rewrite the existing notes to the new
    // level even if nothing new was said.
    bool restyle = false;
};
[[nodiscard]] bool useTailMode(const QString &notes);
[[nodiscard]] QString userMessage(const Request &request);

// Strips <think> blocks, a wrapping ``` fence and a chatty preamble.
[[nodiscard]] QString sanitizeReply(const QString &reply);
[[nodiscard]] NotesParts split(const QString &notes);
[[nodiscard]] QString join(const NotesParts &parts);
// Splices a TAIL reply into `notes`. Returns empty when the reply is
// unusable (no sections at all).
[[nodiscard]] QString mergeTail(const QString &notes, const QString &reply);
// Guard against a model that "revises" by forgetting: a reply much
// shorter than what it replaces is refused. `why` explains it.
[[nodiscard]] bool plausibleRevision(const QString &before, const QString &after, QString *why);

} // namespace LiveNotesPrompt

} // namespace QindaOffice::Ai
