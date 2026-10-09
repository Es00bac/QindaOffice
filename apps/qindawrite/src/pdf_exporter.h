// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "page_setup_io.h"

class QTextDocument;
class QSizeF;
class QPainter;
class QRectF;

#include <functional>

namespace QindaOffice::Apps::Write {

// WYSIWYG PDF export (plan section 7.1): the same pageCount the screen
// view computes drives a per-page loop (translate -i*H, clip, draw) so the
// PDF paginates identically with the screen by construction. Paper size
// and resolution follow the target; the document is 96-dpi design-metric.
[[nodiscard]] bool exportDocumentPdf(QTextDocument *document, const QString &path, QString *error,
                                     const PageDecoration &decoration = {});

// Renders document pages into an already-active painter whose viewport is
// `targetPixels` (printer paper or PDF page). One call draws every page,
// calling painter->device()-independent new-page handling through the
// optional callback. This is THE pagination loop shared by the PDF
// exporter and the print path, so all outputs match the screen; the
// optional decoration draws headers/footers into the margins.
void renderDocumentPages(QPainter *painter, const QSizeF &targetPixels, QTextDocument *document,
                         const std::function<void()> &newPage,
                         const PageDecoration &decoration = {});

// Draws one page's header/footer into `pageRect` (document coordinates,
// already translated to the page being painted). `omit` is a header or
// footer the caller draws itself (the one being edited in place).
void drawPageDecoration(QPainter *painter, const QRectF &pageRect, const PageDecoration &decoration,
                        int pageIndex, int pageCount, const QTextDocument *omit = nullptr);

} // namespace QindaOffice::Apps::Write
