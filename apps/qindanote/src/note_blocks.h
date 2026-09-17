// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The layout of a note page's content blocks.
//
// A page carries typed text, checklist items and images in the envelope's
// blocksJson. Where each one SITS has to be computed identically by the
// screen view (which puts a widget there) and by the pure page renderer
// (which prints it) — a layout computed twice is a layout that drifts, and
// the symptom is typed text printing somewhere the user never saw it.
//
// AGENT-CONTRACT: this is the one place block geometry is decided. Both
// note_page_view.cpp and note_page_render.cpp lay out through layoutBlocks.

#include <QByteArray>
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
    QByteArray imageData; // Kind::Image: the picked file's bytes, verbatim
    QRectF rect;          // page coordinates
};

// Page-space insets shared by the view and the renderer.
inline constexpr qreal kBlockLeft = 40;
inline constexpr qreal kBlockTop = 40;
inline constexpr qreal kBlockGap = 10;
inline constexpr qreal kTextBlockHeight = 90;
inline constexpr qreal kCheckBlockHeight = 28;

// Parses blocksJson and stacks the blocks down the page, each one as tall
// as its kind needs. `pageWidth` is the page's width in page coordinates.
[[nodiscard]] QList<PageBlock> layoutBlocks(const QByteArray &blocksJson, qreal pageWidth);

// The height an image block occupies for its stored bytes, bounded so one
// photograph cannot push every other block off the page.
[[nodiscard]] qreal imageBlockHeight(const QByteArray &imageData, qreal availableWidth);

} // namespace QindaOffice::Apps::Notes
