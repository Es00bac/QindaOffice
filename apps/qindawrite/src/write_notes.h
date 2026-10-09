// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's endnotes and the Footnote and Endnote dialog in QindaWrite (M77,
// D-086), beside the footnotes WriteReviewActions inserts:
//  - Insert Endnote (Alt+Ctrl+D): a mark at the caret and its note, edited
//    like a footnote (double-click the mark; an empty text removes it);
//  - Footnote and Endnote: each kind's number format (1 2 3, a b c, A B C,
//    i ii iii, I II III, * † ‡ §), Start at, Numbering (continuous, or
//    restarting each section — each page for footnotes), the endnotes'
//    place (end of document or of section);
//  - Convert: all footnotes to endnotes, all endnotes to footnotes, or swap.
// Every change is one undo step: the marks change in the text and the note
// texts swap with them (WriteDocument::setNoteTexts).

#include "action_model.h"

#include <QObject>
#include <QVariantMap>

namespace QindaOffice::Quick {
struct MenuNode;
}

namespace QindaOffice::Apps::Write {

class PageTextControl;
class WriteDocument;

class NotesController final : public QObject {
    Q_OBJECT
public:
    NotesController(PageTextControl *control, WriteDocument *document, Quick::ActionModel *model, QObject *parent);

    // Insert Endnote, Footnote and Endnote…, Convert Notes…
    [[nodiscard]] QList<Quick::MenuNode> menuItems() const;

    // A double-click on an endnote mark edits its note; true when one was there.
    bool editEndnoteAt(int position);
    // The note dialog's answer for either kind ("footnote" / "endnote"): an
    // empty `id` inserts a mark at the caret; an empty text on an existing
    // note removes note and mark. One undo step.
    Q_INVOKABLE void commitNote(const QString &kind, const QString &id, const QString &text);

    // {footnote: {format, start, restart, position}, endnote: {…},
    //  formats: [{format, label}]} for the Footnote and Endnote dialog.
    Q_INVOKABLE QVariantMap noteOptions() const;
    // {kind, format, start, restart, position}: one undo step (renumbering).
    Q_INVOKABLE bool applyNoteOptions(const QVariantMap &answer);
    // "toEndnotes", "toFootnotes", "swap": one undo step; false when there
    // was nothing to convert.
    Q_INVOKABLE bool convertNotes(const QString &mode);
    // Marks to their numbers after an edit (both kinds, their formats).
    void renumber();

signals:
    void noteRequested(const QString &kind, const QString &id, const QString &text);
    void optionsRequested();
    void convertRequested();
    void message(const QString &text);

private:
    PageTextControl *m_control;
    WriteDocument *m_document;
    Quick::ActionEntry *m_insertEndnote = nullptr;
    Quick::ActionEntry *m_options = nullptr;
    Quick::ActionEntry *m_convert = nullptr;
};

} // namespace QindaOffice::Apps::Write
