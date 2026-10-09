// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The .fodp writer's automatic-style registry: every graphic, paragraph,
// text and drawing-page style a deck needs, deduplicated by content and
// named the way LibreOffice names its own (gr1, P1, T1, dp1). The writer
// registers styles while it walks the deck once (collect), writes
// office:automatic-styles, then walks again emitting the body with the
// names the registry handed out — so every draw:style-name /
// text:style-name in the body resolves.
//
// AGENT-GUARD: formatting lives in STYLES, never as fo: attributes on
// text:span / text:p (invalid ODF, and LibreOffice drops it). The reader
// (fodp_read_styles.cpp) resolves the same properties back, including
// style:parent-style-name chains, so files LibreOffice wrote read too.

#include "presentation.h"

#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

class QXmlStreamWriter;

namespace QindaOffice::Show::Fodp {

// One property element of a style: element name ("style:graphic-
// properties") and its attributes in writing order.
struct StyleProperties {
    QString element;
    QList<QPair<QString, QString>> attributes;
};

class StyleRegistry {
public:
    // Name of the graphic style that draws `shape` (fill, stroke, markers,
    // text insets). Registers it on first use.
    QString graphicStyle(const Shape &shape);
    // Paragraph style for alignment etc.; empty when the paragraph needs
    // none (left-aligned default).
    QString paragraphStyle(const TextParagraph &paragraph);
    // Text style for a run's character formatting.
    QString textStyle(const TextRun &run);
    // Drawing-page style carrying a slide's transition.
    QString pageStyle(const Slide &slide);
    // Drawing-page style carrying a master page's background.
    QString masterPageStyle(const QColor &background);

    // office:automatic-styles content (page layout first, then styles in
    // registration order) and the list style bullets use.
    void writeAutomaticStyles(QXmlStreamWriter &xml, const QSizeF &slideSize) const;

    static constexpr char16_t kListStyle[] = u"L1";
    static constexpr char16_t kPageLayout[] = u"PM1";
    static constexpr char16_t kArrowMarker[] = u"Arrow";

private:
    struct Entry {
        QString name;
        QString family;
        QList<StyleProperties> properties;
    };
    QString registerStyle(const QString &prefix, const QString &family, const QList<StyleProperties> &properties);

    QList<Entry> m_entries;
    QHash<QString, QString> m_byKey; // family + serialised properties → name
    QHash<QString, int> m_counters;  // prefix → last number used
};

// Writes a shape's paragraphs as text:p / text:list (bullets nest by level)
// with the registry's style names. Shared by text boxes and custom shapes.
void writeParagraphs(QXmlStreamWriter &xml, StyleRegistry &styles, const QList<TextParagraph> &paragraphs);

// Character data with ODF's white-space encoding: runs of spaces become
// text:s, tabs text:tab, line breaks (U+2028, '\n') text:line-break, so a
// reader collapsing white space per ODF §6.1.2 gets the text back exactly.
// `previousSpace` carries "the last character written was white space"
// across the runs of one paragraph (start it true: leading spaces drop).
void writeOdfText(QXmlStreamWriter &xml, const QString &text, bool *previousSpace, bool endsParagraph);

// "#rrggbb" for an opaque colour; the alpha travels separately.
[[nodiscard]] QString colorName(const QColor &color);
// ODF percentage spelling ("50%").
[[nodiscard]] QString percent(qreal value);

} // namespace QindaOffice::Show::Fodp
