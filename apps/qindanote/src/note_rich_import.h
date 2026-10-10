// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// HTML, Markdown and Word content as a text block's formatted text (M84,
// D-093): what Paste, Import ▸ Word document / Markdown / HTML file and the
// page templates build blocks from.
//
// AGENT-GUARD (sanitised HTML): nothing pasted or imported may run, load or
// reach anything. sanitizeHtml() removes scripts, styles, frames, objects,
// forms, event handlers, javascript:/vbscript: links and every picture that
// is not already inline (a data: image) — QTextDocument never fetches a
// remote or local file for us — and CSS url()s. Links keep http(s),
// mailto, ftp, file and in-page anchors only.

#include <QHash>
#include <QJsonArray>
#include <QString>

#include <memory>

class QTextDocument;

namespace QindaTK {
class RichTextDocument;
}

namespace QindaOffice::Apps::Notes::RichImport {

[[nodiscard]] QString sanitizeHtml(const QString &html, qreal maxPictureWidth = 640);
// True for a link address a note may keep.
[[nodiscard]] bool safeHref(const QString &href);

// A formatted text with the To Do tags its task items became (entries in
// the block `tags` format of note_tags.h).
struct Imported {
    std::unique_ptr<QindaTK::RichTextDocument> doc;
    QJsonArray tags;
};

[[nodiscard]] Imported fromHtml(const QString &html, qreal maxPictureWidth = 640);
// GitHub Markdown plus the page's vocabulary (note_markdown_style.h):
// headings, lists, tables, **bold**, ==highlights==, ++underline++, x^2^,
// links; "- [ ]" / "- [x]" items become To Do tags.
[[nodiscard]] Imported fromMarkdown(const QString &markdown, qreal maxPictureWidth = 640);
// A Word body (office_docx's reader): pictures from `media` inline.
[[nodiscard]] Imported fromTextDocument(const QTextDocument &document, const QHash<QString, QByteArray> &media,
                                        qreal maxPictureWidth = 640);

// The block JSON of a text block holding `imported` (no rect: it stacks).
[[nodiscard]] QJsonObject textBlock(const Imported &imported);

} // namespace QindaOffice::Apps::Notes::RichImport
