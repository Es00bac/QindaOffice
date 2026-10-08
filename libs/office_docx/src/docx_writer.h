// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Writing Word documents natively (D-068).
//
// The output is ECMA-376 Transitional WordprocessingML that Microsoft Word
// opens without repair. A document read from a .docx keeps everything the
// suite does not model — styles, numbering definitions, theme, settings,
// fonts, comments, endnotes, custom XML, text boxes, charts, equations — and
// keeps the source markup of every paragraph, run, table and cell property
// the user did not change (D-068 rule 3). A new document gets a complete,
// minimal package whose styles mirror QindaWrite's.
//
// Threading: reads `body` and the header/footer documents; call on the
// thread that owns them.

#include "docx_model.h"

#include <QByteArray>
#include <QString>
#include <QTextDocument>

namespace QindaOffice::Docx {

struct WriteOptions {
    // What the package is: a .docx, a .dotx template, or their macro-enabled
    // twins. Only the macro kinds carry the source's VBA project (Word
    // refuses a .docx that contains one).
    enum Kind { Document, Template, MacroDocument, MacroTemplate };
    Kind kind = Document;
    QString application = QStringLiteral("QindaWrite");
    int pageCount = 0; // statistics for docProps/app.xml; 0 = omit
    int wordCount = 0;
};

[[nodiscard]] QByteArray docxBytes(const QTextDocument &body, const WordParts &parts,
                                   const WriteOptions &options = {}, QString *error = nullptr);
bool writeDocx(const QString &path, const QTextDocument &body, const WordParts &parts,
               const WriteOptions &options = {}, QString *error = nullptr);

} // namespace QindaOffice::Docx
