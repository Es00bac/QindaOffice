// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Floating pictures (M77, D-086): Word's wp:anchor in QindaWrite's text.
//
// A picture is still one image character in its paragraph (its anchor);
// kPictureFloat on its format holds Word's layout — wrap, horizontal and
// vertical position and what they are relative to, wrap distances — and
// kPictureFloatSnapshot what the reader found, so an untouched floating
// picture keeps its own drawing byte for byte.
//
// Shown with what Qt's text layout has:
//  - In Line with Text: an ordinary inline image;
//  - Square, Tight, Through, Top and Bottom: a floating frame of Qt's own
//    (QTextCursor::insertImage with FloatLeft / FloatRight) that the text
//    flows around, left or right of the column; the frame's box holds the
//    picture at its offset from the column's edge and below the line it is
//    anchored at, and the wrap distances. Top and Bottom is a box as wide
//    as the column, so no text sits beside it. Tight and Through flow
//    around the box, not the picture's outline;
//  - Behind Text and In Front of Text: no room in the line at all; the
//    page painter draws the picture where its position puts it on the
//    page, before or after the text (drawFloatingPictures).
// Qt cannot float a picture in the middle of a column with text on both
// sides, above the line it is anchored at, or at a fixed place on the
// page with the text flowing around it: those are shown at their anchor
// line, nearest the side Word puts them (D-086 lists the gaps).
//
// AGENT-GUARD: a document showing a floating picture has QindaWrite's
// picture handler (installPictureHandler) instead of Qt's image handler:
// it sizes and draws every image, inline ones exactly as Qt does
// (QTextImageHandler, Qt 6.11: whole-pixel sizes from the format, the
// picture's own size otherwise, ImageMaxWidth). Copies of a document
// (headers drawn per page, merged documents) need it installed too.

#include "ooxml_xml.h"

#include <QRectF>
#include <QString>
#include <QTextFrameFormat>
#include <QVariantMap>

class QPainter;
class QTextCharFormat;
class QTextCursor;
class QTextDocument;

namespace QindaOffice::Docx {

struct PictureFloat {
    // inline, square, tight, through, topAndBottom, behind, inFront
    QString wrap = QStringLiteral("inline");
    QString wrapText = QStringLiteral("bothSides"); // bothSides, left, right, largest
    // margin, page, column, character, leftMargin, rightMargin, insideMargin, outsideMargin
    QString hRelative = QStringLiteral("column");
    QString hAlign; // left, center, right, inside, outside; empty: hOffset
    qreal hOffset = 0; // px
    // margin, page, paragraph, line, topMargin, bottomMargin, insideMargin, outsideMargin
    QString vRelative = QStringLiteral("paragraph");
    QString vAlign; // top, center, bottom, inside, outside; empty: vOffset
    qreal vOffset = 0;
    qreal distTop = 0;
    qreal distBottom = 0;
    qreal distLeft = 12;  // Word's default 0.125 in
    qreal distRight = 12;
    bool allowOverlap = true;
    bool lockAnchor = false;
    bool layoutInCell = true;
    QString relativeHeight = QStringLiteral("251659264"); // z-order, kept

    [[nodiscard]] bool isFloating() const { return wrap != u"inline"; }
    [[nodiscard]] bool overText() const { return wrap == u"behind" || wrap == u"inFront"; }
    // Word's Move with Text (the vertical position follows the paragraph)
    // as against Fix Position on Page.
    [[nodiscard]] bool movesWithText() const { return vRelative == u"paragraph" || vRelative == u"line"; }
    [[nodiscard]] QVariantMap toMap() const;
    [[nodiscard]] static PictureFloat fromMap(const QVariantMap &map);
    bool operator==(const PictureFloat &) const = default;
};

// The format of the character at `position` as the text holds it — with
// its ObjectIndex, the floating frame a wrapped picture is drawn in.
// AGENT-GUARD: QTextCursor::charFormat() drops ObjectIndex (so typing after
// a picture does not continue it); read and write a floating picture's
// format through this, or a setCharFormat() unfloats it.
[[nodiscard]] QTextCharFormat characterFormatAt(const QTextDocument &document, int position);

// The layout of a picture's format (inline when it has none).
[[nodiscard]] PictureFloat pictureFloat(const QTextCharFormat &format);
// Word's layout of a wp:anchor (or an inline picture's for wp:inline).
[[nodiscard]] PictureFloat readPictureFloat(const Ooxml::XmlElement &frame);
// Rewrites a w:drawing for `layout` at the picture's extents (EMU text):
// wp:inline ↔ wp:anchor as needed, an anchor's position, wrap, distances
// and flags as Word writes them; its docPr, graphic and extensions kept.
void writePictureFloat(Ooxml::XmlElement &drawing, const PictureFloat &layout, const QString &cx, const QString &cy);

// How Qt shows the layout: a FloatLeft / FloatRight frame, or InFlow
// (inline, and the pictures drawn over or under the text).
[[nodiscard]] QTextFrameFormat::Position floatPosition(const PictureFloat &layout);

// Re-inserts the picture at `position` with `layout` (its format and Qt's
// display of it) at the same place: the caller's edit block makes it one
// undo step. Returns the picture's position afterwards, or −1.
int setPictureFloat(QTextDocument *document, int position, const PictureFloat &layout);

// Installs QindaWrite's picture handler on `document`'s layout (once).
void installPictureHandler(QTextDocument *document);
// Whether `document` holds a floating picture (it then needs the handler).
[[nodiscard]] bool hasFloatingPictures(const QTextDocument &document);

// Where the picture at `position` is drawn, in document coordinates: an
// inline one where its line puts it, a floating one in its frame, one
// behind or in front of the text where its position puts it on its page.
// Empty for no picture there.
[[nodiscard]] QRectF floatingPictureRect(const QTextDocument &document, int position);

// The layout that puts the picture at `position` `delta` px further (drag
// to place): new offsets in what its position is relative to, any
// alignment dropped, as Word does when a picture is dragged.
[[nodiscard]] PictureFloat movedPictureFloat(const QTextDocument &document, int position, const QPointF &delta);
// The picture's layout with `wrap`: from In Line with Text, Word keeps the
// picture where it is — relative to the column and the paragraph.
[[nodiscard]] PictureFloat wrappedPictureFloat(const QTextDocument &document, int position, const QString &wrap);

// Draws the pictures behind (`behind`) or in front of the text that meet
// `clip` (document coordinates; the painter set up as for the layout).
void drawFloatingPictures(QPainter *painter, const QTextDocument &document, const QRectF &clip, bool behind);

} // namespace QindaOffice::Docx
