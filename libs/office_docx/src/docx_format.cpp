// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_format.h"

#include "docx_styles.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QTextOption>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

qreal twipsAttr(const XmlElement *e, QStringView name, qreal fallback = 0)
{
    const QString v = wAttr(e, name);
    return v.isEmpty() ? fallback : Units::parseTwips(v, fallback);
}

QColor hexColor(const QString &hex)
{
    if (hex.size() != 6) {
        return {};
    }
    bool ok = false;
    const uint rgb = hex.toUInt(&ok, 16);
    return ok ? QColor::fromRgb(rgb) : QColor();
}

// Word's w:themeTint / w:themeShade arithmetic (hex 00–FF, HSL luminance).
QColor themed(QColor color, const XmlElement *e, QStringView tintAttr, QStringView shadeAttr)
{
    bool ok = false;
    const int tint = wAttr(e, tintAttr).toInt(&ok, 16);
    float h = 0, s = 0, l = 0;
    if (ok) {
        color.getHslF(&h, &s, &l);
        l = l * float(tint) / 255.0f + (1.0f - float(tint) / 255.0f);
        return QColor::fromHslF(h, s, qBound(0.0f, l, 1.0f)).toRgb();
    }
    const int shade = wAttr(e, shadeAttr).toInt(&ok, 16);
    if (ok) {
        color.getHslF(&h, &s, &l);
        return QColor::fromHslF(h, s, l * float(shade) / 255.0f).toRgb();
    }
    return color;
}

// WordprocessingML theme colour names → DrawingML scheme slots.
QString themeSlot(const QString &name)
{
    if (name == u"text1" || name == u"dark1") {
        return QStringLiteral("dk1");
    }
    if (name == u"background1" || name == u"light1") {
        return QStringLiteral("lt1");
    }
    if (name == u"text2" || name == u"dark2") {
        return QStringLiteral("dk2");
    }
    if (name == u"background2" || name == u"light2") {
        return QStringLiteral("lt2");
    }
    if (name == u"hyperlink") {
        return QStringLiteral("hlink");
    }
    if (name == u"followedHyperlink") {
        return QStringLiteral("folHlink");
    }
    return name; // accent1 … accent6
}

QTextCharFormat::UnderlineStyle underlineStyle(const QString &val)
{
    if (val.isEmpty() || val == u"none") {
        return QTextCharFormat::NoUnderline;
    }
    if (val.startsWith(u"dotDotDash") || val.startsWith(u"dashDotDot")) {
        return QTextCharFormat::DashDotDotLine;
    }
    if (val.startsWith(u"dotDash") || val.startsWith(u"dashDot")) {
        return QTextCharFormat::DashDotLine;
    }
    if (val.startsWith(u"dotted")) {
        return QTextCharFormat::DotLine;
    }
    if (val.startsWith(u"dash")) {
        return QTextCharFormat::DashUnderline;
    }
    if (val.startsWith(u"wav")) {
        return QTextCharFormat::WaveUnderline;
    }
    return QTextCharFormat::SingleUnderline; // single, words, double, thick
}

QString colorKey(const QBrush &brush, bool has)
{
    return has && brush.style() != Qt::NoBrush ? brush.color().name(QColor::HexArgb) : QString();
}

} // namespace

QColor wordColor(const XmlElement *element, const Ooxml::Theme &theme, QStringView valueAttribute)
{
    if (element == nullptr) {
        return {};
    }
    const QString val = wAttr(element, valueAttribute);
    QColor color = hexColor(val);
    const QString themeName = wAttr(element, u"themeColor");
    if (!color.isValid() && !themeName.isEmpty() && val != u"auto") {
        const QString slot = themeSlot(themeName);
        color = theme.colors.value(slot, Ooxml::defaultOfficeTheme().colors.value(slot));
        if (color.isValid()) {
            color = themed(color, element, u"themeTint", u"themeShade");
        }
    }
    return color;
}

QColor shadingColor(const XmlElement *shd, const Ooxml::Theme &theme)
{
    if (shd == nullptr) {
        return {};
    }
    const QString pattern = wAttr(shd, u"val", QStringLiteral("clear"));
    QColor fill = hexColor(wAttr(shd, u"fill"));
    if (!fill.isValid() && !wAttr(shd, u"themeFill").isEmpty()) {
        const QString slot = themeSlot(wAttr(shd, u"themeFill"));
        fill = theme.colors.value(slot, Ooxml::defaultOfficeTheme().colors.value(slot));
        if (fill.isValid()) {
            fill = themed(fill, shd, u"themeFillTint", u"themeFillShade");
        }
    }
    if (pattern == u"nil" || pattern == u"clear") {
        return fill;
    }
    QColor fore = hexColor(wAttr(shd, u"color"));
    if (!fore.isValid()) {
        fore = Qt::black;
    }
    if (pattern == u"solid") {
        return fore;
    }
    if (pattern.startsWith(u"pct")) {
        // pctN: N % of the pattern colour over the fill.
        const double f = pattern.mid(3).toDouble() / 100.0;
        const QColor base = fill.isValid() ? fill : QColor(Qt::white);
        return QColor::fromRgbF(float(base.redF() * (1 - f) + fore.redF() * f),
                                float(base.greenF() * (1 - f) + fore.greenF() * f),
                                float(base.blueF() * (1 - f) + fore.blueF() * f));
    }
    return fill;
}

QString latinFont(const XmlElement *rFonts, const Ooxml::Theme &theme)
{
    if (rFonts == nullptr) {
        return {};
    }
    for (QStringView themeAttr : {u"asciiTheme", u"hAnsiTheme"}) {
        const QString t = wAttr(rFonts, themeAttr);
        if (!t.isEmpty()) {
            const Ooxml::Theme &source = theme.isValid() ? theme : Ooxml::defaultOfficeTheme();
            QString font = t.startsWith(u"major") ? source.majorLatin : source.minorLatin;
            if (font.isEmpty()) {
                font = t.startsWith(u"major") ? Ooxml::defaultOfficeTheme().majorLatin
                                              : Ooxml::defaultOfficeTheme().minorLatin;
            }
            return font;
        }
    }
    for (QStringView attr : {u"ascii", u"hAnsi", u"cs", u"eastAsia"}) {
        const QString f = wAttr(rFonts, attr);
        if (!f.isEmpty()) {
            return f;
        }
    }
    return {};
}

ParagraphSpacing spacingOf(const XmlElement *pPr)
{
    ParagraphSpacing out;
    const XmlElement *spacing = pPr != nullptr ? pPr->child(kW, u"spacing") : nullptr;
    if (spacing != nullptr) {
        // Auto spacing is Word's HTML compatibility value: 14 pt.
        out.before = Units::parseOnOff(wAttr(spacing, u"beforeAutospacing"), false)
            ? Units::ptToPx(14)
            : Units::twipsToPx(twipsAttr(spacing, u"before"));
        out.after = Units::parseOnOff(wAttr(spacing, u"afterAutospacing"), false)
            ? Units::ptToPx(14)
            : Units::twipsToPx(twipsAttr(spacing, u"after"));
    }
    out.contextual = onOff(pPr, u"contextualSpacing");
    return out;
}

QTextBlockFormat blockFormatFromPPr(const XmlElement *pPr, const Ooxml::Theme &theme)
{
    QTextBlockFormat f;
    const QString jc = childVal(pPr, u"jc", QStringLiteral("left"));
    if (jc == u"center") {
        f.setAlignment(Qt::AlignHCenter);
    } else if (jc == u"right" || jc == u"end") {
        f.setAlignment(Qt::AlignRight);
    } else if (jc == u"both" || jc == u"distribute" || jc.endsWith(u"Kashida") || jc == u"thaiDistribute") {
        f.setAlignment(Qt::AlignJustify);
    } else {
        f.setAlignment(Qt::AlignLeft);
    }
    const XmlElement *ind = pPr != nullptr ? pPr->child(kW, u"ind") : nullptr;
    const qreal left = Units::twipsToPx(twipsAttr(ind, u"left", twipsAttr(ind, u"start")));
    const qreal right = Units::twipsToPx(twipsAttr(ind, u"right", twipsAttr(ind, u"end")));
    f.setLeftMargin(left);
    f.setRightMargin(right);
    if (!wAttr(ind, u"hanging").isEmpty()) {
        f.setTextIndent(-Units::twipsToPx(twipsAttr(ind, u"hanging")));
    } else {
        f.setTextIndent(Units::twipsToPx(twipsAttr(ind, u"firstLine")));
    }
    if (const XmlElement *spacing = pPr != nullptr ? pPr->child(kW, u"spacing") : nullptr) {
        const QString line = wAttr(spacing, u"line");
        if (!line.isEmpty()) {
            const QString rule = wAttr(spacing, u"lineRule", QStringLiteral("auto"));
            const qreal twips = Units::parseTwips(line);
            if (rule == u"exact") {
                f.setLineHeight(Units::twipsToPx(qAbs(twips)), QTextBlockFormat::FixedHeight);
            } else if (rule == u"atLeast") {
                f.setLineHeight(Units::twipsToPx(twips), QTextBlockFormat::MinimumHeight);
            } else if (twips > 0) {
                f.setLineHeight(twips / 240.0 * 100.0, QTextBlockFormat::ProportionalHeight);
            }
        }
    }
    if (const XmlElement *tabs = pPr != nullptr ? pPr->child(kW, u"tabs") : nullptr) {
        QList<QTextOption::Tab> stops;
        for (const XmlElement *tab : tabs->elements(kW, u"tab")) {
            const QString val = wAttr(tab, u"val", QStringLiteral("left"));
            if (val == u"clear" || val == u"bar" || val == u"num") {
                continue;
            }
            // AGENT-NOTE: Word measures tab stops from the text column's
            // edge, Qt from the paragraph's left indent (probed on Qt 6.11:
            // leftMargin 100 + stop 200 lands at 300).
            const qreal pos = Units::twipsToPx(twipsAttr(tab, u"pos")) - left;
            if (pos <= 0) {
                continue;
            }
            QTextOption::TabType type = QTextOption::LeftTab;
            QChar delimiter;
            if (val == u"center") {
                type = QTextOption::CenterTab;
            } else if (val == u"right" || val == u"end") {
                type = QTextOption::RightTab;
            } else if (val == u"decimal") {
                type = QTextOption::DelimiterTab;
                delimiter = QLatin1Char('.');
            }
            stops.append(QTextOption::Tab(pos, type, delimiter));
        }
        if (!stops.isEmpty()) {
            f.setTabPositions(stops);
        }
    }
    const QColor background = shadingColor(pPr != nullptr ? pPr->child(kW, u"shd") : nullptr, theme);
    if (background.isValid()) {
        f.setBackground(background);
    }
    if (onOff(pPr, u"keepLines")) {
        f.setNonBreakableLines(true);
    }
    if (onOff(pPr, u"pageBreakBefore")) {
        f.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    }
    if (onOff(pPr, u"bidi")) {
        f.setLayoutDirection(Qt::RightToLeft);
    }
    return f;
}

QTextCharFormat charFormatFromRPr(const XmlElement *rPr, const Ooxml::Theme &theme)
{
    QTextCharFormat f;
    const QString family = latinFont(rPr != nullptr ? rPr->child(kW, u"rFonts") : nullptr, theme);
    if (!family.isEmpty()) {
        f.setFontFamilies({family});
    }
    const QString sz = childVal(rPr, u"sz");
    const qreal halfPoints = sz.isEmpty() ? 20.0 : sz.toDouble();
    f.setFontPointSize(halfPoints > 0 ? halfPoints / 2.0 : 10.0);
    f.setFontWeight(onOff(rPr, u"b") ? QFont::Bold : QFont::Normal);
    f.setFontItalic(onOff(rPr, u"i"));
    f.setFontStrikeOut(onOff(rPr, u"strike") || onOff(rPr, u"dstrike"));
    if (onOff(rPr, u"caps")) {
        f.setFontCapitalization(QFont::AllUppercase);
    } else if (onOff(rPr, u"smallCaps")) {
        f.setFontCapitalization(QFont::SmallCaps);
    } else {
        f.setFontCapitalization(QFont::MixedCase);
    }
    const XmlElement *u = rPr != nullptr ? rPr->child(kW, u"u") : nullptr;
    f.setUnderlineStyle(underlineStyle(wAttr(u, u"val", u != nullptr ? QStringLiteral("single") : QString())));
    if (u != nullptr) {
        const QColor uc = wordColor(u, theme, u"color");
        if (uc.isValid()) {
            f.setUnderlineColor(uc);
        }
    }
    const QColor color = wordColor(rPr != nullptr ? rPr->child(kW, u"color") : nullptr, theme);
    if (color.isValid()) {
        f.setForeground(color);
    }
    const QString highlight = childVal(rPr, u"highlight");
    QColor background = highlight.isEmpty() || highlight == u"none" ? QColor() : Ooxml::highlightColor(highlight);
    if (!background.isValid()) {
        background = shadingColor(rPr != nullptr ? rPr->child(kW, u"shd") : nullptr, theme);
    }
    if (background.isValid()) {
        f.setBackground(background);
    }
    const QString valign = childVal(rPr, u"vertAlign");
    f.setVerticalAlignment(valign == u"superscript" ? QTextCharFormat::AlignSuperScript
                               : valign == u"subscript" ? QTextCharFormat::AlignSubScript
                                                        : QTextCharFormat::AlignNormal);
    const QString spacing = childVal(rPr, u"spacing");
    if (!spacing.isEmpty() && Units::parseTwips(spacing) != 0) {
        f.setFontLetterSpacingType(QFont::AbsoluteSpacing);
        f.setFontLetterSpacing(Units::twipsToPx(Units::parseTwips(spacing)));
    }
    const QString scale = childVal(rPr, u"w");
    if (!scale.isEmpty() && scale.toInt() > 0 && scale.toInt() != 100) {
        f.setFontStretch(scale.toInt());
    }
    const QString position = childVal(rPr, u"position");
    if (!position.isEmpty() && position.toDouble() != 0 && halfPoints > 0) {
        f.setBaselineOffset(position.toDouble() / halfPoints * 100.0);
    }
    return f;
}

QString paragraphBordersSpec(const XmlElement &pBdr, const Ooxml::Theme &theme)
{
    QStringList sides;
    for (const XmlElement *side : pBdr.elements()) {
        const QString val = wAttr(side, u"val", QStringLiteral("single"));
        if (val == u"nil" || val == u"none") {
            continue;
        }
        const QString name = side->name == u"start" ? QStringLiteral("left")
            : side->name == u"end"                  ? QStringLiteral("right")
                                                    : side->name;
        if (name != u"top" && name != u"bottom" && name != u"left" && name != u"right" && name != u"between") {
            continue;
        }
        // w:sz in eighths of a point, w:space in points.
        const qreal width = qMax<qreal>(0.75, Units::ptToPx(wAttr(side, u"sz", QStringLiteral("4")).toDouble() / 8.0));
        const QString style = val.startsWith(u"double") ? QStringLiteral("double")
            : val.startsWith(u"dot")                    ? QStringLiteral("dot")
            : val.startsWith(u"dash")                   ? QStringLiteral("dash")
                                                        : QStringLiteral("solid");
        QColor color = wordColor(side, theme, u"color");
        if (!color.isValid()) {
            color = Qt::black;
        }
        const qreal space = Units::ptToPx(wAttr(side, u"space", QStringLiteral("0")).toDouble());
        sides.append(QStringLiteral("%1:%2:%3:%4:%5").arg(name).arg(width, 0, 'f', 2).arg(style, color.name()).arg(space, 0, 'f', 2));
    }
    return sides.join(QLatin1Char(';'));
}

QVariantMap blockSnapshot(const QTextBlockFormat &f)
{
    QStringList tabs;
    for (const QTextOption::Tab &t : f.tabPositions()) {
        tabs.append(QStringLiteral("%1:%2").arg(t.position, 0, 'f', 2).arg(int(t.type)));
    }
    return {
        {QStringLiteral("align"), int(f.alignment() & Qt::AlignHorizontal_Mask)},
        {QStringLiteral("left"), qRound(f.leftMargin() * 100)},
        {QStringLiteral("right"), qRound(f.rightMargin() * 100)},
        {QStringLiteral("indent"), qRound(f.textIndent() * 100)},
        {QStringLiteral("top"), qRound(f.topMargin() * 100)},
        {QStringLiteral("bottom"), qRound(f.bottomMargin() * 100)},
        {QStringLiteral("lhType"), f.lineHeightType()},
        {QStringLiteral("lh"), qRound(f.lineHeight() * 100)},
        {QStringLiteral("bg"), colorKey(f.background(), f.hasProperty(QTextFormat::BackgroundBrush))},
        {QStringLiteral("break"), int(f.pageBreakPolicy())},
        {QStringLiteral("keep"), f.nonBreakableLines()},
        {QStringLiteral("heading"), f.headingLevel()},
        {QStringLiteral("tabs"), tabs.join(QLatin1Char(';'))},
        {QStringLiteral("dir"), int(f.layoutDirection())},
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
    };
}

} // namespace QindaOffice::Docx
