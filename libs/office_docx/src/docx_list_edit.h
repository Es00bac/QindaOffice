// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's list commands on a QTextDocument (M73, D-082): what QindaWrite's
// Bullets, Numbering and Multilevel List libraries, Define New…, Change
// List Level (Tab / Shift+Tab), Restart at 1, Continue Numbering, Set
// Numbering Value and Adjust List Indents do — Word's model, so the
// writer saves them as Word does (docx_lists.h).
//
// Every function edits the document but none is an edit block of its own:
// the caller groups one command into one undo step. Each paragraph that
// joins a Word list gets the level's indents (kListHanging, the left
// margin) and Qt's own marker switched off (QTextFormat::ListStyle); the
// caller lays the first lines out after the marker (layoutListText).

#include "docx_lists.h"

#include <QList>
#include <QString>
#include <QTextBlock>

class QTextDocument;

namespace QindaOffice::Docx {

// The Word list a paragraph is an item of; invalid when it is not.
struct WordListItem {
    int numId = 0;
    int level = 0;
    QString definition;
    QString abstractKey;
    QList<int> starts;
    [[nodiscard]] bool isValid() const { return numId > 0; }
};
[[nodiscard]] WordListItem wordListItem(const QTextBlock &block);

// A w:numId, and a definition key ("new:<n>"), no list of `document` uses.
[[nodiscard]] int nextNumId(const QTextDocument &document);
[[nodiscard]] QString newAbstractKey(const QTextDocument &document);

// `blocks` become the items of one new list with `definition`, each at
// the level it had in a list (`level` for paragraphs that were in none, or
// for every one when `keepLevels` is false). Returns the new list's numId.
int makeList(const QList<QTextBlock> &blocks, const QString &definition, int level = 0, bool keepLevels = true);
// Every item of list `numId` takes `definition` — a new instance of a new
// definition, as Word makes when a list's format changes. Returns the new
// numId (0 when no paragraph is in that list).
int redefineList(QTextDocument *document, int numId, const QString &definition);
// `block` moves to `level` of its list, with that level's indents.
void setListLevel(const QTextBlock &block, int level);
// Takes `block` out of its list. `keepIndent` leaves its text where it was
// (Word's Backspace at an item's start); otherwise the paragraph returns to
// no indent (the Bullets and Numbering toggles, Enter on an empty item).
void removeFromList(const QTextBlock &block, bool keepIndent);
// Restart at `value`: `block` and the rest of its list become a new
// instance whose start at `block`'s level is `value` (w:startOverride).
// Returns the new numId.
int restartNumbering(const QTextBlock &block, int value);
// Continue Numbering: the list `block` is in joins the closest list before
// it with the same definition; false when there is none.
bool continueNumbering(const QTextBlock &block);
// The list Continue Numbering would join (0: none).
[[nodiscard]] int continuableNumId(const QTextBlock &block);
// The paragraphs of list `numId`, in document order.
[[nodiscard]] QList<QTextBlock> listItems(const QTextDocument &document, int numId);

} // namespace QindaOffice::Docx
