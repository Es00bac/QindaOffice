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

#include "docx_mailmerge.h"
#include "docx_bibliography.h"
#include "docx_notes.h"
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
#include <map>
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
// pictures); PAGE / NUMPAGES / SECTIONPAGES fields are runs carrying
// kFieldInstruction. `sourcePart` names the part it was read from: an
// UNMODIFIED header is written back by copying that part, byte for byte.
struct HeaderFooter {
    std::unique_ptr<QTextDocument> document;
    QString sourcePart;
    // M88 (D-096): the section's own even while empty — Link to Previous
    // was turned off (Word writes a reference to an empty part).
    bool unlinked = false;

    [[nodiscard]] bool isEmpty() const { return !document || document->isEmpty(); }
    // Whether the section this belongs to has the slot itself (Word: a
    // w:headerReference / w:footerReference of that type); a slot it does
    // not define is Word's "Link to Previous": the previous section's.
    // An empty story nobody typed into (opened and closed again) defines
    // nothing.
    [[nodiscard]] bool defines() const
    {
        return document && (!document->isEmpty() || !sourcePart.isEmpty() || unlinked);
    }
};

// ---- Word sections (M88, D-096; docx_sections.h) -------------------------------------
// How a section starts after the one before it (w:type).
enum class SectionStart { NextPage = 0, Continuous = 1, EvenPage = 2, OddPage = 3, NextColumn = 4 };

// w:pgNumType: where the section's page numbers start and how they look.
struct PageNumbering {
    int start = -1;           // w:start; -1 continues from the previous section
    QString format;           // w:fmt as Word names it; empty is decimal
    int chapterStyle = 0;     // w:chapStyle: chapter numbers from Heading N; 0 none
    QString chapterSeparator; // w:chapSep: hyphen (Word's default), period, colon, emDash, enDash
    bool operator==(const PageNumbering &) const = default;
};

// w:cols: kept and written; QindaWrite lays a section out in one column.
struct SectionColumns {
    int count = 1;          // w:num
    qreal space = 48;       // px between columns (w:space; Word's 0.5 inch)
    bool separator = false; // w:sep: a line between them
    bool operator==(const SectionColumns &) const = default;
};

// What a section says besides its page and its first-page flag.
struct SectionFlow {
    SectionStart start = SectionStart::NextPage;
    PageNumbering numbering;
    SectionColumns columns;
    QString verticalAlign; // w:vAlign: empty (top), center, both, bottom
    bool operator==(const SectionFlow &) const = default;
};

// A section's own headers and footers (WordParts::sectionStories).
struct SectionStories {
    std::array<HeaderFooter, kHeaderFooterSlots> headers;
    std::array<HeaderFooter, kHeaderFooterSlots> footers;
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

// A Word content control (w:sdt, docx_controls.h): a form field (check box,
// drop-down, date, plain text) or a rich-text, gallery or group wrapper. Its
// content stays ordinary text; the runs, paragraphs and tables inside carry
// its key (kContentControls / kBlockControls). Written back from its own
// w:sdtPr, patched for what Write changed: the check box's state, the date,
// the chosen item, the placeholder and data-binding flags.
struct ContentControl {
    enum class Kind { RichText, PlainText, CheckBox, DropDown, ComboBox, Date, Picture, Gallery, Group, Other };
    Kind kind = Kind::RichText;
    bool block = false;          // wraps paragraphs and tables, not runs
    QString propertiesXml;       // w:sdtPr as read
    QString endPropertiesXml;    // w:sdtEndPr as read, or empty
    QString sourceText;          // its content's text as read (controlTexts())
    bool showingPlaceholder = false;
    QString title;               // w:alias
    QString tag;
    // Check box (w14:checkbox): the symbol and font of each state.
    QChar checkedSymbol = QChar(0x2612);
    QChar uncheckedSymbol = QChar(0x2610);
    QString checkedFont;
    QString uncheckedFont;
    // Drop-down list / combo box: each item's shown text and value.
    QStringList itemTexts;
    QStringList itemValues;
    QString dateFormat; // Word's w:dateFormat ("M/d/yyyy")
};

struct WordParts {
    // The CLOSING section (w:body's own w:sectPr, M88): its page, headers
    // and footers, title page and the rest of what it says. Every other
    // section ends at a paragraph that carries its settings
    // (kSectionSettings) and key (kSectionId) and keeps its headers and
    // footers in `sectionStories` under that key (docx_sections.h).
    PageLayout layout;
    std::array<HeaderFooter, kHeaderFooterSlots> headers;
    std::array<HeaderFooter, kHeaderFooterSlots> footers;
    bool differentFirstPage = false;
    SectionFlow closingFlow;
    std::map<QString, SectionStories> sectionStories;
    // Word's "Different Odd & Even Pages" is the document's
    // (w:evenAndOddHeaders in settings.xml), not a section's.
    bool differentOddEven = false;

    // Footnote texts keyed by the id on the marker run (QindaWrite's model),
    // plus, for notes read from a .docx, the preserved w:footnote and the
    // text it had: a note whose text is unchanged is written back verbatim.
    QHash<QString, QString> footnotes;
    QHash<QString, QString> footnoteXml;
    QHash<QString, QString> footnoteImportedText;
    // Endnotes, the same way (M77, D-086): texts by the id on the reference
    // mark (kEndnoteIdProperty), each read note's own w:endnote and text.
    // While the notes the body references are exactly the ones read, with
    // their text unchanged, endnotes.xml is copied byte for byte.
    QHash<QString, QString> endnotes;
    QHash<QString, QString> endnoteXml;
    QHash<QString, QString> endnoteImportedText;
    // Footnote and endnote options (Word's Footnote and Endnote dialog) and
    // what was read: written to settings.xml and the sections when changed.
    NoteOptions footnoteOptions;
    NoteOptions endnoteOptions;
    NoteOptions importedFootnoteOptions;
    NoteOptions importedEndnoteOptions;

    // Mail merge (M77, D-086, docx_mailmerge.h): the merge settings and
    // what the source said; the source's own w:mailMerge is kept while they
    // are equal.
    Bibliography bibliography; // document-local sources and preserved native b:Sources
    MailMergeSettings mailMerge;
    MailMergeSettings importedMailMerge;

    // Comments in reading order; the set read from the source is copied
    // back unchanged unless one was added, edited, resolved or removed.
    QList<Comment> comments;
    QStringList sourceCommentIds;

    // Content controls of every story (body, headers, footers) by key.
    QHash<QString, ContentControl> contentControls;

    // Original bytes of every picture, keyed by its resource name
    // ("qwmedia:image3.png"); the QTextDocument holds decoded images.
    QHash<QString, QByteArray> media;

    // The package this document was read from, or null for a new one, and
    // what the reader found there: the body's closing w:sectPr and the
    // layout, first-page flag and flow it produced. The writer keeps the
    // source markup while the closing section still says these (D-068
    // rule 3).
    std::shared_ptr<const Ooxml::Package> source;
    QString sourceSectionXml;
    PageLayout importedLayout;
    bool importedDifferentFirstPage = false;
    SectionFlow importedClosingFlow;
    Ooxml::CoreProperties properties;
    qreal defaultTabStop = 48.0; // px
    // The document's styles part (w:styles) as QindaWrite's style commands
    // left it (M73, D-082: New / Modify / Update / Delete Style, a built-in
    // added on first use); null while the styles are the source package's
    // own — or, for a new document, Write's built-ins the writer adds. An
    // immutable snapshot: an edit swaps in a new root (its undo step swaps
    // back), and the writer, the reader-side restyling and the Styles pane
    // read it (docx_style_edit.h stylesRoot()).
    Ooxml::XmlElementPtr styles;

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

// A copy of `parts` for another document made from this one (a merged
// document, M77): the same layout, styles, notes, comments, controls,
// pictures and source package, every section's headers and footers cloned
// (docx_parts.cpp). Merge settings are left behind: a merged document is
// an ordinary one.
[[nodiscard]] WordParts cloneWordParts(const WordParts &parts);
// Gives section `key` a copy of the closing section's headers and footers
// (a merged document's record sections show the main document's, M88).
void shareStories(WordParts &parts, const QString &key);
// The w:sectPr that ends a section of this document: the source's own
// closing one, or one made from the closing section's settings (a merged
// record's section break, M77).
[[nodiscard]] QString sectionPropertiesXml(const WordParts &parts);

} // namespace QindaOffice::Docx
