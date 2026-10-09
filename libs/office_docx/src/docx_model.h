// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Everything a word-processing document carries besides its body text.
//
// The body is a QTextDocument the caller owns (QindaWrite's WriteDocument).
// WordParts holds the rest: page layout, rich headers and footers, footnote
// texts, the original bytes of every picture, and the package the document
// was opened from (so a save can carry over what the suite does not model,
// D-068).
//
// Ownership: WordParts owns its header/footer QTextDocuments and is
// move-only. Threading: GUI thread, like the body document.

#include "ooxml_package.h"
#include "ooxml_properties.h"

#include <QDateTime>
#include <QHash>
#include <QMarginsF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTextDocument>

#include <array>
#include <memory>

namespace QindaOffice::Docx {

// Page geometry in document units (96-dpi px). Defaults: US Letter with
// Word's 1-inch margins and 0.5-inch header/footer distances.
struct PageLayout {
    QSizeF pageSize{816.0, 1056.0};
    QMarginsF margins{96.0, 96.0, 96.0, 96.0}; // left, top, right, bottom
    qreal headerDistance = 48.0; // page top edge → header top
    qreal footerDistance = 48.0; // page bottom edge → footer bottom
    qreal gutter = 0.0;

    [[nodiscard]] bool landscape() const { return pageSize.width() > pageSize.height(); }
    [[nodiscard]] qreal contentWidth() const
    {
        return pageSize.width() - margins.left() - margins.right() - gutter;
    }
    bool operator==(const PageLayout &) const = default;
};

enum class HeaderFooterSlot { Default = 0, First = 1, Even = 2 };
inline constexpr int kHeaderFooterSlots = 3;

// One header or footer. `document` is rich content (paragraphs, tables,
// pictures); PAGE / NUMPAGES fields are runs carrying kFieldInstruction.
// `sourcePart` names the part it was read from: an UNMODIFIED header is
// written back by copying that part, byte for byte.
struct HeaderFooter {
    std::unique_ptr<QTextDocument> document;
    QString sourcePart;

    [[nodiscard]] bool isEmpty() const { return !document || document->isEmpty(); }
};

// A Word comment (docx_comments.h). In the text, its range is the runs whose
// kCommentIds name it and its reference mark the character carrying
// kCommentReference; the comment itself lives here.
struct Comment {
    QString id;        // w:id, decimal
    QString author;
    QString initials;
    QDateTime date;
    QString text;      // its paragraphs, joined with '\n'
    QString parentId;  // the comment this one replies to, or empty
    bool done = false; // resolved
    // As read: the w:comment markup and the values above at that time; an
    // unchanged comment is written back from its markup.
    QString sourceXml;
    QString sourceText;
    QString sourceParentId;
    bool sourceDone = false;
    QString paraId; // w14:paraId of its last paragraph (commentsExtended's key)

    [[nodiscard]] bool isUnchanged() const
    {
        return !sourceXml.isEmpty() && text == sourceText && parentId == sourceParentId && done == sourceDone;
    }
};

struct WordParts {
    PageLayout layout;
    std::array<HeaderFooter, kHeaderFooterSlots> headers;
    std::array<HeaderFooter, kHeaderFooterSlots> footers;
    bool differentFirstPage = false;
    bool differentOddEven = false;

    // Footnote texts keyed by the id on the marker run (QindaWrite's model),
    // plus, for notes read from a .docx, the preserved w:footnote and the
    // text it had: a note whose text is unchanged is written back verbatim.
    QHash<QString, QString> footnotes;
    QHash<QString, QString> footnoteXml;
    QHash<QString, QString> footnoteImportedText;

    // Comments in reading order; the set read from the source is copied
    // back unchanged unless one was added, edited, resolved or removed.
    QList<Comment> comments;
    QStringList sourceCommentIds;

    // Original bytes of every picture, keyed by its resource name
    // ("qwmedia:image3.png"); the QTextDocument holds decoded images.
    QHash<QString, QByteArray> media;

    // The package this document was read from, or null for a new one, and
    // what the reader found there: the body's closing w:sectPr and the
    // layout and first-page flag it produced. The writer keeps the source
    // markup while layout/differentFirstPage still equal these (D-068 rule 3).
    std::shared_ptr<const Ooxml::Package> source;
    QString sourceSectionXml;
    PageLayout importedLayout;
    bool importedDifferentFirstPage = false;
    Ooxml::CoreProperties properties;
    qreal defaultTabStop = 48.0; // px

    // Human-readable notes about what was approximated on open
    // ("2 text boxes shown as boxes; kept unchanged on save").
    QStringList warnings;

    WordParts() = default;
    WordParts(WordParts &&) = default;
    WordParts &operator=(WordParts &&) = default;
    WordParts(const WordParts &) = delete;
    WordParts &operator=(const WordParts &) = delete;

    [[nodiscard]] HeaderFooter &header(HeaderFooterSlot slot) { return headers[size_t(slot)]; }
    [[nodiscard]] HeaderFooter &footer(HeaderFooterSlot slot) { return footers[size_t(slot)]; }
    [[nodiscard]] const HeaderFooter &header(HeaderFooterSlot slot) const { return headers[size_t(slot)]; }
    [[nodiscard]] const HeaderFooter &footer(HeaderFooterSlot slot) const { return footers[size_t(slot)]; }
};

} // namespace QindaOffice::Docx
