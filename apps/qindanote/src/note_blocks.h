// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The layout of a note page's content blocks.
//
// A page carries typed text, checklist items and images in the envelope's
// blocksJson. Where each one SITS has to be computed identically by the
// screen item (which paints it and places the one live editor there) and
// by the pure page renderer (which prints it) — a layout computed twice is
// a layout that drifts, and the symptom is typed text printing somewhere
// the user never saw it.
//
// AGENT-CONTRACT: this is the one place block geometry is decided. Both
// note_page_item.cpp and note_page_render.cpp lay out through layoutBlocks.
//
// Blocks are free-floating (D-027): a block with a stored `rect` [x, y, w]
// sits there, its height computed from its content; a block WITHOUT one
// (every M21 notebook) is stacked down the page exactly where it always
// was, and gains a rect the first time it is written back after an edit.

#include <QByteArray>
#include <QFont>
#include <QJsonObject>
#include <QList>
#include <QRectF>
#include <QString>

namespace QindaOffice::Apps::Notes {

struct PageBlock {
    enum class Kind { Text, Check, Image };

    Kind kind = Kind::Text;
    int index = 0;      // position in the blocksJson array
    QString text;
    bool checked = false;
    int level = 0;        // Kind::Check: indent level 0–3
    QByteArray imageData; // Kind::Image: the picked file's bytes, verbatim
    QRectF rect;          // page coordinates
    bool floating = false; // true when the JSON carried a rect
};

// Page-space insets shared by the item and the renderer.
inline constexpr qreal kBlockLeft = 40;
inline constexpr qreal kBlockTop = 40;
inline constexpr qreal kBlockGap = 10;
inline constexpr qreal kTextBlockMinHeight = 24;
inline constexpr qreal kCheckBlockHeight = 28;
inline constexpr qreal kCheckIndent = 24;
inline constexpr qreal kTextPadding = 4;

// The font typed blocks are laid out and painted with, on screen and on
// paper (the same font, so the two cannot disagree).
[[nodiscard]] QFont pageFont();

// Parses blocksJson: floating blocks at their rect, the rest stacked down
// the page, each one as tall as its kind and content need. `pageWidth` is
// the page's width in page coordinates.
[[nodiscard]] QList<PageBlock> layoutBlocks(const QByteArray &blocksJson, qreal pageWidth);

// The height a text block needs for `text` at `width` (word-wrapped in
// pageFont), never below kTextBlockMinHeight.
[[nodiscard]] qreal textBlockHeight(const QString &text, qreal width);

// The height an image block occupies for its stored bytes, bounded so one
// photograph cannot push every other block off the page.
[[nodiscard]] qreal imageBlockHeight(const QByteArray &imageData, qreal availableWidth);

// JSON helpers over the envelope's blocks array (the source of truth).
[[nodiscard]] QJsonArray blocksArray(const QByteArray &blocksJson);
[[nodiscard]] QByteArray withBlocksArray(const QByteArray &blocksJson, const QJsonArray &blocks);
// A rect stored as [x, y, w] (height is always computed).
void setBlockRect(QJsonObject *block, const QRectF &rect);

} // namespace QindaOffice::Apps::Notes
