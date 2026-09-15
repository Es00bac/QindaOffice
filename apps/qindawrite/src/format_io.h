// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaWrite format matrix (plan section 6.1):
//  - .html/.htm  native (QTextDocument HTML)
//  - .md         native (QTextDocument Markdown, Qt 6)
//  - .txt        plain text
//  - .odt        native save via QTextDocumentWriter (ODF)
//  - .odt/.docx/.doc/.rtf OPEN and .docx/.rtf SAVE through the headless
//    LibreOffice bridge (HTML intermediate, blocking wait over the async
//    converter). Page setup meta is not round-tripped through ODT/DOCX in
//    v1 (documented limitation, plan 6.1).
// Default Save format is .odt.

class QTextDocument;
class QString;

namespace QindaOffice::Apps::Write {

[[nodiscard]] bool loadDocument(const QString &path, QTextDocument *document, QString *error);
[[nodiscard]] bool saveDocument(const QString &path, QTextDocument *document, QString *error);
[[nodiscard]] QString defaultSaveFilter();
[[nodiscard]] QString openFilterString();

} // namespace QindaOffice::Apps::Write
