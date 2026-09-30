// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// How live notes are ORGANISED and how they are TYPESET (M57, D-063).
//
// A layout is the notes' shape: by subject (the default), chronological,
// by learning objective, for retention (Cornell cues + review questions),
// meeting minutes, an argument map, questions and answers, a story guide
// (characters, plot, lore — for shows and books), or the user's own list
// of sections. Organised layouts rely on section edits
// (live_notes_edits.h): a point made late is filed in the section it
// belongs to, which is what keeps them from collapsing into a log. An
// optional ## Timeline keeps the chronological layer underneath.
//
// The style guide is the typesetting vocabulary the page renders
// (apps/qindanote/src/note_markdown*: coloured highlights by meaning,
// callout boxes, underline, super/subscript, stand-out text, tag badges,
// tables) at one of three intensities.
//
// AGENT-CONTRACT: layout ids and style ids are persisted in live-notes.json
// "setup" (NotesBriefing::toMap) and listed by NoteLiveSetupDialog.qml —
// add new ones, never rename. The markup named in styleGuide() is exactly
// what note_markdown renders; change both together.

#include <QList>
#include <QString>

namespace QindaOffice::Ai::LiveNotesLayouts {

struct Layout {
    QString id;
    QString name;
    QString hint; // one line for the setup dialog
};

inline constexpr auto kDefaultLayout = "topics";
inline constexpr auto kDefaultStyle = "styled";

// topics, timeline, objectives, study, meeting, argument, qa, story, custom.
[[nodiscard]] QList<Layout> layouts();
// plain, styled, rich.
[[nodiscard]] QList<Layout> styles();

// The organisation rules for the system prompt. `sections` is the user's
// own list (one per line, "Name — what goes there"): the whole structure
// for "custom", extra sections to keep for any other layout.
[[nodiscard]] QString layoutBrief(const QString &id, const QString &sections, bool timestamps);
// The ## Timeline rule (the chronological layer under organised notes).
[[nodiscard]] QString timelineBrief();
// The typesetting vocabulary at `style`.
[[nodiscard]] QString styleGuide(const QString &style);

} // namespace QindaOffice::Ai::LiveNotesLayouts
