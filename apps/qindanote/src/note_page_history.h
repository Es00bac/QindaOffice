// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaNote's ONE page history (M22, plan section 3.6): a QUndoStack owned
// by NotePageView replaces the two hand-rolled snapshot vectors. M22 lands
// only InkCommand (pen strokes and eraser gestures — exactly what the old
// stacks covered); BlocksCommand/BackgroundCommand arrive with M23.
//
// AGENT-GUARD: undo and redo are two halves of ONE history. QUndoStack
// clears the redo branch on every push — a redo stack that survives a
// fresh edit replays strokes onto a page that has moved on (that was the
// old code's bug shape). Do not reintroduce a second container.

#include "stroke_model.h"

#include <QPointer>
#include <QUndoCommand>

class QUndoStack;

namespace QindaOffice::Apps::Notes {

class NotePageView;

// One ink edit as old/new StrokeLayer snapshots (what the old stacks
// held). The view applies the edit BEFORE pushing, so the command's first
// redo() — fired by QUndoStack::push() itself — must be a no-op, or the
// edit would be applied twice and double-report pageEdited.
class InkCommand final : public QUndoCommand {
public:
    InkCommand(const QString &text, Ink::StrokeLayer before, Ink::StrokeLayer after,
               NotePageView *view);

    void undo() override;
    void redo() override;

private:
    Ink::StrokeLayer m_before;
    Ink::StrokeLayer m_after;
    // Commands die with the stack (the view clears it on setPage), and the
    // stack dies with the view — but a QPointer makes the dangling case a
    // no-op instead of a crash if that contract is ever broken.
    QPointer<NotePageView> m_view;
    bool m_alreadyApplied = true;
};

} // namespace QindaOffice::Apps::Notes
