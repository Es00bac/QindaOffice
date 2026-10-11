// SPDX-License-Identifier: GPL-3.0-or-later
// The import snapshots (D-068 rule 3) and the small format queries the
// reader, the writer and QindaWrite share; split from docx_format.cpp
// (the WordprocessingML ⇄ Qt property mapping).

#include "docx_format.h"

#include "docx_properties.h"
#include "docx_sections.h"

#include <QTextOption>

namespace QindaOffice::Docx {

namespace {

QString colorKey(const QBrush &brush, bool has)
{
    return has && brush.style() != Qt::NoBrush ? brush.color().name(QColor::HexArgb) : QString();
}

} // namespace

QVariantMap blockSnapshot(const QTextBlockFormat &f)
{
    QStringList tabs;
    for (const QTextOption::Tab &t : f.tabPositions()) {
        tabs.append(QStringLiteral("%1:%2").arg(t.position, 0, 'f', 2).arg(int(t.type)));
    }
    return {
        {QStringLiteral("align"), int(f.alignment() & Qt::AlignHorizontal_Mask)},
        {QStringLiteral("left"), qRound(f.leftMargin() * 100)},
        {QStringLiteral("right"), qRound(ownRightMargin(f) * 100)}, // M88: not the section layout's pad
        {QStringLiteral("indent"), qRound(f.textIndent() * 100)},
        {QStringLiteral("top"), qRound(f.topMargin() * 100)},
        {QStringLiteral("bottom"), qRound(f.bottomMargin() * 100)},
        {QStringLiteral("lhType"), ownLineSpacing(f).type},
        {QStringLiteral("lh"), qRound(ownLineSpacing(f).value * 100)},
        {QStringLiteral("bg"), colorKey(f.background(), f.hasProperty(QTextFormat::BackgroundBrush))},
        {QStringLiteral("break"), int(ownPageBreaks(f))},
        {QStringLiteral("keep"), f.nonBreakableLines()},
        {QStringLiteral("keepNext"), f.boolProperty(kKeepWithNext)},
        {QStringLiteral("heading"), f.headingLevel()},
        {QStringLiteral("tabs"), tabs.join(QLatin1Char(';'))},
        {QStringLiteral("dir"), int(f.layoutDirection())},
        // M70 (D-079): the Word values Qt's block format cannot hold.
        {QStringLiteral("sp"), f.property(kParagraphSpacing).toStringList().join(QLatin1Char(','))},
        {QStringLiteral("ctx"), f.boolProperty(kContextualSpacing)},
        {QStringLiteral("outline"), f.hasProperty(kOutlineLevel) ? f.intProperty(kOutlineLevel) : -1},
        {QStringLiteral("wtabs"), f.stringProperty(kTabStops)},
        {QStringLiteral("pbdr"), f.stringProperty(kParagraphBorderSpec)},
        {QStringLiteral("widow"), !f.hasProperty(kWidowControl) || f.boolProperty(kWidowControl)},
        // M73 (D-082): list membership and where a list paragraph's number stands.
        {QStringLiteral("inlist"), f.objectIndex() >= 0},
        {QStringLiteral("lhang"), f.hasProperty(kListHanging) ? qRound(f.doubleProperty(kListHanging) * 100) : -1},
    };
}

QVariantMap charSnapshot(const QTextCharFormat &f)
{
    return {
        {QStringLiteral("family"), f.hasProperty(QTextFormat::FontFamilies)
                                       ? f.fontFamilies().toStringList().join(QLatin1Char('|'))
                                       : QString()},
        {QStringLiteral("size"), qRound(f.fontPointSize() * 100)},
        {QStringLiteral("weight"), f.fontWeight()},
        {QStringLiteral("italic"), f.fontItalic()},
        {QStringLiteral("ustyle"), int(f.underlineStyle())},
        {QStringLiteral("strike"), f.fontStrikeOut()},
        {QStringLiteral("caps"), int(f.fontCapitalization())},
        {QStringLiteral("fg"), colorKey(f.foreground(), f.hasProperty(QTextFormat::ForegroundBrush))},
        {QStringLiteral("bg"), colorKey(f.background(), f.hasProperty(QTextFormat::BackgroundBrush))},
        {QStringLiteral("valign"), int(f.verticalAlignment())},
        {QStringLiteral("lspacing"), f.hasProperty(QTextFormat::FontLetterSpacing)
                                         ? qRound(f.fontLetterSpacing() * 100)
                                         : 0},
        {QStringLiteral("stretch"), f.hasProperty(QTextFormat::FontStretch) ? f.fontStretch() : 0},
        {QStringLiteral("baseline"), qRound(f.baselineOffset() * 100)},
        {QStringLiteral("href"), f.anchorHref()},
        {QStringLiteral("ucolor"), f.hasProperty(QTextFormat::TextUnderlineColor) && f.underlineColor().isValid()
                                       ? f.underlineColor().name()
                                       : QString()},
        {QStringLiteral("uword"), f.stringProperty(kUnderlineWord)},
        {QStringLiteral("dstrike"), f.boolProperty(kDoubleStrike)},
        {QStringLiteral("hidden"), f.boolProperty(kHiddenText)},
        {QStringLiteral("kern"), f.intProperty(kKerning)},
        // M73 (D-082): a character style applied in QindaWrite.
        {QStringLiteral("rstyle"), f.stringProperty(kRunStyleId)},
    };
}

qreal displayedPointSize(const QTextCharFormat &format, const QFont &defaultFont)
{
    if (format.fontPointSize() > 0) {
        return format.fontPointSize();
    }
    if (!format.hasProperty(QTextFormat::FontSizeAdjustment)) {
        return 0;
    }
    // AGENT-NOTE: the same table and index as QTextFormatPrivate::resolveFont
    // (qtextformat.cpp, Qt 6): step 0 is the default size, +1 "large" ×1.2,
    // +2 ×1.5 (Markdown "##"), +3 ×2 ("#"), -1 ×0.8, -2 ×0.7.
    static constexpr qreal kScale[7] = {0.7, 0.8, 1.0, 1.2, 1.5, 2.0, 2.4};
    const int step = qBound(0, format.intProperty(QTextFormat::FontSizeAdjustment) + 2, 6);
    const qreal base = defaultFont.pointSizeF() > 0 ? defaultFont.pointSizeF() : 11.0;
    return kScale[step] * base;
}

QString wordFontFamily(const QString &family)
{
    const QString key = family.trimmed().toLower().remove(QLatin1Char(' ')).remove(QLatin1Char('-'));
    if (key == u"sansserif" || key == u"sans" || key == u"systemui" || key == u"uisansserif") {
        return QStringLiteral("Calibri");
    }
    if (key == u"serif" || key == u"uiserif") {
        return QStringLiteral("Times New Roman");
    }
    if (key == u"monospace" || key == u"mono" || key == u"uimonospace") {
        return QStringLiteral("Courier New");
    }
    return family;
}

QTextFormat::PageBreakFlags ownPageBreaks(const QTextBlockFormat &format)
{
    if (format.hasProperty(kSyntheticBreak)) {
        return QTextFormat::PageBreakFlags(format.intProperty(kSyntheticBreak));
    }
    return format.pageBreakPolicy();
}

LineSpacing ownLineSpacing(const QTextBlockFormat &format)
{
    if (format.lineHeightType() == QTextBlockFormat::LineDistanceHeight && format.hasProperty(kLineSpacingOwn)) {
        const QVariantList own = format.property(kLineSpacingOwn).toList();
        if (own.size() == 2) {
            return {own.at(0).toInt(), own.at(1).toReal()};
        }
    }
    return {format.lineHeightType(), format.lineHeight()};
}

} // namespace QindaOffice::Docx
