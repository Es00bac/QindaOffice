// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Page decoration (what is drawn in a page's margins) and the HTML form of
// the page setup.
//
// PageDecoration is what the screen view, the print path and the PDF
// exporter all hand to drawPageDecoration: the page layout, the header and
// footer documents per slot (rich text; PAGE / NUMPAGES fields expand per
// page), and the footnote lines per page — one renderer, so all three agree.
//
// HTML persistence (M14): QTextDocument::setHtml drops unknown <meta> tags,
// so the layout, plain header/footer patterns and footnote texts travel in
// qindawrite-* metas parsed back from the raw source. Rich headers survive
// only in .docx (and the recovery snapshot, which is .docx).

#include "docx_model.h"

#include <QHash>
#include <QList>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <array>
#include <memory>

class QTextDocument;

namespace QindaOffice::Apps::Write {

struct EndnoteArea;

// One page as the screen, print and PDF draw it (M88, D-096): its section's
// page and margins, the header and footer it shows (its slot, links to
// previous sections followed) and what its page fields say.
struct PageFrame {
    QSizeF size;             // the page's own size (document px)
    QPointF origin;          // its top-left on the layout canvas (page_geometry.h)
    Docx::PageLayout layout; // its section's margins and header / footer distances
    const QTextDocument *header = nullptr;
    const QTextDocument *footer = nullptr;
    int section = 0;          // 0-based, in document order
    int number = 1;           // the page number it shows (restarts applied)
    QString numberFormat;     // its section's w:fmt; empty is 1, 2, 3
    int sectionPages = 1;     // SECTIONPAGES
    bool firstOfSection = false;
    // Where the body's text ends on the page when its section's pages hold
    // less than the canvas (page px; 0: no limit): the body is clipped
    // there, so a table row Qt stretched to the canvas page's foot, or a
    // paragraph taller than the page, does not run into the footer.
    qreal textBottom = 0;
};

struct PageDecoration {
    // The uniform page (documents laid out without sections: a merged
    // copy, an envelope, a label sheet), and with `pages` empty its
    // header / footer slots below.
    Docx::PageLayout layout;
    // Borrowed from the WriteDocument; valid while it lives and its
    // headers are not replaced (a decoration is used right after it is made).
    std::array<const QTextDocument *, Docx::kHeaderFooterSlots> headers{};
    std::array<const QTextDocument *, Docx::kHeaderFooterSlots> footers{};
    bool differentFirstPage = false;
    bool differentOddEven = false;
    // Footnote lines to print at the foot of each 1-based page, already
    // numbered (write_footnotes.h).
    QHash<int, QStringList> footnotes;
    // The endnotes after the body's last line (write_endnotes.h, M77).
    std::shared_ptr<const EndnoteArea> endnotes;
    // Pictures behind and in front of the text are drawn with the body
    // (docx_float.h); the decoration only says whether there are any.
    bool overTextPictures = false;
    // Every page's own frame (M88, WriteDocument with its sections); empty:
    // every page is `layout` on the document's page size.
    QList<PageFrame> pages;

    // The pages to draw: the body's, or as many as the endnotes reach.
    [[nodiscard]] int pageCount(int bodyPages) const;
    // Page `index` (0-based): its own frame, or the uniform one for a
    // document whose canvas page size is `canvas`.
    [[nodiscard]] PageFrame frame(int index, const QSizeF &canvas) const;

    // The header/footer Word would show on 1-based `page`.
    [[nodiscard]] const QTextDocument *headerFor(int page) const;
    [[nodiscard]] const QTextDocument *footerFor(int page) const;
    [[nodiscard]] bool isEmpty() const;
};

struct PageSetup {
    Docx::PageLayout layout;
    bool hasCustomPageSize = false;
    QString header; // plain patterns with {page}/{pages}
    QString footer;
    // Footnote texts keyed by the id their marker carries. The MARKERS
    // travel in the HTML body as ordinary superscript runs; without this
    // meta their notes would not, so a saved document would reopen with
    // numbers pointing at nothing.
    QHash<QString, QString> footnotes;
};

// Reads the qindawrite-* metas from raw HTML; tolerant of absence
// (defaults), of HTML-escaped content and of the M14 three-number page meta.
[[nodiscard]] PageSetup pageSetupFromHtml(const QString &html);

// Injects the metas into an HTML document (before </head>); returns the
// input unchanged when no </head> exists.
[[nodiscard]] QString injectPageSetupMeta(const QString &html, const PageSetup &setup);

// Expands {page} and {pages} tokens for one rendered page.
[[nodiscard]] QString formatPageDecoration(const QString &pattern, int page, int pageCount);

} // namespace QindaOffice::Apps::Write
