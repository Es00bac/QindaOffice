// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Laying out and painting a text block's rich content (M84, D-093) on the
// page and on paper.
//
// AGENT-CONTRACT: like note_blocks.h for geometry, this is the ONE place a
// rich block is measured and painted — the screen item (note_page_item.cpp)
// and the print/PDF renderer (note_page_render.cpp) both come here, and the
// live editor (Tk.RichTextEditor in NotePage.qml) lays out the same
// document with the same QindaTK engine at the same width
// (block width − 2 × kTextPadding), so a block does not move when it opens
// or closes. Colours are paper colours (D-004).
//
// The decoded document and its continuous-flow layout are cached per
// distinct `rich` object (bounded, GUI thread only).

#include <QJsonObject>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

class QPainter;

namespace QindaOffice::Apps::Notes::RichRender {

// The plain text of `rich` (RichBlock::plainText), or a null QString when
// `rich` does not decode.
[[nodiscard]] QString plainText(const QJsonObject &rich);
// The block height (padding included) at block width `width`.
[[nodiscard]] qreal height(const QJsonObject &rich, qreal width);
// Paints `rich` into `box` (padding inside it).
void paint(QPainter *painter, const QRectF &box, const QJsonObject &rich);

struct LineBox {
    QString paragraph;  // paragraph id
    QRectF rect;        // block coordinates (padding included)
    bool first = false; // the paragraph's first line
};
// Every laid-out line at block width `width`, in document order.
[[nodiscard]] QList<LineBox> lines(const QJsonObject &rich, qreal width);
// The link under `point` (block coordinates), or "".
[[nodiscard]] QString linkAt(const QJsonObject &rich, qreal width, const QPointF &point);
// The caret position under `point` (block coordinates) as
// {paragraph id, offset}; {"", -1} when nothing is there.
[[nodiscard]] std::pair<QString, int> positionAt(const QJsonObject &rich, qreal width, const QPointF &point);
// One paragraph's text, or "" when the id is not there.
[[nodiscard]] QString paragraphText(const QJsonObject &rich, const QString &paragraphId);

} // namespace QindaOffice::Apps::Notes::RichRender
