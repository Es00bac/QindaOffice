// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure page renderer for QindaNote (plan section 6.4).
//
// AGENT-CONTRACT: this draws a page with NO widget, palette or zoom in
// sight, so print, preview and any future export all paginate through the
// same code the screen agrees with. Paper is paper (decisions D-004): the
// page is white with dark ink whatever the desktop palette is doing, which
// is also what a printer needs.

#include "notebook.h"

class QPainter;

namespace QindaOffice::Apps::Notes {

// Draws `page` into the rect (0, 0, pageSize) of `painter`, in page
// coordinates. The caller owns any scaling or translation.
void renderNotePage(QPainter *painter, const NotePage &page, const QSizeF &pageSize);

} // namespace QindaOffice::Apps::Notes
