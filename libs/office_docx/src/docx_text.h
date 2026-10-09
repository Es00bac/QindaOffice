// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QTextDocument helpers shared by office_docx and QindaWrite: pictures as
// bytes the document owns, headers/footers with page-number fields, and the
// one safe way to set a block's character format inside a table cell.

#include <QFont>
#include <QRectF>
#include <QHash>
#include <QString>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

#include <memory>

class QPainter;

namespace QindaOffice::Docx {

// QTextCursor::setBlockCharFormat() that leaves a table cell's own format
// (spans, shading, borders, padding, vertical alignment) intact.
// AGENT-GUARD: every caller that sets a block's char format on a block that
// may be the first of a table cell must use this (StyleCatalog::apply did
// not, and wiped imported cell shading when a style was applied).
void setBlockCharFormatKeepingCell(QTextCursor &cursor, const QTextCharFormat &format);

// Stores `bytes` in `media` under a fresh "qwmedia:imageN.ext" name,
// registers the decoded image with `document`, and returns the name; empty
// when the bytes are not an image Qt can decode.
QString addPicture(QTextDocument *document, QHash<QString, QByteArray> *media, const QByteArray &bytes);

// The document as HTML with every qwmedia: picture inlined as a data: URI,
// so the file stands alone (QTextDocument::setHtml decodes them back).
// Inserts a PAGE (or, with `total`, NUMPAGES) field at the cursor in its
// current character format: the result text "1" carries the instruction
// (kFieldInstruction), filled in per page when a header is drawn, written
// as w:fldSimple. Returns the format to keep typing in (without the field).
QTextCharFormat insertPageField(QTextCursor &cursor, bool total);

// Removes the display-only page breaks QindaWrite's pagination added
// (kSyntheticBreak), restoring each block's own flags; returns how many
// there were. Not undo-neutral: for clones and freshly read documents.
int removeSyntheticBreaks(QTextDocument *document);

[[nodiscard]] QString htmlWithEmbeddedPictures(const QTextDocument &document,
                                               const QHash<QString, QByteArray> &media);

// A header/footer from a plain pattern ("Page {page} of {pages}"): one
// centred paragraph in `font`, the tokens as PAGE / NUMPAGES fields.
[[nodiscard]] std::unique_ptr<QTextDocument> headerFooterFromPattern(const QString &pattern, const QFont &font);

// The pattern a header/footer reads as (fields back to {page}/{pages}).
// `simple` is false when the content is richer than one plain line —
// pictures, tables, several paragraphs, mixed formatting — so replacing it
// with a pattern would lose something.
[[nodiscard]] QString patternFromHeaderFooter(const QTextDocument &document, bool *simple = nullptr);

// A copy of a header/footer whose PAGE / NUMPAGES fields show `page` and
// `pageCount` (pictures included).
[[nodiscard]] std::unique_ptr<QTextDocument> instantiateHeaderFooter(const QTextDocument &document, int page,
                                                                    int pageCount);

// Paints Word paragraph borders (kParagraphBorders) of the document's
// top-level paragraphs that meet `clip` (document coordinates; the painter
// is already set up the way QAbstractTextDocumentLayout::draw expects).
// Consecutive paragraphs with the same borders share one box, as in Word.
void drawParagraphBorders(QPainter *painter, const QTextDocument &document, const QRectF &clip);

// Pads table rows up to their Word minimum height (kRowHeight): Qt tables
// have no row heights, and forms use tall empty rows as writing space.
// Lays the document out first; call after the page width is final.
void applyRowHeights(QTextDocument *document);

// The laid-out height of a header/footer across `width` px (page fields
// filled in for page 1), for Word's rule that a tall header pushes the
// body down.
[[nodiscard]] qreal headerFooterHeight(const QTextDocument &story, qreal width);

} // namespace QindaOffice::Docx
