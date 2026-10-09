// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_restyle.h"

#include "docx_format.h"
#include "docx_properties.h"
#include "docx_text.h"
#include "ooxml_namespaces.h"
#include "ooxml_package.h"

#include <QSet>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextFrame>
#include <QTextList>

namespace QindaOffice::Docx {

using Ooxml::XmlChild;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

XmlElementPtr relatedXml(const Ooxml::Package &package, const char16_t *type)
{
    const QString main = package.mainPart();
    const auto rel = package.firstOfType(main, QString::fromUtf16(type));
    if (!rel) {
        return nullptr;
    }
    const QString part = package.targetPart(main, *rel);
    return part.isEmpty() ? nullptr : package.xml(part);
}

// What a direct w:pPr keeps when a style replaces its formatting: list
// membership, a section break, the paragraph mark's run properties,
// tracked paragraph changes and the other non-formatting markup.
bool keptOnRestyle(const XmlElement &child)
{
    static const QSet<QString> kKept{QStringLiteral("numPr"),     QStringLiteral("sectPr"),
                                     QStringLiteral("rPr"),       QStringLiteral("pPrChange"),
                                     QStringLiteral("cnfStyle"),  QStringLiteral("divId"),
                                     QStringLiteral("framePr"),   QStringLiteral("suppressLineNumbers")};
    return child.ns == kW && kKept.contains(child.name);
}

// Word's added spacing between `block` and the paragraph before it, the way
// the reader folds it (docx_reader_body.cpp, readParagraph); for a block
// that starts its frame, or follows one Write made, just its own "before".
qreal foldedTop(const StyleSheet &styles, const QTextBlock &block)
{
    const QTextBlockFormat format = block.blockFormat();
    const QString styleId = format.stringProperty(kParagraphStyleId);
    const XmlElementPtr direct = Ooxml::parseFragment(format.stringProperty(kParagraphXml));
    const ParagraphSpacing own = spacingOf(mergeProperties(styles.paragraphProperties(styleId).get(), direct.get()).get());
    const QTextBlock previous = block.previous();
    const QTextBlockFormat before = previous.isValid() ? previous.blockFormat() : QTextBlockFormat();
    const bool sameFrame = previous.isValid()
        && QTextCursor(previous).currentFrame() == QTextCursor(block).currentFrame();
    if (!sameFrame || !before.hasProperty(kParagraphSnapshot) || before.boolProperty(kParagraphSynthetic)) {
        return own.before;
    }
    const QString previousStyle = before.stringProperty(kParagraphStyleId);
    const XmlElementPtr previousDirect = Ooxml::parseFragment(before.stringProperty(kParagraphXml));
    const ParagraphSpacing prior =
        spacingOf(mergeProperties(styles.paragraphProperties(previousStyle).get(), previousDirect.get()).get());
    const bool sameStyle = previousStyle == styleId;
    return ((own.contextual && sameStyle) ? 0.0 : own.before) + ((prior.contextual && sameStyle) ? 0.0 : prior.after);
}

// A run's look from the style chain, its character style and its own
// preserved w:rPr — and everything on it that is not formatting.
QTextCharFormat restyledRun(const StyleSheet &styles, const Ooxml::Theme &theme, const XmlElementPtr &paragraphRun,
                            const QTextCharFormat &old)
{
    const QString rStyle = old.stringProperty(kRunStyleId);
    XmlElementPtr effective = mergeProperties(paragraphRun.get(), styles.characterRunProperties(rStyle).get());
    effective = mergeProperties(effective.get(), Ooxml::parseFragment(old.stringProperty(kRunXml)).get());
    QTextCharFormat format = charFormatFromRPr(effective.get(), theme);
    for (const int key : {int(QTextFormat::IsAnchor), int(QTextFormat::AnchorHref), int(QTextFormat::AnchorName),
                          int(QTextFormat::TextToolTip), kRunStyleId, kRunXml, kFieldInstruction,
                          kFootnoteIdProperty, kRevisionInsertProperty, kRevisionDeleteProperty, kRevisionDate}) {
        if (old.hasProperty(key)) {
            format.setProperty(key, old.property(key));
        }
    }
    // A footnote marker keeps Write's raised marker; tracked changes keep
    // their display marks (docx_properties.h), as the reader shows them.
    if (old.hasProperty(kFootnoteIdProperty)) {
        format.setVerticalAlignment(old.verticalAlignment());
    }
    if (old.hasProperty(kRevisionInsertProperty)) {
        format.setFontUnderline(true);
        format.setForeground(QColor::fromRgb(kRevisionInsertColor));
    }
    if (old.hasProperty(kRevisionDeleteProperty)) {
        format.setFontStrikeOut(true);
        format.setForeground(QColor::fromRgb(kRevisionDeleteColor));
    }
    format.setProperty(kRunSnapshot, charSnapshot(format));
    return format;
}

} // namespace

DocumentStyles::DocumentStyles(const WordParts &parts)
{
    if (!parts.source) {
        return;
    }
    if (const XmlElementPtr root = relatedXml(*parts.source, Ooxml::RelType::Styles)) {
        m_styles = StyleSheet::parse(root.get());
    }
    if (const XmlElementPtr theme = relatedXml(*parts.source, Ooxml::RelType::Theme)) {
        m_theme = Ooxml::parseTheme(*theme);
    }
}

QString DocumentStyles::styleFor(int writeStyleId) const
{
    if (m_styles.isEmpty()) {
        return {};
    }
    QString name;
    switch (writeStyleId) {
    case 0:
        return m_styles.defaultParagraphStyle();
    case 1:
        name = QStringLiteral("title");
        break;
    case 8:
        name = QStringLiteral("quote");
        break;
    default:
        if (writeStyleId >= 2 && writeStyleId <= 7) {
            name = QStringLiteral("heading %1").arg(writeStyleId - 1);
        }
    }
    return name.isEmpty() ? QString() : m_styles.idForName(name);
}

void DocumentStyles::apply(const QTextBlock &block, const QString &styleId) const
{
    const Style *style = m_styles.style(styleId);
    const QTextBlockFormat old = block.blockFormat();
    if (style == nullptr || style->type != u"paragraph" || old.boolProperty(kParagraphSynthetic)) {
        return;
    }
    auto pPr = std::make_shared<XmlElement>(kW, QStringLiteral("pPr"));
    pPr->appendElement(kW, QStringLiteral("pStyle")).setAttribute(kW, QStringLiteral("val"), styleId);
    if (const XmlElementPtr direct = Ooxml::parseFragment(old.stringProperty(kParagraphXml))) {
        for (const XmlChild &child : direct->children) {
            if (child.element && keptOnRestyle(*child.element)) {
                pPr->appendElement(child.element->clone());
            }
        }
    }
    const XmlElementPtr effective = mergeProperties(m_styles.paragraphProperties(styleId).get(), pPr.get());
    QTextBlockFormat format = blockFormatFromPPr(effective.get(), m_theme);
    if (const XmlElement *pBdr = effective->child(kW, u"pBdr")) {
        if (const QString borders = paragraphBordersSpec(*pBdr, m_theme); !borders.isEmpty()) {
            format.setProperty(kParagraphBorders, borders);
        }
    }
    format.setProperty(kParagraphStyleId, styleId);
    format.setProperty(kParagraphXml, Ooxml::serializeFragment(*pPr));
    if (const int heading = m_styles.headingLevel(styleId); heading >= 1 && heading <= 6) {
        format.setHeadingLevel(heading);
        format.setProperty(kWriteStyleIdProperty, 1 + heading);
    } else if (const QString name = style->name.toLower(); name == u"title") {
        format.setProperty(kWriteStyleIdProperty, 1);
    } else if (name == u"quote" || name == u"intense quote") {
        format.setProperty(kWriteStyleIdProperty, 8);
    } else {
        format.setProperty(kWriteStyleIdProperty, 0);
    }
    // Not formatting: list membership (with the list level's indents), a
    // section break, a page break that was a w:br, a TOC paragraph.
    for (const int key : {int(QTextFormat::ObjectIndex), kSectionXml, kPageBreakRun, kWriteTocBlockProperty}) {
        if (old.hasProperty(key)) {
            format.setProperty(key, old.property(key));
        }
    }
    if (block.textList() != nullptr) {
        format.setLeftMargin(old.leftMargin());
        format.setTextIndent(old.textIndent());
    }
    QTextFormat::PageBreakFlags breaks = format.pageBreakPolicy() | (ownPageBreaks(old) & QTextFormat::PageBreak_AlwaysAfter);
    if (old.boolProperty(kPageBreakRun)) {
        breaks |= QTextFormat::PageBreak_AlwaysBefore;
    }
    format.setPageBreakPolicy(breaks);
    format.setBottomMargin(0);

    QTextCursor cursor(block);
    cursor.setBlockFormat(format);
    format.setTopMargin(foldedTop(m_styles, block));
    format.setProperty(kParagraphSnapshot, blockSnapshot(format));
    cursor.setBlockFormat(format);
    // The next paragraph's top margin holds this one's "after".
    if (const QTextBlock next = block.next(); next.isValid() && next.blockFormat().hasProperty(kParagraphSnapshot)
        && !next.blockFormat().boolProperty(kParagraphSynthetic)
        && QTextCursor(next).currentFrame() == QTextCursor(block).currentFrame()) {
        QTextBlockFormat following = next.blockFormat();
        following.setTopMargin(foldedTop(m_styles, next));
        following.setProperty(kParagraphSnapshot, blockSnapshot(following));
        QTextCursor(next).setBlockFormat(following);
    }

    // Runs: collected first, since restyling merges and splits fragments.
    const XmlElementPtr paragraphRun = m_styles.paragraphRunProperties(styleId);
    QList<std::pair<std::pair<int, int>, QTextCharFormat>> runs;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        const QTextCharFormat old = fragment.charFormat();
        if (old.isImageFormat() || old.objectType() != QTextFormat::NoObject || old.hasProperty(kInlineXml)) {
            continue; // pictures and preserved objects keep their own format
        }
        runs.append({{fragment.position(), fragment.length()}, restyledRun(m_styles, m_theme, paragraphRun, old)});
    }
    for (const auto &[range, run] : std::as_const(runs)) {
        QTextCursor span(block);
        span.setPosition(range.first);
        span.setPosition(range.first + range.second, QTextCursor::KeepAnchor);
        span.setCharFormat(run);
    }
    const XmlElement *markRPr = pPr->child(kW, u"rPr");
    QTextCharFormat mark = charFormatFromRPr(mergeProperties(paragraphRun.get(), markRPr).get(), m_theme);
    QTextCursor markCursor(block);
    setBlockCharFormatKeepingCell(markCursor, mark);
}

} // namespace QindaOffice::Docx
