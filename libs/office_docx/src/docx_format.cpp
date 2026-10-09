// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_format.h"

#include "docx_borders.h"
#include "docx_paragraph_format.h"
#include "docx_properties.h"
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
        out.autoBefore = Units::parseOnOff(wAttr(spacing, u"beforeAutospacing"), false);
        out.autoAfter = Units::parseOnOff(wAttr(spacing, u"afterAutospacing"), false);
        out.before = out.autoBefore ? Units::ptToPx(14) : Units::twipsToPx(twipsAttr(spacing, u"before"));
        out.after = out.autoAfter ? Units::ptToPx(14) : Units::twipsToPx(twipsAttr(spacing, u"after"));
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
    if (onOff(pPr, u"keepNext")) {
        f.setProperty(kKeepWithNext, true);
    }
    // AGENT-NOTE: Word keeps widow/orphan control ON when no level of the
    // style chain mentions it (LibreOffice's DOCX import does the same); the
    // corpus never sets it and one document turns it off explicitly.
    if (pPr != nullptr && pPr->child(kW, u"widowControl") != nullptr && !onOff(pPr, u"widowControl")) {
        f.setProperty(kWidowControl, false);
    }
    if (onOff(pPr, u"pageBreakBefore")) {
        f.setPageBreakPolicy(QTextFormat::PageBreak_AlwaysBefore);
    }
    if (onOff(pPr, u"bidi")) {
        f.setLayoutDirection(Qt::RightToLeft);
    }
    // M70 (D-079): the Word values the formatting dialogs edit and the
    // writer compares with the snapshot (docx_properties.h).
    const XmlElement *spacing = pPr != nullptr ? pPr->child(kW, u"spacing") : nullptr;
    f.setProperty(kParagraphSpacing,
                  QVariantList{qRound(twipsAttr(spacing, u"before")), qRound(twipsAttr(spacing, u"after")),
                               Units::parseOnOff(wAttr(spacing, u"beforeAutospacing"), false),
                               Units::parseOnOff(wAttr(spacing, u"afterAutospacing"), false)});
    if (onOff(pPr, u"contextualSpacing")) {
        f.setProperty(kContextualSpacing, true);
    }
    if (const QString level = childVal(pPr, u"outlineLvl"); !level.isEmpty()) {
        f.setProperty(kOutlineLevel, qBound(0, level.toInt(), 9));
    }
    if (const XmlElement *tabs = pPr != nullptr ? pPr->child(kW, u"tabs") : nullptr) {
        if (const QList<TabStop> stops = readTabStops(tabs); !stops.isEmpty()) {
            f.setProperty(kTabStops, tabStopsSpec(stops));
        }
    }
    if (const XmlElement *pBdr = pPr != nullptr ? pPr->child(kW, u"pBdr") : nullptr) {
        if (const WordBorders borders = readBorders(pBdr, theme); !borders.isEmpty()) {
            setParagraphBorders(f, borders);
        }
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
    // M70 (D-079): what Qt cannot draw is kept as Word wrote it (shown
    // approximately: one underline, one strike line, hidden text shown).
    static const QStringList kQtUnderlines{QStringLiteral("single"), QStringLiteral("dash"), QStringLiteral("dotted"),
                                           QStringLiteral("dotDash"), QStringLiteral("dotDotDash"),
                                           QStringLiteral("wave"), QStringLiteral("none")};
    if (const QString uval = wAttr(u, u"val", u != nullptr ? QStringLiteral("single") : QString());
        !uval.isEmpty() && !kQtUnderlines.contains(uval)) {
        f.setProperty(kUnderlineWord, uval);
    }
    if (onOff(rPr, u"dstrike")) {
        f.setProperty(kDoubleStrike, true);
    }
    if (onOff(rPr, u"vanish")) {
        f.setProperty(kHiddenText, true);
    }
    if (const int kern = childVal(rPr, u"kern").toInt(); kern > 0) {
        f.setProperty(kKerning, kern);
    }
    return f;
}

QString paragraphBordersSpec(const XmlElement &pBdr, const Ooxml::Theme &theme)
{
    return paragraphDisplaySpec(readBorders(&pBdr, theme));
}

} // namespace QindaOffice::Docx
