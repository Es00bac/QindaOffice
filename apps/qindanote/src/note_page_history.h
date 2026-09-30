// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaNote's ONE page history (plan section 3.6): a QUndoStack owned by
// NotePageItem. Ink strokes and eraser gestures (InkCommand), block edits
// (BlocksCommand: old/new blocksJson, consecutive typing on one block
// merges) and background changes (BackgroundCommand) share it: if it
// changed the page, Ctrl+Z undoes it.
//
// AGENT-GUARD: undo and redo are two halves of ONE history. QUndoStack
// clears the redo branch on every push — a redo stack that survives a
// fresh edit replays strokes onto a page that has moved on. Do not
// reintroduce a second container. The item applies an edit BEFORE pushing,
// so every command's first redo() — fired by QUndoStack::push() itself —
// is a no-op.

#include "stroke_model.h"

#include <QByteArray>
#include <QPointer>
#include <QUndoCommand>

namespace QindaOffice::Apps::Notes {

class NotePageItem;

class InkCommand final : public QUndoCommand {
public:
    InkCommand(const QString &text, Ink::StrokeLayer before, Ink::StrokeLayer after,
               NotePageItem *item);
    void undo() override;
    void redo() override;

private:
    Ink::StrokeLayer m_before;
    Ink::StrokeLayer m_after;
    QPointer<NotePageItem> m_item;
    bool m_alreadyApplied = true;
};

class BlocksCommand final : public QUndoCommand {
public:
    enum { Id = 1 };
    // `mergeKey` non-negative marks typing on that block: consecutive
    // typing commands on the same block merge into one step.
    BlocksCommand(const QString &text, QByteArray before, QByteArray after, NotePageItem *item,
                  int mergeKey = -1);
    void undo() override;
    void redo() override;
    [[nodiscard]] int id() const override { return Id; }
    bool mergeWith(const QUndoCommand *other) override;

private:
    QByteArray m_before;
    QByteArray m_after;
    QPointer<NotePageItem> m_item;
    int m_mergeKey;
    bool m_alreadyApplied = true;
};

class BackgroundCommand final : public QUndoCommand {
public:
    BackgroundCommand(QString before, QString after, NotePageItem *item);
    void undo() override;
    void redo() override;

private:
    QString m_before;
    QString m_after;
    QPointer<NotePageItem> m_item;
    bool m_alreadyApplied = true;
};

// AGENT-CONTRACT: NoteTools batches title and blocks together; the same
// command must undo both, preserving transcript/audio fields and ink.
class PageContentCommand final : public QUndoCommand {
public:
    PageContentCommand(QString text, QByteArray before, QByteArray after,
                       QString oldTitle, QString newTitle, NotePageItem *item);
    void undo() override;
    void redo() override;
private:
    QByteArray m_before, m_after;
    QString m_oldTitle, m_newTitle;
    QPointer<NotePageItem> m_item;
    bool m_alreadyApplied = true;
};

} // namespace QindaOffice::Apps::Notes
