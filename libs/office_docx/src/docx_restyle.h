// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Applying a Word document's own paragraph style to paragraphs already in
// it, the way Word does (D-068): the paragraph's direct paragraph formatting
// gives way to the style; its runs keep their direct character formatting
// (from the preserved w:rPr) laid over the new style's; text typed in Write
// (no w:rPr behind it) takes the style's look. The markup becomes
// <w:pStyle w:val="…"/> plus what is not formatting (numbering, a section
// break, revision marks), and the import snapshots are re-taken, so the
// writer emits nothing that would override the document's style in Word.
//
// Display follows the reader (docx_reader_body.cpp): docDefaults → style
// chain → direct; Word's added paragraph spacing folded into the top margin
// of this paragraph and of the next. A table style's layers are not
// re-applied to a restyled cell paragraph (shown approximately; Word, which
// has the table style, shows it exactly).

#include "docx_model.h"
#include "docx_styles.h"
#include "ooxml_theme.h"

#include <QSet>
#include <QString>

class QTextBlock;
class QTextCursor;

namespace QindaOffice::Docx {

class DocumentStyles {
public:
    // The styles of the package `parts` was read from; empty for a document
    // that did not come from Word (Write's own styles apply then).
    explicit DocumentStyles(const WordParts &parts);

    [[nodiscard]] bool isEmpty() const { return m_styles.isEmpty(); }
    // The document's paragraph style standing for one of Write's built-in
    // styles (kWriteStyleIdProperty: 0 Normal, 1 Title, 2–7 Heading 1–6,
    // 8 Quote), found by Word's built-in style name; empty when the
    // document defines none (Write's own look then applies).
    [[nodiscard]] QString styleFor(int writeStyleId) const;
    // Restyles the paragraph at `block`. Not an edit block of its own: the
    // caller groups the edits into one undo step.
    void apply(const QTextBlock &block, const QString &styleId) const;

    // ---- M73 (D-082), docx_restyle_edit.cpp; none is an edit block ----
    [[nodiscard]] const StyleSheet &sheet() const { return m_styles; }
    [[nodiscard]] const Ooxml::Theme &theme() const { return m_theme; }
    // The style a paragraph is in: its w:pStyle, else the Write style it
    // was given (kWriteStyleIdProperty), else the default paragraph style.
    [[nodiscard]] QString paragraphStyleOf(const QTextBlock &block) const;
    // Character style `styleId` on the selection's runs (empty: none, Word's
    // Default Paragraph Font): kRunStyleId (written as w:rStyle) and the
    // look the style gives, the runs' own formatting kept.
    void applyCharacter(const QTextCursor &selection, const QString &styleId) const;
    // Clear Formatting: the selected paragraphs keep their style (and list)
    // and lose their own paragraph formatting; the selected runs keep their
    // character style and lose their own character formatting.
    void clearFormatting(const QTextCursor &selection) const;
    // The styles in `changed` went from `before` to this sheet: every
    // paragraph and run of `document` that uses one (or a style based on
    // one) shows the new look; what it says itself stays, and the import
    // snapshots move along, so the writer still writes only its own
    // markup (D-068 rule 3).
    void restyle(QTextDocument *document, const StyleSheet &before, const QSet<QString> &changed) const;

private:
    StyleSheet m_styles;
    Ooxml::Theme m_theme;
};

} // namespace QindaOffice::Docx
