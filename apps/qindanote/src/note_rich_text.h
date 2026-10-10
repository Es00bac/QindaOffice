// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Formatted typing in text blocks (M84, D-093). A text block's content is a
// QindaTK rich-text document (QindaTK::RichTextDocument): paragraphs with
// character runs, headings and named paragraph styles (Title, Quote, Code),
// bulleted and numbered lists, tables and links.
//
// AGENT-CONTRACT (the page format, docs/qindanote.md#page-format): a text
// block keeps `text` — the plain-text mirror every older reader, the search,
// the assistant and the agent use — and, when the text carries anything
// plain text cannot, `rich`: the qindatk.richtext codec object
// (RichTextCodec::toJsonObject). Paragraph tags (note_tags.h) name rich
// paragraphs by their stable ids, so a tagged block always keeps `rich`.
// A block whose `rich` no longer matches its `text` was edited by a
// QindaNote older than M84 (it rewrote `text` and kept `rich` verbatim):
// the text wins and the stale formatting is ignored.
//
// Threading: GUI thread (RichTextDocument is a QObject).

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <memory>

namespace QindaTK {
class RichTextDocument;
namespace RichText {
struct DocumentDefaults;
struct Fragment;
}
}

namespace QindaOffice::Apps::Notes::RichBlock {

// The page format M84 writes (blocksJson "version"; absent means 1).
inline constexpr int kPageFormatVersion = 2;

// Paper ink and the page font (note_blocks.h pageFont) as document
// defaults, so rich text and plain blocks look alike (D-004).
[[nodiscard]] QindaTK::RichText::DocumentDefaults pageDefaults();

[[nodiscard]] std::unique_ptr<QindaTK::RichTextDocument> fromPlainText(const QString &text);
// Null when `rich` is not a valid qindatk.richtext object. The stored
// defaults are replaced by pageDefaults(): a notebook follows the machine's
// page font like its plain blocks do.
[[nodiscard]] std::unique_ptr<QindaTK::RichTextDocument> fromJson(const QJsonObject &rich);
// A fragment (an HTML or Word import) as a whole document; the body is
// given the closing paragraph the model requires.
[[nodiscard]] std::unique_ptr<QindaTK::RichTextDocument> fromFragment(QindaTK::RichText::Fragment fragment);
// The block's document: its `rich` when usable, else built from `text`.
[[nodiscard]] std::unique_ptr<QindaTK::RichTextDocument> forBlock(const QJsonObject &block);

[[nodiscard]] QJsonObject toJson(const QindaTK::RichTextDocument &doc);
// Paragraphs joined by '\n'; table cells by '\t' and rows by '\n'; soft
// line breaks as '\n'; objects (pictures) dropped.
[[nodiscard]] QString plainText(const QindaTK::RichTextDocument &doc);
// True when plain text loses nothing: default paragraph and run formats,
// no list, table, object, soft break or styled paragraph.
[[nodiscard]] bool isPlain(const QindaTK::RichTextDocument &doc);
// Every paragraph id in document order (table cells included).
[[nodiscard]] QStringList paragraphIds(const QindaTK::RichTextDocument &doc);

// Writes `doc` into a text block: `text` always; `rich` unless the
// document is plain and untagged; tags on paragraphs that no longer exist
// are dropped.
void store(QJsonObject *block, const QindaTK::RichTextDocument &doc);
// Notebook-wide Replace in formatted text: every occurrence inside one
// paragraph replaced in the format of its first character, as one
// transaction; returns how many.
int replaceAll(QindaTK::RichTextDocument &doc, const QString &needle, const QString &replacement,
               Qt::CaseSensitivity sensitivity);
// Whether an open editor may stay over a block whose stored form went from
// `a` to `b` (an undo or redo): the same text, with only its tags, its
// place, or a `rich` that adds no formatting coming or going (a first tag
// on a plain block, and its undo).
[[nodiscard]] bool sameEditorContent(QJsonObject a, QJsonObject b);
// A plain-text rewrite (the assistant's replace_block, an agent edit):
// `text` replaced, the formatting and paragraph tags that no longer match
// it dropped.
void setPlainText(QJsonObject *block, const QString &text);

} // namespace QindaOffice::Apps::Notes::RichBlock
