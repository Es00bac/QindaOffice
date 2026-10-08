// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A note page as a Word document (M65, D-068) — what OneNote's "Export
// page as Word document" gives: the title, typed and formatted notes as rich
// text (Markdown blocks through the same builder the page paints with, so
// colours, callouts and tables come along), checklists as ☐ / ☑ lines,
// pictures with their own bytes, and the handwriting as one picture of the
// ink. Blocks follow reading order (top to bottom, then left to right).
// Written through office_docx, so Microsoft Word opens it as it is.

#include <QString>

namespace QindaOffice::Apps::Notes {

struct NotePage;

// `pageWidth` is the page's width in page coordinates (layoutBlocks).
bool exportPageAsWord(const NotePage &page, qreal pageWidth, const QString &path, QString *error = nullptr);

} // namespace QindaOffice::Apps::Notes
