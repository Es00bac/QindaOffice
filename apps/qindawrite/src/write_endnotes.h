// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Endnotes at the end of the document (M77, D-086), as Word places them:
// after the body's last line, below a short separator rule, numbered in
// the endnote format, running on over as many pages as they need.
//
// The notes are a QTextDocument of their own laid out with the body's page
// size and margins; its first paragraph is pushed down to where the body
// ends, so its page k is the body's page firstPage + k. The screen, print
// and PDF draw it through PageDecoration (one renderer, so all three
// agree), and the page count is the body's or the notes' last page,
// whichever is later. Display only: the notes are written to endnotes.xml
// from WordParts, never from this document.

#include "docx_notes.h"

#include <QHash>
#include <QList>
#include <QRectF>
#include <QString>

#include <memory>

class QPainter;
class QTextDocument;

namespace QindaOffice::Apps::Write {

struct EndnoteArea {
    std::shared_ptr<QTextDocument> notes;
    int firstPage = 0; // 0-based body page the notes start on
    int pages = 0;     // pages of `notes`
    qreal separatorY = 0; // where the rule is drawn on the first page (page px)

    [[nodiscard]] bool isEmpty() const { return !notes || pages == 0; }
    // The last page (1-based) the notes reach; 0 without notes.
    [[nodiscard]] int lastPage() const { return isEmpty() ? 0 : firstPage + pages; }
};

// Lays out the endnotes the body references (`texts` by id) after its last
// line; empty when there are none.
[[nodiscard]] std::shared_ptr<EndnoteArea> layoutEndnotes(const QTextDocument &body,
                                                          const QHash<QString, QString> &texts,
                                                          const Docx::NoteOptions &options);

// Draws the notes that fall on page `pageIndex` (0-based) into `pageRect`
// (document coordinates of that page, already translated as for headers).
void drawEndnotes(QPainter *painter, const EndnoteArea &area, int pageIndex, const QRectF &pageRect);

} // namespace QindaOffice::Apps::Write
