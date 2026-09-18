// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure page renderer for QindaNote (plan section 6.4).
//
// AGENT-CONTRACT: this draws a page with NO item, palette or zoom in
// sight, so print, preview and any future export all paginate through the
// same code the screen agrees with. Paper is paper (decisions D-004): the
// page is white with dark ink whatever the desktop palette is doing, which
// is also what a printer needs.
//
// Render order (shared with NotePageItem::paint, so the screen and the
// printer cannot drift): background → image blocks → text/checklist blocks
// → highlighter strokes → pen strokes. Ink sits above typed text.

#include "note_blocks.h"
#include "notebook.h"

class QPainter;

namespace QindaOffice::Apps::Notes {

// Draws `page` into the rect (0, 0, pageSize) of `painter`, in page
// coordinates. The caller owns any scaling or translation.
void renderNotePage(QPainter *painter, const NotePage &page, const QSizeF &pageSize);

// The pieces the screen item composes with its own ink pass and editor:
// guide lines for `background` over a page of `pageSize` (page space).
void paintNoteBackground(QPainter *painter, const QString &background, const QSizeF &pageSize);
// Every block of `kind` except `skipIndex` (the one under a live editor).
void paintNoteBlocks(QPainter *painter, const QList<PageBlock> &blocks, PageBlock::Kind kind,
                     int skipIndex = -1);

} // namespace QindaOffice::Apps::Notes
