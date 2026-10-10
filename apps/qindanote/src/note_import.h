// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// File ▸ Import (M84, D-093): documents become note pages of formatted text
// (note_rich_import.h), inserted after the open page.
//  - Word (.docx/.docm/.dotx/.dotm) through office_docx: headings, lists,
//    tables, pictures and formatting. A document with two or more Heading 1
//    paragraphs becomes one page per Heading 1 (the heading is the page
//    title; text before the first one is a page of its own), so a long
//    report is a section of pages, as OneNote's own import gives.
//  - Markdown (.md/.markdown): one page; a leading "# Title" names it.
//  - HTML (.html/.htm): one page, sanitised; its <title> names it.
//  - OneNote (.one/.onepkg): refused, with the way round — the MS-ONESTORE
//    format has no open implementation the suite can use. Export from
//    OneNote to Word or PDF and import that.

#include <QJsonArray>
#include <QList>
#include <QString>

namespace QindaOffice::Apps::Notes::Import {

struct ImportedPage {
    QString title;
    QJsonArray blocks;
};

enum class Kind { Word, Markdown, Html, OneNote, Unknown };
[[nodiscard]] Kind kindOf(const QString &path);
// The name filters for the Import dialog (QtQuick.Dialogs form).
[[nodiscard]] QStringList nameFilters();
// What to say about a OneNote file.
[[nodiscard]] QString oneNoteExplanation();

// False (with `error`) when the file cannot be read or is not supported.
bool importFile(const QString &path, QList<ImportedPage> *pages, QString *error);

} // namespace QindaOffice::Apps::Notes::Import
