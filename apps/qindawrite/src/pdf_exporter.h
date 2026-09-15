// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

class QTextDocument;
class QString;

namespace QindaOffice::Apps::Write {

// WYSIWYG PDF export (plan section 7.1): the same pageCount the screen
// view computes drives a per-page loop (translate -i*H, clip, draw) so the
// PDF paginates identically with the screen by construction. Paper size
// and resolution follow the target; the document is 96-dpi design-metric.
[[nodiscard]] bool exportDocumentPdf(QTextDocument *document, const QString &path, QString *error);

} // namespace QindaOffice::Apps::Write
