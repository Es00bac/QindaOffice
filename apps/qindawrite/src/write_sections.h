// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word sections in QindaWrite's layout (M88, D-096): every section on its
// own page size and margins, with its own headers, footers and page
// numbers, although Qt paginates a document with ONE page size.
//
// The canvas. QTextDocumentLayout lays the body out on pages of one size
// (QTextDocument::pageSize, the root frame's margins). The probe
// (tests/qindawrite/tst_write_probe_sections.cpp) showed that child frames
// would give a section its own width, but a selection across a section
// break would then take both whole sections; so sections stay in the root
// frame and the canvas is fitted to all of them:
//  - width: the canvas's text column is as wide as the widest section's; a
//    narrower section's paragraphs and tables carry a display-only right
//    margin pad (Docx::kSectionPad) so their lines wrap at its own width;
//  - height: the canvas holds as much text as the tallest section's page;
//    in a section that holds less, the pagination pass (write_pagination.h)
//    breaks the page where its own text area ends — before a paragraph, or
//    between two table rows;
//  - where: canvas page i is page i, shown at its section's size with its
//    text area laid over the canvas's (PageFrame::origin).
// The canvas's margins are the closing section's, as the whole document's
// were before M88: a one-section document lays out exactly as it did, and a
// section elsewhere changes only the canvas page size (never an undo item)
// and pads (joined to the edit that caused them).
//
// Ownership: WriteDocument owns this, the body and the parts. GUI thread.

#include "docx_sections.h"
#include "page_setup_io.h"

#include <QHash>
#include <QList>
#include <QMarginsF>
#include <QPointer>
#include <QSizeF>
#include <QTextDocument>

#include <functional>

namespace QindaOffice::Apps::Write {

// One section as it is laid out.
struct SectionGeometry {
    Docx::SectionRef ref;
    qreal left = 96;   // the text column's left edge on its page (margin + gutter)
    qreal right = 96;
    qreal top = 96;    // where its body starts: a tall header pushes it down (Word)
    qreal bottom = 96; // its body's bottom margin: a tall footer pushes it up

    [[nodiscard]] QSizeF pageSize() const { return ref.settings.layout.pageSize; }
    [[nodiscard]] qreal textWidth() const { return pageSize().width() - left - right; }
    [[nodiscard]] qreal textHeight() const { return pageSize().height() - top - bottom; }
    // Whether the section starts on a new page (next, even or odd page).
    [[nodiscard]] bool startsPage(int index) const;
};

class SectionLayout {
public:
    // `body` and `parts` outlive this (WriteDocument owns all three).
    SectionLayout(QTextDocument *body, Docx::WordParts *parts);

    // Reads the sections and their headers' heights again.
    void update();
    [[nodiscard]] const QList<SectionGeometry> &sections() const { return m_sections; }
    [[nodiscard]] const QList<Docx::SectionRef> &refs() const { return m_refs; }
    [[nodiscard]] int count() const { return int(m_sections.size()); }
    [[nodiscard]] int sectionAt(int position) const;

    // The canvas the body is laid out on (page size; root frame margins).
    [[nodiscard]] QSizeF canvasSize() const { return m_canvasSize; }
    [[nodiscard]] QMarginsF canvasMargins() const { return m_canvasMargins; }
    [[nodiscard]] qreal canvasTextWidth() const;
    [[nodiscard]] qreal canvasTextHeight() const;
    // The pad a top-level paragraph or table of section `index` needs.
    [[nodiscard]] qreal padFor(int index) const;
    // Sets every top-level paragraph's and table's pad to its section's;
    // each change runs through `edit` (the caller's undo grouping).
    // Returns how many changed.
    int applyPads(const std::function<void(const std::function<void()> &)> &edit) const;

    // The header or footer document section `index` shows in `slot` (links
    // to previous sections followed), or null.
    [[nodiscard]] const QTextDocument *story(int index, bool footer, Docx::HeaderFooterSlot slot) const;
    // Every page of the laid-out body (`pages` of them, endnote pages
    // included): its section, size, canvas origin, number, header, footer.
    [[nodiscard]] QList<PageFrame> pageFrames(int pages) const;
    // The section each canvas page belongs to: the one of the first text on
    // it; a page without text (an odd / even page break's blank page) the
    // one before. Needs the body laid out.
    [[nodiscard]] QList<int> pageSections(int pages) const;

private:
    [[nodiscard]] qreal storyHeight(int index, bool footer, qreal width) const;

    QTextDocument *m_body;
    Docx::WordParts *m_parts;
    // Laid-out header and footer heights by document, its revision and the
    // width (a header is laid out again only when it or its width changed).
    struct Height {
        QPointer<const QTextDocument> document; // null once that document is gone
        int revision = -1;
        qreal width = 0;
        qreal height = 0;
    };
    mutable QHash<const QTextDocument *, Height> m_heights;
    QList<Docx::SectionRef> m_refs;
    QList<SectionGeometry> m_sections;
    QSizeF m_canvasSize;
    QMarginsF m_canvasMargins;
};

} // namespace QindaOffice::Apps::Write
