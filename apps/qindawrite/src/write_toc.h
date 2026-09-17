// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Table of contents for QindaWrite.
//
// The entries come from the document's own heading styles (StyleCatalog),
// and the page numbers come from the SAME PageGeometry the view paints
// with — so a TOC built on screen and a TOC printed on paper agree by
// construction, which is the only thing that makes page numbers in a
// contents list worth printing.
//
// AGENT-CONTRACT: the generated blocks carry kTocBlockProperty. That is how
// Update finds the previous table and replaces exactly it, instead of
// appending a second one every time the user asks.

#include <QList>
#include <QString>
#include <QTextFormat>

class QTextCursor;
class QTextDocument;

namespace QindaOffice::Apps::Write {

class PageGeometry;

// Marks a block as part of a generated table of contents.
inline constexpr int kTocBlockProperty = QTextFormat::UserProperty + 7;

struct TocEntry {
    QString text;
    int level = 1;   // 1..6, from the heading style
    int page = 1;    // 1-based
    int position = 0; // document position of the heading block
};

// Every heading in document order, with the page it falls on.
[[nodiscard]] QList<TocEntry> collectHeadings(const QTextDocument *document,
                                              const PageGeometry &geometry);

// Writes the table at `at`, replacing a previous generated table if the
// cursor is inside one. Returns the number of entries written.
int insertTableOfContents(QTextDocument *document, const PageGeometry &geometry,
                          const QTextCursor &at);

// Rebuilds the FIRST generated table in the document in place (Update).
// Returns -1 when the document has no table of contents yet.
int updateTableOfContents(QTextDocument *document, const PageGeometry &geometry);

} // namespace QindaOffice::Apps::Write
