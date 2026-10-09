// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word's paragraph formatting on QTextDocument blocks (M70, D-079): a
// paragraph's own spacing and the gaps Word adds between paragraphs, tab
// stops with leaders, paragraph borders, the outline level. Each setter
// keeps the display (Qt's margins, tab positions, the kParagraphBorders
// spec) in step with the Word value the writer reads (docx_properties.h),
// so QindaWrite's Paragraph, Tabs and Borders dialogs change both at once.
//
// Threading: GUI thread, like the documents. The setters are not edit
// blocks of their own: the caller groups one command into one undo step.

#include "docx_borders.h"

#include <QList>
#include <QRectF>
#include <QString>
#include <QTextBlockFormat>
#include <QTextOption>

class QPainter;
class QTextBlock;
class QTextDocument;

namespace QindaOffice::Ooxml {
class XmlElement;
}

namespace QindaOffice::Docx {

// ---- tab stops ------------------------------------------------------------------

struct TabStop {
    int position = 0;                         // twips from the text column's edge (w:pos)
    QString alignment = QStringLiteral("left"); // left, center, right, decimal, bar
    QString leader = QStringLiteral("none");    // none, dot, hyphen, underscore, heavy, middleDot
    bool operator==(const TabStop &) const = default;
};

// "720/left/none;9350/right/dot", sorted by position.
[[nodiscard]] QString tabStopsSpec(QList<TabStop> stops);
[[nodiscard]] QList<TabStop> parseTabStops(QStringView spec);
// The stops an effective w:tabs sets (clears already applied by the style
// merge are skipped; start/end read as left/right).
[[nodiscard]] QList<TabStop> readTabStops(const Ooxml::XmlElement *tabs);
// A paragraph's stops: kTabStops, or — for a paragraph Word did not write —
// Qt's own positions turned into Word's.
[[nodiscard]] QList<TabStop> tabStops(const QTextBlockFormat &format);
// Qt's tab positions for `stops` on a paragraph indented `leftPx` (Qt
// measures from the left indent; bar tabs and stops left of it draw none).
[[nodiscard]] QList<QTextOption::Tab> qtTabs(const QList<TabStop> &stops, qreal leftPx);
// Sets kTabStops and Qt's positions from the format's current left indent.
void setTabStops(QTextBlockFormat &format, const QList<TabStop> &stops);

// Paints the leaders of the document's tab stops that have one (dots,
// hyphens, an underline) across each tab's gap, for the blocks that meet
// `clip`. Qt lays tabs out but draws no leader; the page view, print and
// PDF call this beside drawParagraphBorders (docx_text.h).
void drawTabLeaders(QPainter *painter, const QTextDocument &document, const QRectF &clip);

// ---- spacing --------------------------------------------------------------------

struct OwnSpacing {
    int before = 0; // twips (w:spacing/@w:before)
    int after = 0;  // twips
    bool autoBefore = false; // w:beforeAutospacing: Word's HTML 14 pt
    bool autoAfter = false;
    bool contextual = false; // w:contextualSpacing
    bool operator==(const OwnSpacing &) const = default;
};
// kParagraphSpacing + kContextualSpacing, or the margins of a paragraph
// Word did not write.
[[nodiscard]] OwnSpacing ownSpacing(const QTextBlockFormat &format);
// Sets `block`'s own spacing. A paragraph read from Word keeps the reader's
// folding: the gap to the paragraph before and to the one after change by
// what Word's sum (after + before, contextual spacing between paragraphs of
// one style) changes by; one Write made takes the values as its margins.
void setParagraphSpacing(const QTextBlock &block, const OwnSpacing &spacing);

// ---- borders and outline ----------------------------------------------------------

[[nodiscard]] WordBorders paragraphBorders(const QTextBlockFormat &format);
// Sets kParagraphBorderSpec and the kParagraphBorders display spec.
void setParagraphBorders(QTextBlockFormat &format, const WordBorders &borders);

// The paragraph's outline level: 0–8, or 9 for body text (kOutlineLevel,
// else its heading level).
[[nodiscard]] int outlineLevel(const QTextBlockFormat &format);
// Sets kOutlineLevel and Qt's heading level (levels 0–5 are headings 1–6).
void setOutlineLevel(QTextBlockFormat &format, int level);

} // namespace QindaOffice::Docx
