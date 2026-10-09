// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A Word document's styles as the user edits them (M73, D-082): the Styles
// pane's list, Word's built-ins added on first use, New Style, Modify
// Style, Update Style to Match Selection and Delete Style on the styles
// part, and what those do to the text — every paragraph and run of a
// changed style re-derived the way Word shows it, character styles applied
// as w:rStyle, Clear Formatting back to the style.
//
// The styles part is WordParts::styles (docx_model.h): an immutable root,
// replaced whole by each edit so the caller's undo step can swap it back.
// Styles are written as Word writes them: w:style with w:name, w:basedOn,
// w:next, w:link, w:qFormat, w:uiPriority and its own w:pPr / w:rPr; a
// paragraph that uses one says only <w:pStyle/>, a run <w:rStyle/>.
//
// Threading: GUI thread.

#include "docx_model.h"
#include "docx_styles.h"
#include "ooxml_xml.h"

#include <QFont>
#include <QList>
#include <QSet>
#include <QString>
#include <QTextBlockFormat>
#include <QTextCharFormat>

class QTextBlock;
class QTextDocument;

namespace QindaOffice::Docx {

// One row of the Styles pane.
struct StyleEntry {
    QString id;
    QString name;    // as Word shows it ("Heading 1", not "heading 1")
    QString type;    // paragraph, character, linked, table
    QString basedOn;
    QString next;
    QString link;    // the linked style of a linked pair
    int priority = 99; // w:uiPriority: the gallery's order
    bool quick = false;      // w:qFormat: in the gallery
    bool hidden = false;     // w:semiHidden / w:hidden
    bool inDocument = true;  // false: one of Word's built-ins the document lacks
    bool inUse = false;      // a paragraph, run or table of the body uses it
};

// The styles part as it stands: WordParts::styles, else the source
// package's, else the one a new document is written with (Write's base
// styles over `defaultFont`). Never null.
[[nodiscard]] Ooxml::XmlElementPtr stylesRoot(const WordParts &parts, const QFont &defaultFont);
// The document's styles (numbering styles and the character half of a
// linked pair left out) followed by Word's built-ins it lacks, each in
// Word's order (priority, then name); `inUse` from `body`.
[[nodiscard]] QList<StyleEntry> styleEntries(const Ooxml::XmlElement &root, const QTextDocument &body);
// The paragraph and character styles of the gallery: quick ones, the
// document's then the built-ins it lacks, by priority.
[[nodiscard]] QList<StyleEntry> quickStyles(const Ooxml::XmlElement &root, const QTextDocument &body);
[[nodiscard]] QString displayStyleName(const QString &wordName);
// The style a paragraph says it is in: its w:pStyle, else the Word id of
// the Write style it was given (kWriteStyleIdProperty), else "Normal".
[[nodiscard]] QString paragraphStyleIdOf(const QTextBlockFormat &format);

// Word's definition of a built-in style the document lacks, adapted to it
// (based on its own default paragraph style); null for an unknown id.
[[nodiscard]] Ooxml::XmlElementPtr builtInStyleFor(const Ooxml::XmlElement &root, const QString &styleId);
// `root` with `style` added, or replacing the style with its w:styleId.
[[nodiscard]] Ooxml::XmlElementPtr withStyle(const Ooxml::XmlElement &root, const Ooxml::XmlElementPtr &style);
// `root` without style `id`: styles based on it are based on its parent,
// those it was "next" for or linked to forget it.
[[nodiscard]] Ooxml::XmlElementPtr withoutStyle(const Ooxml::XmlElement &root, const QString &id);
// A style id for `name` no style of `root` has ("Note 1" → "Note1").
[[nodiscard]] QString newStyleId(const Ooxml::XmlElement &root, const QString &name);
// A w:style: `type` paragraph or character; `pPr` / `rPr` may be null.
[[nodiscard]] Ooxml::XmlElementPtr makeStyle(const QString &type, const QString &id, const QString &name,
                                             const QString &basedOn, const QString &next, bool quick,
                                             const Ooxml::XmlElementPtr &pPr, const Ooxml::XmlElementPtr &rPr);
// `style` (a w:style) with its name, base, next style, gallery flag and
// own w:pPr / w:rPr replaced; all else it says (w:link, w:uiPriority,
// w:rsid, a table style's layers…) kept, children in schema order.
[[nodiscard]] Ooxml::XmlElementPtr editedStyle(const Ooxml::XmlElement &style, const QString &name, const QString &basedOn,
                                               const QString &next, bool quick, const Ooxml::XmlElementPtr &pPr,
                                               const Ooxml::XmlElementPtr &rPr);
// The w:style of `root` with that id, or null.
[[nodiscard]] Ooxml::XmlElementPtr styleElement(const Ooxml::XmlElement &root, const QString &id);
// The styles a change to `id` reaches: it and every style based on it.
[[nodiscard]] QSet<QString> derivedStyles(const StyleSheet &styles, const QString &id);

// A paragraph's and a run's own formatting as Word markup — what the
// writer writes for it, without what is not formatting (its style,
// numbering, section break, mark, tracked changes). New Style and Update
// Style to Match Selection make a style of it.
// `keepNumbering`: a style's own w:numPr stays (Modify Style); a
// paragraph's list is not a style's (New Style).
[[nodiscard]] Ooxml::XmlElementPtr paragraphFormatting(const QTextBlock &block, const WordParts &parts,
                                                       bool keepNumbering = false);
[[nodiscard]] Ooxml::XmlElementPtr runFormatting(const QTextCharFormat &format, const QTextDocument &document,
                                                 const WordParts &parts);

} // namespace QindaOffice::Docx
