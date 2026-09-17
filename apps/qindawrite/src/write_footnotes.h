// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Footnotes for QindaWrite.
//
// A footnote is two things: a superscript MARKER in the body, and a NOTE
// printed at the foot of the page the marker landed on. Qt's QTextDocument
// has no footnote concept, so the marker is an ordinary text run carrying
// kFootnoteIdProperty and the note text lives beside the document, keyed by
// that id.
//
// Numbering is never stored: it is the marker's position in document order,
// recomputed on every read. That is what makes inserting a footnote in the
// middle renumber everything after it without touching a thing.
//
// The notes are drawn into the page's bottom margin band by
// drawPageDecoration, which the screen view, the print path and the PDF
// exporter all already call — so all three agree by construction.
//
// AGENT-NOTE (documented bound): the band is the bottom margin, so a page
// can show about six short notes before the rest are elided with "…". A
// growing band would need per-page re-layout, which QTextDocument's single
// flow cannot express.

#include <QHash>
#include <QList>
#include <QString>
#include <QTextFormat>

class QTextCursor;
class QTextDocument;

namespace QindaOffice::Apps::Write {

class PageGeometry;

// Set on the marker run's char format; its value is the footnote id.
inline constexpr int kFootnoteIdProperty = QTextFormat::UserProperty + 8;

struct FootnoteRef {
    QString id;
    int number = 1;   // 1-based, by document order
    int position = 0; // document position of the marker
    int page = 1;     // 1-based page the marker falls on
};

// Every marker in the document, in order, numbered and placed on a page.
[[nodiscard]] QList<FootnoteRef> collectFootnotes(const QTextDocument *document,
                                                  const PageGeometry &geometry);

// Page (1-based) -> the note lines to print at its foot, already numbered.
[[nodiscard]] QHash<int, QStringList> footnotesByPage(const QTextDocument *document,
                                                      const PageGeometry &geometry,
                                                      const QHash<QString, QString> &texts);

// Inserts a marker at the cursor and returns its fresh id. The caller
// stores the note text against that id on the document.
[[nodiscard]] QString insertFootnoteMarker(QTextCursor cursor);

// The id of the marker at (or immediately before) `position`, or empty.
// Double-clicking a marker edits its note through this.
[[nodiscard]] QString footnoteIdAt(const QTextDocument *document, int position);

// Rewrites every marker's visible glyph to its current number. Numbering
// is document order, so inserting a note in the middle changes the ones
// after it; this is what makes the page show that.
void renumberFootnoteMarkers(QTextDocument *document);

// Drops markers whose note has been deleted and note texts whose marker is
// gone, so the two halves cannot drift apart.
void pruneOrphanFootnotes(const QTextDocument *document, QHash<QString, QString> *texts);

} // namespace QindaOffice::Apps::Write
