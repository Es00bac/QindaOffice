// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QTextDocument helpers shared by office_docx and QindaWrite: pictures as
// bytes the document owns, headers/footers with page-number fields, and the
// one safe way to set a block's character format inside a table cell.

#include <QFont>
#include <QHash>
#include <QString>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>

#include <memory>

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

} // namespace QindaOffice::Docx
