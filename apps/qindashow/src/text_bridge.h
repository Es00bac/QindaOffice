// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QTextDocument <-> show_model TextParagraph bridge, shared by the canvas
// in-place editor and the tests. Runs keep bold/italic/size/color/family
// (and underline/strike) as character formats; block alignment maps to
// TextParagraph::alignment. M62: everything else a paragraph or run carries
// (bullets, numbering, spacing, indents, baseline, caps, highlight,
// hyperlinks, fields) rides a user property of the block / character
// format and comes back unchanged, and sizes are scaled so the editor
// shows text at the size the renderer paints it (text_painter.h).

#include "presentation.h"

class QObject;
class QTextDocument;

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Apps::Show {

[[nodiscard]] QList<ShowModel::TextParagraph> paragraphsFromDocument(const QTextDocument &document);
[[nodiscard]] QTextDocument *documentFromParagraphs(const QList<ShowModel::TextParagraph> &paragraphs,
                                                    QObject *parent = nullptr);

} // namespace QindaOffice::Apps::Show
