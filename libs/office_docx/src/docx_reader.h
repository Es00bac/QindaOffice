// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Reading Word documents (.docx, .docm, .dotx, .dotm) natively (D-068).
//
// The body becomes `body` (cleared first): paragraphs with Word's resolved
// formatting, lists numbered as Word numbers them, tables with merges,
// borders and shading, pictures with their original bytes, hyperlinks,
// bookmarks, page/date fields, footnote markers, tracked insertions and
// deletions. Everything else (text boxes, shapes, charts, SmartArt,
// OLE objects, comments, endnotes) is preserved verbatim and
// shown approximately; WordParts::warnings says what.
//
// M92b: equations become native OMML inline objects with shared math layout.
// Page layout, headers/footers (rich, per slot), footnote texts, pictures and
// the source package land in `parts`.
//
// Threading: runs on the calling thread; `body` must not be shown in a view
// while it is being filled.

#include "docx_model.h"

#include <QString>
#include <QTextDocument>

#include <memory>

namespace QindaOffice::Docx {

// False (with `error`) when the file is not a readable Word document.
bool readDocx(const QString &path, QTextDocument *body, WordParts *parts, QString *error = nullptr);
bool readDocx(std::shared_ptr<const Ooxml::Package> package, QTextDocument *body, WordParts *parts,
              QString *error = nullptr);

// True when `package`'s main part is a WordprocessingML document.
[[nodiscard]] bool isWordPackage(const Ooxml::Package &package);

} // namespace QindaOffice::Docx
