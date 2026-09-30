// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Section edits for live notes (M57, D-063): instead of returning the whole
// notes every turn, the model returns a short EDIT SCRIPT that names the
// sections to change, and applyEdits() carries it out. This is what lets
// organised notes (by subject, by objective, …) grow in place — a point
// made at 40:00 lands in the section opened at 05:00 — and it keeps each
// turn's output to what changed, which matters most on a slow local model
// (docs/live-notes.md, "Offline notes").
//
// The script (one op per "=== " line, its body on the lines below):
//
//   === TITLE                      body: the new "# Title" line
//   === SUMMARY                    body: the new summary bullets
//   === REPLACE ## Heading         body: the whole revised section
//   === APPEND ## Heading          body: lines added to the section's own
//                                  text (before its subsections)
//   === NEW UNDER ## Heading       body: a new subsection (### …) at the
//                                  end of that section
//   === NEW AFTER ## Heading       body: a new section after that one
//   === NEW                        body: a new section at the end of the
//                                  topic sections (before Timeline,
//                                  Definitions, Open questions, …)
//   === MOVE ## A TO UNDER ## B    (or TO AFTER ## B) re-files a section
//   === DELETE ## Heading
//   === TIMELINE                   body: lines appended to ## Timeline
//   === ACTIONS                    body: the new action-item checklist
//
// Headings are matched loosely (level, case, **, trailing [mm:ss]
// ignored; then a prefix match), so a model that shortens a heading still
// hits it. An op whose target is missing degrades to the op that keeps the
// content (REPLACE/APPEND → a new section), never to a silent loss.
//
// AGENT-CONTRACT (prompt ↔ parser): protocolText() is what the model is
// told; parse() is what it is held to. Change both together, with
// tst_live_notes_edits. Section names Summary / Action items / Timeline
// are shared with live_notes_prompt.cpp.
//
// Pure functions over strings; no Qt objects, no I/O.

#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Ai::LiveNotesEdits {

struct Op {
    enum Kind { Title, Summary, Replace, Append, NewUnder, NewAfter, NewAtEnd, Move, Delete,
                Timeline, Actions };
    Kind kind = NewAtEnd;
    QString target;        // the heading an op names (without #s)
    QString destination;   // MOVE: the heading it goes under/after
    bool moveUnder = false;
    QString body;
};

// The ops in `reply`, in order. `*script` is false when the reply holds no
// op line at all (a model that answered with whole notes instead).
[[nodiscard]] QList<Op> parse(const QString &reply, bool *script = nullptr);

struct Result {
    QString notes;
    int applied = 0;
    QStringList remarks; // what degraded ("APPEND target missing: …"), for the log
};
[[nodiscard]] Result apply(const QString &notes, const QList<Op> &ops);

// The protocol as the model reads it (English; part of the user message).
[[nodiscard]] QString protocolText();

// "## Photosynthesis [12:40]" → "photosynthesis" (what matching compares).
[[nodiscard]] QString normalizeHeading(const QString &heading);

} // namespace QindaOffice::Ai::LiveNotesEdits
