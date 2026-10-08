// SPDX-License-Identifier: GPL-3.0-or-later
#include "style_catalog.h"

#include "docx_properties.h"
#include "docx_text.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

namespace QindaOffice::Apps::Write {

// AGENT-CONTRACT (docx_properties.h): the .docx mapping reads and writes
// Write's own style, TOC, footnote and revision properties.
static_assert(kStyleIdProperty == Docx::kWriteStyleIdProperty);

namespace {

ParagraphStyle make(int id, const QString &name, int headingLevel, int pointSize, bool bold,
                    bool italic = false)
{
    ParagraphStyle style;
    style.id = id;
    style.name = name;
    // Explicit resets: Qt blocks inherit the previous block's format on
    // insertBlock, and mergeBlockFormat keeps anything we do not set -
    // each style must fully own every property it cares about.
    style.blockFormat.setAlignment(Qt::AlignLeft);
    style.blockFormat.setHeadingLevel(0);
    style.blockFormat.setNonBreakableLines(false);
    style.blockFormat.setLeftMargin(0);
    // Only Heading 1 / Title force a page break — applying "Heading 3"
    // mid-flow previously started a new page at EVERY heading level.
    style.blockFormat.setPageBreakPolicy(QTextFormat::PageBreak_Auto);
    if (headingLevel > 0) {
        style.blockFormat.setHeadingLevel(headingLevel);
        if (headingLevel == 1) {
            style.blockFormat.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
        }
    }
    style.charFormat.setFontPointSize(pointSize);
    style.charFormat.setFontWeight(bold ? QFont::Bold : QFont::Normal);
    style.charFormat.setFontItalic(italic);
    return style;
}

} // namespace

StyleCatalog::StyleCatalog()
{
    m_styles.append(make(Normal, QStringLiteral("Normal"), 0, 11, false));
    ParagraphStyle title = make(Title, QStringLiteral("Title"), 1, 26, true);
    // The resolver identifies Title as a centred heading 1 (plan 7.1), so
    // the built-in Title carries the centre alignment.
    title.blockFormat.setAlignment(Qt::AlignHCenter);
    m_styles.append(title);
    m_styles.append(make(Heading1, QStringLiteral("Heading 1"), 1, 20, true));
    m_styles.append(make(Heading2, QStringLiteral("Heading 2"), 2, 16, true));
    m_styles.append(make(Heading3, QStringLiteral("Heading 3"), 3, 14, true));
    m_styles.append(make(Heading4, QStringLiteral("Heading 4"), 4, 12, true));
    m_styles.append(make(Heading5, QStringLiteral("Heading 5"), 5, 11, true));
    m_styles.append(make(Heading6, QStringLiteral("Heading 6"), 6, 11, true, true));
    ParagraphStyle quote = make(Quote, QStringLiteral("Quote"), 0, 11, false, true);
    quote.blockFormat.setLeftMargin(30);
    m_styles.append(quote);
    ParagraphStyle code = make(Code, QStringLiteral("Code"), 0, 10, false);
    code.charFormat.setFontFamily(QStringLiteral("monospace"));
    code.blockFormat.setNonBreakableLines(true);
    m_styles.append(code);
}

QStringList StyleCatalog::names() const
{
    QStringList out;
    out.reserve(m_styles.size());
    for (const ParagraphStyle &style : m_styles) {
        out.append(style.name);
    }
    return out;
}

void StyleCatalog::apply(QTextCursor cursor, int id) const
{
    const ParagraphStyle &style = m_styles.at(qBound(0, id, int(m_styles.size()) - 1));
    // SET (not merge) the block format: each style fully owns the properties
    // it cares about, and mergeBlockFormat kept the block's stale page-break
    // policy / heading level (Heading -> Normal kept breaking pages).
    QTextBlockFormat block = style.blockFormat;
    block.setProperty(kStyleIdProperty, style.id);
    // AGENT-GUARD: never force a page break BEFORE the first block. Heading 1
    // starts a chapter on a fresh page, but a document whose first paragraph
    // is a Heading 1 then opens with a blank page 1 — and every page number
    // in it, including the table of contents', is one out.
    if (cursor.block().position() == 0) {
        block.setPageBreakPolicy(QTextFormat::PageBreak_Auto);
    }
    cursor.setBlockFormat(block);
    // A style that does not set a family RESETS it instead of keeping the
    // previous style's (Code -> Normal left monospace behind). Probe with
    // hasProperty: reading fontFamily() asserts when the property is unset
    // (Qt 6.11 asserts on the empty string-list property).
    QTextCharFormat charFormat = style.charFormat;
    if (!charFormat.hasProperty(QTextFormat::FontFamily)) {
        charFormat.setFontFamily(QString());
    }
    // Not setBlockCharFormat(): in a table cell's first paragraph that would
    // reset the cell's merge, shading and borders (docx_text.h).
    Docx::setBlockCharFormatKeepingCell(cursor, charFormat);
    if (cursor.hasSelection()) {
        cursor.mergeCharFormat(charFormat);
    }
}

int classifyBlockStyle(const QTextBlock &block)
{
    const QTextBlockFormat format = block.blockFormat();
    const int level = format.headingLevel();
    if (level == 1 && format.alignment() == Qt::AlignHCenter) {
        return StyleCatalog::Title;
    }
    if (level >= 1 && level <= 6) {
        return StyleCatalog::Heading1 + level - 1;
    }
    if (format.nonBreakableLines()) {
        return StyleCatalog::Code;
    }
    const QFont font = block.charFormat().font();
    if (font.family().contains(QLatin1String("mono"), Qt::CaseInsensitive)
        || font.fixedPitch()) {
        return StyleCatalog::Code;
    }
    if (format.leftMargin() >= 30 && font.italic()) {
        return StyleCatalog::Quote;
    }
    return StyleCatalog::Normal;
}

} // namespace QindaOffice::Apps::Write
