// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaWrite's format matrix for a bare QTextDocument (plan section 6.1,
// D-068). WriteDocument (write_document_io.cpp) is the full path — it adds
// page layout, headers/footers, footnotes and pictures; these helpers are
// what it and the tests use for the text alone:
//  - .html/.htm  QTextDocument HTML
//  - .md         QTextDocument Markdown (Qt 6)
//  - .txt        plain text
//  - .docx/.docm/.dotx/.dotm natively through office_docx
//  - .odt        saved with QTextDocumentWriter (ODF); opened, like .doc
//    and .rtf, through LibreOffice → .docx → office_docx (never HTML)
// The default Save format is Word (.docx): the owner's documents are Word
// documents and must reopen in Microsoft Word (D-068).

class QTextDocument;
class QString;

namespace QindaOffice::Apps::Write {

[[nodiscard]] bool loadDocument(const QString &path, QTextDocument *document, QString *error);
[[nodiscard]] bool saveDocument(const QString &path, QTextDocument *document, QString *error);
[[nodiscard]] QString defaultSaveFilter();
[[nodiscard]] QString openFilterString();

} // namespace QindaOffice::Apps::Write
