// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Word sections (M88, D-096): what a w:sectPr says, where sections are in
// the body, and the headers and footers each one has or links to.
//
// The model. A body is a run of sections. Every section but the last ends
// at a top-level paragraph whose block format carries the section's
// SectionSettings (kSectionSettings), what the reader mapped
// (kSectionSnapshot), its key (kSectionId) and the source markup
// (kSectionXml); the last, CLOSING section is WordParts' own (layout,
// differentFirstPage, closingFlow, headers, footers). A section's headers
// and footers are WordParts::sectionStories[key] (the closing section's are
// WordParts::headers / footers); a slot a section does not define
// (HeaderFooter::defines) is Word's "Link to Previous": the nearest earlier
// section's that does.
//
// Reading (docx_reader.cpp) marks every paragraph-level w:sectPr with
// markSectionEnd(); writing (docx_writer.cpp) emits each section's w:sectPr
// from its markup where its settings equal the snapshot, patched where they
// differ (writeSectionSettings), from scratch for a section made without
// markup. Display (QindaWrite's write_sections.h) lays every section out on
// its own page size; office_docx only knows the model.
//
// Threading: GUI thread, like the documents. Every function is pure over
// its arguments.

#include "docx_model.h"
#include "ooxml_xml.h"

#include <QList>
#include <QTextBlock>

#include <functional>
#include <QTextFormat>
#include <QVariantMap>

namespace QindaOffice::Docx {

// Everything a section's w:sectPr says that QindaWrite models.
struct SectionSettings {
    PageLayout layout;
    bool titlePage = false; // w:titlePg: Different First Page
    SectionFlow flow;
    bool operator==(const SectionSettings &) const = default;

    [[nodiscard]] QVariantMap toMap() const;
    [[nodiscard]] static SectionSettings fromMap(const QVariantMap &map);
};

// ---- w:sectPr ------------------------------------------------------------------------
// The settings a w:sectPr says (Word's defaults where it is silent; null
// gives the defaults: US Letter, 1-inch margins, next page, one column).
[[nodiscard]] SectionSettings readSectionSettings(const Ooxml::XmlElement *sectPr);
// Patches `sectPr` to say `now`, touching only what differs from `was`
// (the settings its markup said): an unchanged w:pgSz keeps its w:code, an
// unchanged w:cols its w:col list, everything not modelled (w:lnNumType,
// w:docGrid, w:pgBorders, printer settings) stays as it was.
void writeSectionSettings(Ooxml::XmlElement &sectPr, const SectionSettings &now, const SectionSettings &was);
// A complete w:sectPr for a section without markup (children unsorted:
// the writer sorts them into schema order).
[[nodiscard]] Ooxml::XmlElementPtr newSectionElement(const SectionSettings &settings);

// ---- sections in the body ------------------------------------------------------------
// Whether a paragraph format carries a section break (settings, or the
// markup of one made before M88).
[[nodiscard]] bool carriesSectionBreak(const QTextBlockFormat &format);
// The settings a paragraph's section break says.
[[nodiscard]] SectionSettings sectionSettings(const QTextBlockFormat &format);
// Makes `format` end a section: `sectPrXml` (a self-contained fragment) is
// its markup, its settings and snapshot are what the markup says.
void markSectionEnd(QTextBlockFormat &format, const QString &key, const QString &sectPrXml);
// Takes a section break off a paragraph (the section joins the next one).
void clearSectionEnd(QTextBlockFormat &format);
// The w:sectPr a paragraph's section break would be written with now (its
// markup patched to its settings, or new): what a section split from it
// starts from.
[[nodiscard]] QString currentSectionXml(const QTextBlockFormat &format);
// A fresh key for a section made in QindaWrite.
[[nodiscard]] QString newSectionKey();

// The top-level paragraphs that end sections, in order: of consecutive
// paragraphs with one key the last; the document's last paragraph never
// (the closing section ends there).
[[nodiscard]] QList<QTextBlock> sectionEnds(const QTextDocument &body);

// One section of a document: its end paragraph (invalid for the closing
// section), key (empty for the closing section) and settings.
struct SectionRef {
    QTextBlock end;
    QString key;
    SectionSettings settings;
    [[nodiscard]] bool closing() const { return !end.isValid(); }
};
// Every section of `body` in order, the closing one last (always at least one).
[[nodiscard]] QList<SectionRef> documentSections(const QTextDocument &body, const WordParts &parts);
// The index of the section `position` lies in.
[[nodiscard]] int sectionIndexAt(const QList<SectionRef> &sections, int position);

// The closing section's settings, assembled from WordParts, and back.
[[nodiscard]] SectionSettings closingSettings(const WordParts &parts);
[[nodiscard]] SectionSettings importedClosingSettings(const WordParts &parts);
void setClosingSettings(WordParts &parts, const SectionSettings &settings);

// ---- headers and footers -------------------------------------------------------------
// The slot a section keeps itself (the closing section's for an empty key);
// with `create`, the map entry is made.
[[nodiscard]] HeaderFooter *sectionStory(WordParts &parts, const QString &key, bool footer, HeaderFooterSlot slot,
                                         bool create = false);
[[nodiscard]] const HeaderFooter *sectionStory(const WordParts &parts, const QString &key, bool footer,
                                               HeaderFooterSlot slot);
// The index of the section whose story section `index` shows in `slot`:
// itself when it defines the slot, else the nearest earlier one that does;
// -1 when none does (Word shows nothing).
[[nodiscard]] int storyOwner(const WordParts &parts, const QList<SectionRef> &sections, int index, bool footer,
                             HeaderFooterSlot slot);

// Every header and footer of every section that has a document.
void forEachStory(WordParts &parts, const std::function<void(HeaderFooter &)> &fn);
void forEachStory(const WordParts &parts, const std::function<void(const HeaderFooter &)> &fn);

// ---- page numbers --------------------------------------------------------------------
// `number` in a w:pgNumType/@w:fmt (decimal, lowerRoman, upperRoman,
// lowerLetter, upperLetter, decimalZero, numberInDash; others as decimal).
[[nodiscard]] QString formatPageNumber(int number, const QString &format);
// A PAGE field's own number format switch (\* roman, \* ALPHABETIC, …) as a
// w:fmt, or empty when the field has none (the section's applies).
[[nodiscard]] QString fieldNumberFormat(const QString &instruction);

// ---- the display-only right margin (kSectionPad) ---------------------------------------
// A paragraph's (or table's) right margin without the section layout's pad.
[[nodiscard]] qreal ownRightMargin(const QTextFormat &format);
// Sets the paragraph's own right margin, keeping a section pad on top.
void setOwnRightMargin(QTextFormat &format, qreal margin);
// Sets the pad on top of the own right margin (0 removes it).
void setSectionPad(QTextFormat &format, qreal pad);

} // namespace QindaOffice::Docx
