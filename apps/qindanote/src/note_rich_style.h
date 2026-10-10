// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// OneNote's styles, links and table commands on a text block's formatted
// text (M84, D-093), as QindaTK document transactions — each call is ONE
// transaction, so one undo step on the page history.
//
// Styles: Normal, Title, Heading 1–6, Quote and Code. Headings are the
// paragraph heading level (QindaTK's heading defaults: 20/16/13.5/12/11/11
// pt, bold); Title, Quote and Code are named paragraph styles a document
// gets the first time one is used.
//
// AGENT-CONTRACT: the style NAMES "Title", "Quote" and "Code" are how the
// Word export (note_rich_export.h) maps them to Word's Title, Quote and
// HTML Code styles; renaming one breaks that mapping.

#include <QString>
#include <QVariantMap>

#include <optional>

namespace QindaTK {
class RichTextDocument;
namespace RichText {
struct Position;
struct Selection;
}
}

namespace QindaOffice::Apps::Notes::RichStyle {

enum class Style { Normal, Title, Heading1, Heading2, Heading3, Heading4, Heading5, Heading6, Quote, Code };
enum class TableEdit { RowAbove, RowBelow, ColumnLeft, ColumnRight, DeleteRow, DeleteColumn, DeleteTable };

[[nodiscard]] QString styleName(Style style);
// The style of the paragraph at `position`.
[[nodiscard]] Style at(const QindaTK::RichTextDocument &doc, const QindaTK::RichText::Position &position);
bool apply(QindaTK::RichTextDocument &doc, const QindaTK::RichText::Selection &selection, Style style);

// A collapsed selection gets `label` (the address when empty) inserted as
// a link; a range becomes the link. Returns the caret after it.
std::optional<QindaTK::RichText::Position> insertLink(QindaTK::RichTextDocument &doc,
                                                      const QindaTK::RichText::Selection &selection,
                                                      const QString &href, const QString &label);

// The table edit at the caret's cell; false outside a table.
bool editTable(QindaTK::RichTextDocument &doc, const QindaTK::RichText::Position &caret, TableEdit edit);
[[nodiscard]] bool inTable(const QindaTK::RichTextDocument &doc, const QindaTK::RichText::Position &caret);

} // namespace QindaOffice::Apps::Notes::RichStyle
