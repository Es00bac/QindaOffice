// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pre-session briefing for live notes (D-059): what the user tells the
// note-taker before a session starts — the kind of session (a scenario
// with its own emphasis), what to focus on, what to leave out, which
// optional sections to keep, and free-form custom instructions. It becomes
// the SESSION BRIEFING part of the system prompt (live_notes_prompt.h),
// which outranks the general rules where they conflict.
//
// AGENT-CONTRACT: the map keys (toMap/fromMap) are persisted in
// live-notes.json "setup" and read by NoteLiveSetupDialog.qml; scenario ids
// are stable — add new ones, never rename.

#include <QList>
#include <QString>
#include <QVariantMap>

namespace QindaOffice::Ai {

struct NotesBriefing {
    QString scenario = QStringLiteral("general");
    QString focus;       // "what I care about"
    QString skip;        // "what I don't care about"
    QString custom;      // free-form instructions
    bool timestamps = true;     // [mm:ss] on section headings
    bool actionItems = true;    // the ## Action items checklist
    bool quotes = true;         // > "exact wording"
    bool openQuestions = false; // ## Open questions
    bool definitions = false;   // ## Definitions glossary

    [[nodiscard]] QVariantMap toMap() const;
    [[nodiscard]] static NotesBriefing fromMap(const QVariantMap &map);
    bool operator==(const NotesBriefing &) const = default;
};

namespace LiveNotesBriefing {

struct Scenario {
    QString id;
    QString name;
    QString hint;   // one line for the setup dialog
};

// general, lecture, meeting, interview, tutorial, talk.
[[nodiscard]] QList<Scenario> scenarios();
// The note-taking emphasis a scenario adds to the prompt ("" for general).
[[nodiscard]] QString scenarioBrief(const QString &id);
// The SESSION BRIEFING section: scenario, focus, skip, custom.
[[nodiscard]] QString render(const NotesBriefing &briefing);

} // namespace LiveNotesBriefing

} // namespace QindaOffice::Ai
