// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's list numbers on the page (M73, D-082): what each list paragraph's
// marker says, in which font and where it stands, worked out by Word's
// rules from the paragraphs' list formats (docx_lists.h), and drawn on
// screen, paper and PDF beside the paragraph borders and tab leaders.
//
// Counting, as Word counts: paragraphs in document order; one counter set
// per definition (kListAbstractKey), or per instance when the instance
// overrides a start; an item at a level resets the deeper levels its
// w:lvlRestart allows; "%1.%2." shows each level's current number in that
// level's format (legal numbering: in arabic numerals). The editing path
// and the reader's output are numbered by this one function, so a list
// renumbers itself as items are added, moved, promoted or removed.
//
// AGENT-NOTE: Qt draws its own list marker unless the paragraph's block
// format says QTextFormat::ListStyle = ListStyleUndefined (probed in Qt
// 6.11's QTextDocumentLayoutPrivate::drawListItem); every Word list
// paragraph says so (docx_list_edit.h, the reader), and this draws Word's.

#include <QHash>
#include <QRectF>
#include <QString>
#include <QTextCharFormat>

class QPainter;
class QTextBlock;
class QTextDocument;

namespace QindaOffice::Docx {

struct ListMarker {
    QString text;           // as shown: "1.", "2.1.", "•" for Symbol's bullet
    QTextCharFormat format; // the paragraph mark's format with the level's w:rPr over it
    qreal hanging = 0;      // px: the number stands at left − hanging
    QString justification = QStringLiteral("left"); // the number's alignment there
    QString suffix = QStringLiteral("tab");         // what follows it: tab, space, nothing
    int level = 0;
    int numId = 0;
    int value = 0; // the level's number at this paragraph (Set Numbering Value)

    [[nodiscard]] bool isValid() const { return numId > 0; }
};

// Every Word list paragraph's marker, by block number; cached on the
// document until its next change. `fresh` recomputes (inside an edit
// block, where the change signals are not out yet).
[[nodiscard]] QHash<int, ListMarker> listMarkers(const QTextDocument &document, bool fresh = false);
// One paragraph's marker; invalid when it is not in a Word list.
[[nodiscard]] ListMarker listMarker(const QTextBlock &block);

// Paints the markers of the Word list paragraphs that meet `clip`
// (document coordinates; the painter set up as for the layout's draw).
void drawListMarkers(QPainter *painter, const QTextDocument &document, const QRectF &clip);

// A symbol font's code (Symbol, Wingdings, Webdings: U+F0xx or its low
// byte) as the Unicode character that looks like it; other text as it is.
[[nodiscard]] QString displayGlyph(const QString &text, const QString &font);

// Where a list paragraph's first line starts, from its left indent, so the
// text follows the marker as Word sets it: after a tab — at the left indent
// when the number ends before it, else at the next tab stop — or a space,
// or nothing.
[[nodiscard]] qreal markerTextOffset(const QTextBlock &block, const ListMarker &marker, qreal defaultTab);
// Sets every Word list paragraph's first-line offset (its textIndent,
// display only: the writer reads a list paragraph's indents from
// kListHanging and the left margin) to markerTextOffset. Not an edit block.
int layoutListText(QTextDocument *document, qreal defaultTab);

} // namespace QindaOffice::Docx
