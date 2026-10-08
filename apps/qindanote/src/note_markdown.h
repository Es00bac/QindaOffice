// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Markdown blocks (D-057): the live notes land on the page as ONE block of
// type "markdown" whose text is GitHub-flavoured Markdown, painted as
// formatted text — headings, lists, task boxes, bold/italic, quotes, code,
// tables, and ==highlights== (not part of CommonMark; painted as a marker
// stroke). Editing a markdown block shows its source in the live editor.
//
// AGENT-CONTRACT: the screen item and the printer both lay out and paint
// through these functions (like note_blocks.h), so a printed page matches
// the screen. The colours are PAPER colours (D-004: the document surface is
// the one place literals are allowed) — ink, a marker yellow, a heading
// blue — the same on every theme.

#include <QRectF>
#include <QString>

class QPainter;

#include <memory>

class QTextDocument;

namespace QindaOffice::Apps::Notes {

// Height of the rendered text at `width` (padding included).
[[nodiscard]] qreal markdownBlockHeight(const QString &markdown, qreal width);
// Paints `markdown` into `box` (padding inside it).
void paintMarkdown(QPainter *painter, const QRectF &box, const QString &markdown);
// The rendered markdown as HTML, colours and callout boxes included (the
// QindaWrite hand-off, M57).
[[nodiscard]] QString markdownHtml(const QString &markdown);
// The rendered markdown as a laid-out document at `width` — the one the
// page paints — for exports that keep its formatting (Word, M65).
[[nodiscard]] std::shared_ptr<QTextDocument> markdownDocument(const QString &markdown, qreal width);
// Plain text of the rendered markdown (search, the assistant's context).
[[nodiscard]] QString markdownPlainText(const QString &markdown);
// The [mm:ss] / [h:mm:ss] time under `point` (block coordinates, padding
// included) in the rendered text, in ms; -1 when there is none (M56:
// Ctrl+click replays the recording there).
[[nodiscard]] qint64 markdownTimestampAt(const QString &markdown, qreal width, const QPointF &point);

} // namespace QindaOffice::Apps::Notes
