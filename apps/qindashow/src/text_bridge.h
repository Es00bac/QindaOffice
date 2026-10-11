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

class QColor;
class QObject;
class QTextDocument;

#ifndef QQO_SHOW_MODEL_ALIAS
#define QQO_SHOW_MODEL_ALIAS
namespace ShowModel = QindaOffice::Show;
#endif

namespace QindaOffice::Apps::Show {

[[nodiscard]] QList<ShowModel::TextParagraph> paragraphsFromDocument(const QTextDocument &document);
// `emptyLook` (M67): the run formatting an EMPTY paragraph types in — a
// table cell's text look (TableEdit::textLook) — instead of the editor's
// default; nullptr keeps the default.
[[nodiscard]] QTextDocument *documentFromParagraphs(const QList<ShowModel::TextParagraph> &paragraphs,
                                                    QObject *parent = nullptr,
                                                    const ShowModel::TextRun *emptyLook = nullptr);

// M91 (D-099), Insert ▸ Link on the text selected in the editor: every run
// of [from, to) gets `link` (show_text.h's spelling; empty removes it) and,
// with a link, PowerPoint's look for one — underlined, in `color`.
void setHyperlink(QTextDocument &document, int from, int to, const QString &link, const QColor &color);
// The link of the text at `position` (empty: none).
[[nodiscard]] QString hyperlinkAt(const QTextDocument &document, int position);

} // namespace QindaOffice::Apps::Show
