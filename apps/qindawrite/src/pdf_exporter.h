// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QTextDocument;
class QString;
class QSizeF;
class QPainter;

#include <functional>

namespace QindaOffice::Apps::Write {

// WYSIWYG PDF export (plan section 7.1): the same pageCount the screen
// view computes drives a per-page loop (translate -i*H, clip, draw) so the
// PDF paginates identically with the screen by construction. Paper size
// and resolution follow the target; the document is 96-dpi design-metric.
[[nodiscard]] bool exportDocumentPdf(QTextDocument *document, const QString &path, QString *error);

// Renders document pages into an already-active painter whose viewport is
// `targetPixels` (printer paper or PDF page). One call draws every page,
// calling painter->device()-independent new-page handling through the
// optional callback. This is THE pagination loop shared by the PDF
// exporter and the print path, so all outputs match the screen.
void renderDocumentPages(QPainter *painter, const QSizeF &targetPixels, QTextDocument *document,
                         const std::function<void()> &newPage);

} // namespace QindaOffice::Apps::Write
