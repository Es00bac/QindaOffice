// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A text block's formatted text and tags in the exports (M84, D-093):
//  - Word (note_word_export.cpp): Word runs — font, size, bold, italic,
//    underline, strike, colour, highlight, super/subscript, links — and
//    Word's Title, Heading 1–6, Quote and Code styles, bulleted and
//    numbered lists, tables, inline pictures; To Do as ☐ / ☑, every other
//    tag as its icon (a small picture) before the paragraph.
//  - Markdown (Export Markdown, the AI's page context): GitHub Markdown
//    plus the page vocabulary (note_markdown_style.h) — ++underline++,
//    ==highlight==, x^2^, H~2~O — To Do as "- [ ]" / "- [x]", other tags as
//    [#Tag] badges. Text colour has no Markdown form and is dropped.
// PDF and paper paint the block itself (note_rich_render.h).

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class QTextCursor;
class QTextDocument;

namespace QindaOffice::Docx {
struct WordParts;
}

namespace QindaOffice::Apps::Notes::RichExport {

// Appends block `block` (a text block, rich or plain) at `cursor`, which
// sits in a fresh, empty paragraph of `doc`. `maxWidth` bounds pictures.
void insertWord(QTextCursor &cursor, const QJsonObject &block, QTextDocument &doc, Docx::WordParts &parts,
                qreal maxWidth);
// Block-level tags (a checklist, markdown or picture block) as icons plus
// names at `cursor` — empty when the block has none.
void insertWordTagIcons(QTextCursor &cursor, const QJsonArray &tags, QTextDocument &doc, Docx::WordParts &parts);

// The block as Markdown (rich or plain text, with its tags).
[[nodiscard]] QString markdown(const QJsonObject &block);
// A tag as a Markdown badge: [#Important], [#Remember-for-later].
[[nodiscard]] QString badge(const QString &name);

} // namespace QindaOffice::Apps::Notes::RichExport
