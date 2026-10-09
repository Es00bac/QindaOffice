// SPDX-License-Identifier: GPL-3.0-or-later
// w:pPr and w:rPr for the writer: preserved source markup for what is
// unchanged since import, re-derived markup for what the user changed
// (D-068 rule 3), children in schema order.

#include "docx_format.h"
#include "docx_properties.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"
#include "ooxml_theme.h"
#include "ooxml_units.h"

#include <QTextList>

#include <algorithm>
#include <numeric>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
using Ooxml::XmlChild;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

const QStringList &orderFor(QStringView parent)
{
    static const QStringList pPr{
        QStringLiteral("pStyle"), QStringLiteral("keepNext"), QStringLiteral("keepLines"),
        QStringLiteral("pageBreakBefore"), QStringLiteral("framePr"), QStringLiteral("widowControl"),
        QStringLiteral("numPr"), QStringLiteral("suppressLineNumbers"), QStringLiteral("pBdr"),
        QStringLiteral("shd"), QStringLiteral("tabs"), QStringLiteral("suppressAutoHyphens"),
        QStringLiteral("kinsoku"), QStringLiteral("wordWrap"), QStringLiteral("overflowPunct"),
        QStringLiteral("topLinePunct"), QStringLiteral("autoSpaceDE"), QStringLiteral("autoSpaceDN"),
        QStringLiteral("bidi"), QStringLiteral("adjustRightInd"), QStringLiteral("snapToGrid"),
        QStringLiteral("spacing"), QStringLiteral("ind"), QStringLiteral("contextualSpacing"),
        QStringLiteral("mirrorIndents"), QStringLiteral("suppressOverlap"), QStringLiteral("jc"),
        QStringLiteral("textDirection"), QStringLiteral("textAlignment"), QStringLiteral("textboxTightWrap"),
        QStringLiteral("outlineLvl"), QStringLiteral("divId"), QStringLiteral("cnfStyle"), QStringLiteral("rPr"),
        QStringLiteral("sectPr"), QStringLiteral("pPrChange")};
    static const QStringList rPr{
        QStringLiteral("ins"), QStringLiteral("del"), QStringLiteral("moveFrom"), QStringLiteral("moveTo"),
        QStringLiteral("rStyle"), QStringLiteral("rFonts"), QStringLiteral("b"), QStringLiteral("bCs"),
        QStringLiteral("i"), QStringLiteral("iCs"), QStringLiteral("caps"), QStringLiteral("smallCaps"),
        QStringLiteral("strike"), QStringLiteral("dstrike"), QStringLiteral("outline"), QStringLiteral("shadow"),
        QStringLiteral("emboss"), QStringLiteral("imprint"), QStringLiteral("noProof"), QStringLiteral("snapToGrid"),
        QStringLiteral("vanish"), QStringLiteral("webHidden"), QStringLiteral("color"), QStringLiteral("spacing"),
        QStringLiteral("w"), QStringLiteral("kern"), QStringLiteral("position"), QStringLiteral("sz"),
        QStringLiteral("szCs"), QStringLiteral("highlight"), QStringLiteral("u"), QStringLiteral("effect"),
        QStringLiteral("bdr"), QStringLiteral("shd"), QStringLiteral("fitText"), QStringLiteral("vertAlign"),
        QStringLiteral("rtl"), QStringLiteral("cs"), QStringLiteral("em"), QStringLiteral("lang"),
        QStringLiteral("eastAsianLayout"), QStringLiteral("specVanish"), QStringLiteral("oMath"),
        QStringLiteral("rPrChange")};
    static const QStringList tblPr{
        QStringLiteral("tblStyle"), QStringLiteral("tblpPr"), QStringLiteral("tblOverlap"),
        QStringLiteral("bidiVisual"), QStringLiteral("tblStyleRowBandSize"), QStringLiteral("tblStyleColBandSize"),
        QStringLiteral("tblW"), QStringLiteral("jc"), QStringLiteral("tblCellSpacing"), QStringLiteral("tblInd"),
        QStringLiteral("tblBorders"), QStringLiteral("shd"), QStringLiteral("tblLayout"),
        QStringLiteral("tblCellMar"), QStringLiteral("tblLook"), QStringLiteral("tblCaption"),
        QStringLiteral("tblDescription"), QStringLiteral("tblPrChange")};
    static const QStringList tcPr{
        QStringLiteral("cnfStyle"), QStringLiteral("tcW"), QStringLiteral("gridSpan"), QStringLiteral("hMerge"),
        QStringLiteral("vMerge"), QStringLiteral("tcBorders"), QStringLiteral("shd"), QStringLiteral("noWrap"),
        QStringLiteral("tcMar"), QStringLiteral("textDirection"), QStringLiteral("tcFitText"),
        QStringLiteral("vAlign"), QStringLiteral("hideMark"), QStringLiteral("headers"), QStringLiteral("cellIns"),
        QStringLiteral("cellDel"), QStringLiteral("cellMerge"), QStringLiteral("tcPrChange")};
    static const QStringList trPr{QStringLiteral("cnfStyle"), QStringLiteral("divId"), QStringLiteral("gridBefore"),
                                  QStringLiteral("gridAfter"), QStringLiteral("wBefore"), QStringLiteral("wAfter"),
                                  QStringLiteral("cantSplit"), QStringLiteral("trHeight"), QStringLiteral("tblHeader"),
                                  QStringLiteral("tblCellSpacing"), QStringLiteral("jc"), QStringLiteral("hidden"),
                                  QStringLiteral("ins"), QStringLiteral("del"), QStringLiteral("trPrChange")};
    static const QStringList sectPr{
        QStringLiteral("headerReference"), QStringLiteral("footerReference"), QStringLiteral("footnotePr"),
        QStringLiteral("endnotePr"), QStringLiteral("type"), QStringLiteral("pgSz"), QStringLiteral("pgMar"),
        QStringLiteral("paperSrc"), QStringLiteral("pgBorders"), QStringLiteral("lnNumType"),
        QStringLiteral("pgNumType"), QStringLiteral("cols"), QStringLiteral("formProt"), QStringLiteral("vAlign"),
        QStringLiteral("noEndnote"), QStringLiteral("titlePg"), QStringLiteral("textDirection"),
        QStringLiteral("bidi"), QStringLiteral("rtlGutter"), QStringLiteral("docGrid"),
        QStringLiteral("printerSettings"), QStringLiteral("sectPrChange")};
    static const QStringList borders{QStringLiteral("top"), QStringLiteral("start"), QStringLiteral("left"),
                                     QStringLiteral("bottom"), QStringLiteral("end"), QStringLiteral("right"),
                                     QStringLiteral("insideH"), QStringLiteral("insideV"), QStringLiteral("tl2br"),
                                     QStringLiteral("tr2bl"), QStringLiteral("between"), QStringLiteral("bar")};
    static const QStringList numPr{QStringLiteral("ilvl"), QStringLiteral("numId"),
                                   QStringLiteral("numberingChange"), QStringLiteral("ins")};
    static const QStringList none;
    if (parent == u"pPr") {
        return pPr;
    }
    if (parent == u"rPr") {
        return rPr;
    }
    if (parent == u"tblPr") {
        return tblPr;
    }
    if (parent == u"tcPr") {
        return tcPr;
    }
    if (parent == u"trPr") {
        return trPr;
    }
    if (parent == u"sectPr") {
        return sectPr;
    }
    if (parent == u"tblBorders" || parent == u"tcBorders" || parent == u"tcMar" || parent == u"tblCellMar"
        || parent == u"pBdr") {
        return borders;
    }
    if (parent == u"numPr") {
        return numPr;
    }
    return none;
}

QString hex(const QColor &color)
{
    return color.name(QColor::HexRgb).mid(1).toUpper();
}

QString halfPoints(qreal pt)
{
    return QString::number(qMax(1, qRound(pt * 2)));
}

} // namespace

void sortChildren(XmlElement &element)
{
    const QStringList &order = orderFor(element.name);
    for (XmlChild &child : element.children) {
        if (child.element && child.element->ns == kW) {
            sortChildren(*child.element);
        }
    }
    if (order.isEmpty() || element.ns != kW) {
        return;
    }
    // Unknown names sort with the known name before them (stable).
    QList<qsizetype> rank;
    qsizetype last = -1;
    for (const XmlChild &child : element.children) {
        const qsizetype at = child.element && child.element->ns == kW ? order.indexOf(child.element->name) : -1;
        last = at >= 0 ? at : last;
        rank.append(at >= 0 ? at : last);
    }
    QList<qsizetype> index(element.children.size());
    std::iota(index.begin(), index.end(), 0);
    std::stable_sort(index.begin(), index.end(), [&rank](qsizetype a, qsizetype b) { return rank[a] < rank[b]; });
    QList<XmlChild> sorted;
    sorted.reserve(element.children.size());
    for (const qsizetype i : index) {
        sorted.append(element.children.at(i));
    }
    element.children = sorted;
}

XmlElement &wChild(XmlElement &parent, const QString &name)
{
    if (XmlElement *existing = parent.child(kW, name)) {
        return *existing;
    }
    return parent.appendElement(kW, name);
}

void setVal(XmlElement &element, const QString &value)
{
    element.setAttribute(kW, QStringLiteral("val"), value);
}

XmlElement &setChildVal(XmlElement &parent, const QString &name, const QString &value)
{
    XmlElement &child = wChild(parent, name);
    child.attributes.clear();
    setVal(child, value);
    return child;
}

QString twips(qreal px)
{
    return QString::number(qRound(Units::pxToTwips(px)));
}

XmlElementPtr BodyWriter::paragraphProperties(const QTextBlock &block)
{
    const QTextBlockFormat bf = block.blockFormat();
    XmlElementPtr pPr = Ooxml::parseFragment(bf.property(kParagraphXml).toString());
    if (!pPr) {
        pPr = std::make_shared<XmlElement>(kW, QStringLiteral("pPr"));
    }
    const QVariantMap then = bf.property(kParagraphSnapshot).toMap();
    const bool imported = !then.isEmpty();
    const QVariantMap now = blockSnapshot(bf);
    auto changed = [&](const char *key) {
        const QString k = QString::fromLatin1(key);
        return !imported || then.value(k) != now.value(k);
    };

    QString styleId = bf.property(kParagraphStyleId).toString();
    if (styleId.isEmpty() && bf.hasProperty(kWriteStyleIdProperty)) {
        styleId = builtInStyleId(bf.intProperty(kWriteStyleIdProperty));
    }
    const bool hadStyle = pPr->child(kW, u"pStyle") != nullptr;
    if (!styleId.isEmpty() && (hadStyle || styleId != m_env.sourceStyles.defaultParagraphStyle())) {
        setChildVal(*pPr, QStringLiteral("pStyle"), styleId);
        m_env.usedStyles.insert(styleId);
    }
    if (changed("keep")) {
        pPr->removeChildren(kW, u"keepLines");
        if (bf.nonBreakableLines()) {
            wChild(*pPr, QStringLiteral("keepLines"));
        } else if (imported) {
            setChildVal(*pPr, QStringLiteral("keepLines"), QStringLiteral("0"));
        }
    }
    if (changed("keepNext")) {
        pPr->removeChildren(kW, u"keepNext");
        if (bf.boolProperty(kKeepWithNext)) {
            wChild(*pPr, QStringLiteral("keepNext"));
        } else if (imported) {
            setChildVal(*pPr, QStringLiteral("keepNext"), QStringLiteral("0"));
        }
    }
    const bool breakBefore = ownPageBreaks(bf) & QTextFormat::PageBreak_AlwaysBefore;
    if (!bf.boolProperty(kPageBreakRun) && changed("break")) {
        pPr->removeChildren(kW, u"pageBreakBefore");
        if (breakBefore && block.position() > 0) {
            wChild(*pPr, QStringLiteral("pageBreakBefore"));
        } else if (imported) {
            setChildVal(*pPr, QStringLiteral("pageBreakBefore"), QStringLiteral("0"));
        }
    }
    if (const QTextList *list = block.textList()) {
        const NumberingBuilder::Ref ref = m_env.numbering->refFor(list, m_document->indentWidth());
        XmlElement &numPr = wChild(*pPr, QStringLiteral("numPr"));
        numPr.children.clear();
        setChildVal(numPr, QStringLiteral("ilvl"), QString::number(ref.ilvl));
        setChildVal(numPr, QStringLiteral("numId"), QString::number(ref.numId));
    } else if (pPr->child(kW, u"numPr") != nullptr || m_env.sourceStyles.styleNumbering(styleId) != nullptr) {
        XmlElement &numPr = wChild(*pPr, QStringLiteral("numPr"));
        numPr.children.clear();
        setChildVal(numPr, QStringLiteral("numId"), QStringLiteral("0"));
    }
    if (changed("bg")) {
        pPr->removeChildren(kW, u"shd");
        const bool has = bf.hasProperty(QTextFormat::BackgroundBrush) && bf.background().style() != Qt::NoBrush;
        if (has || imported) {
            XmlElement &shd = wChild(*pPr, QStringLiteral("shd"));
            shd.setAttribute(kW, QStringLiteral("val"), QStringLiteral("clear"));
            shd.setAttribute(kW, QStringLiteral("color"), QStringLiteral("auto"));
            shd.setAttribute(kW, QStringLiteral("fill"), has ? hex(bf.background().color()) : QStringLiteral("auto"));
        }
    }
    if (changed("tabs") || (changed("left") && !bf.tabPositions().isEmpty())) {
        pPr->removeChildren(kW, u"tabs");
        if (!bf.tabPositions().isEmpty()) {
            XmlElement &tabs = wChild(*pPr, QStringLiteral("tabs"));
            for (const QTextOption::Tab &t : bf.tabPositions()) {
                XmlElement &tab = tabs.appendElement(kW, QStringLiteral("tab"));
                const QString type = t.type == QTextOption::CenterTab      ? QStringLiteral("center")
                    : t.type == QTextOption::RightTab                      ? QStringLiteral("right")
                    : t.type == QTextOption::DelimiterTab                  ? QStringLiteral("decimal")
                                                                           : QStringLiteral("left");
                setVal(tab, type);
                tab.setAttribute(kW, QStringLiteral("pos"), twips(t.position + bf.leftMargin()));
            }
        }
    }
    if (changed("dir")) {
        pPr->removeChildren(kW, u"bidi");
        if (bf.layoutDirection() == Qt::RightToLeft) {
            wChild(*pPr, QStringLiteral("bidi"));
        }
    }
    if (changed("top") || changed("bottom") || changed("lh") || changed("lhType")) {
        XmlElement &spacing = wChild(*pPr, QStringLiteral("spacing"));
        if (!imported || changed("top")) {
            spacing.removeAttribute(kW, u"beforeAutospacing").removeAttribute(kW, u"beforeLines");
            spacing.setAttribute(kW, QStringLiteral("before"), twips(bf.topMargin()));
        }
        if (!imported || changed("bottom")) {
            spacing.removeAttribute(kW, u"afterAutospacing").removeAttribute(kW, u"afterLines");
            spacing.setAttribute(kW, QStringLiteral("after"), twips(bf.bottomMargin()));
        }
        if (!imported || changed("lh") || changed("lhType")) {
            const LineSpacing own = ownLineSpacing(bf);
            QString line = QStringLiteral("240");
            QString rule = QStringLiteral("auto");
            if (own.type == QTextBlockFormat::ProportionalHeight) {
                line = QString::number(qRound(own.value * 2.4));
            } else if (own.type == QTextBlockFormat::FixedHeight) {
                line = twips(own.value);
                rule = QStringLiteral("exact");
            } else if (own.type == QTextBlockFormat::MinimumHeight) {
                line = twips(own.value);
                rule = QStringLiteral("atLeast");
            }
            spacing.setAttribute(kW, QStringLiteral("line"), line);
            spacing.setAttribute(kW, QStringLiteral("lineRule"), rule);
        }
    }
    const bool inList = block.textList() != nullptr;
    if (changed("left") || changed("right") || (changed("indent") && !inList)) {
        const qreal listIndent = inList ? block.textList()->format().indent() * m_document->indentWidth() : 0.0;
        const bool any = bf.leftMargin() != 0 || bf.rightMargin() != 0 || bf.textIndent() != 0 || listIndent != 0;
        if (any || imported) {
            XmlElement &ind = wChild(*pPr, QStringLiteral("ind"));
            ind.attributes.clear();
            ind.setAttribute(kW, QStringLiteral("left"), twips(bf.leftMargin() + listIndent));
            ind.setAttribute(kW, QStringLiteral("right"), twips(bf.rightMargin()));
            if (inList) {
                ind.setAttribute(kW, QStringLiteral("hanging"), QStringLiteral("360"));
            } else if (bf.textIndent() < 0) {
                ind.setAttribute(kW, QStringLiteral("hanging"), twips(-bf.textIndent()));
            } else if (bf.textIndent() > 0) {
                ind.setAttribute(kW, QStringLiteral("firstLine"), twips(bf.textIndent()));
            }
        }
    }
    if (changed("align")) {
        pPr->removeChildren(kW, u"jc");
        const Qt::Alignment a = bf.alignment() & Qt::AlignHorizontal_Mask;
        const QString jc = a == Qt::AlignHCenter ? QStringLiteral("center")
            : a == Qt::AlignRight                ? QStringLiteral("right")
            : a == Qt::AlignJustify              ? QStringLiteral("both")
                                                 : QStringLiteral("left");
        if (imported || jc != u"left" || styleId == u"Title") {
            setChildVal(*pPr, QStringLiteral("jc"), jc);
        }
    }
    if (!imported && bf.headingLevel() > 0 && styleId.isEmpty()) {
        setChildVal(*pPr, QStringLiteral("outlineLvl"), QString::number(bf.headingLevel() - 1));
    }
    if (!imported && pPr->child(kW, u"rPr") == nullptr) {
        if (XmlElementPtr mark = runProperties(block.charFormat())) {
            mark->name = QStringLiteral("rPr");
            pPr->appendElement(mark);
        }
    }
    sortChildren(*pPr);
    return pPr->children.isEmpty() && pPr->attributes.isEmpty() ? nullptr : pPr;
}

XmlElementPtr BodyWriter::runProperties(const QTextCharFormat &f)
{
    XmlElementPtr rPr = Ooxml::parseFragment(f.property(kRunXml).toString());
    if (!rPr) {
        rPr = std::make_shared<XmlElement>(kW, QStringLiteral("rPr"));
    }
    const QVariantMap then = f.property(kRunSnapshot).toMap();
    const bool imported = !then.isEmpty();
    const QVariantMap now = charSnapshot(f);
    auto changed = [&](const char *key) {
        const QString k = QString::fromLatin1(key);
        return !imported || then.value(k) != now.value(k);
    };
    auto toggle = [&](const QString &name, const QString &complex, bool on) {
        rPr->removeChildren(kW, name);
        if (!complex.isEmpty()) {
            rPr->removeChildren(kW, complex);
        }
        if (on) {
            wChild(*rPr, name);
            if (!complex.isEmpty()) {
                wChild(*rPr, complex);
            }
        } else if (imported) {
            setChildVal(*rPr, name, QStringLiteral("0"));
        }
    };
    // A tracked change's display marks (docx_properties.h) are not formatting.
    const bool insertMark = f.hasProperty(kRevisionInsertProperty);
    const bool deleteMark = f.hasProperty(kRevisionDeleteProperty);
    const QColor shown = f.foreground().color();
    const bool markColour = (insertMark && shown == QColor::fromRgb(kRevisionInsertColor))
        || (deleteMark && shown == QColor::fromRgb(kRevisionDeleteColor));
    if (!imported && f.isAnchor() && !f.anchorHref().isEmpty() && rPr->child(kW, u"rStyle") == nullptr) {
        setChildVal(*rPr, QStringLiteral("rStyle"), QStringLiteral("Hyperlink"));
        m_env.usedStyles.insert(QStringLiteral("Hyperlink"));
    }
    if (const QString style = f.property(kRunStyleId).toString(); !style.isEmpty()) {
        m_env.usedStyles.insert(style);
    }
    if (changed("family")) {
        rPr->removeChildren(kW, u"rFonts");
        const QString family =
            wordFontFamily(now.value(QStringLiteral("family")).toString().section(QLatin1Char('|'), 0, 0));
        if (!family.isEmpty()) {
            XmlElement &fonts = wChild(*rPr, QStringLiteral("rFonts"));
            for (const char *attr : {"ascii", "hAnsi", "cs"}) {
                fonts.setAttribute(kW, QString::fromLatin1(attr), family);
            }
        }
    }
    if (const qreal points = displayedPointSize(f, m_document->defaultFont()); changed("size") && points > 0) {
        setChildVal(*rPr, QStringLiteral("sz"), halfPoints(points));
        setChildVal(*rPr, QStringLiteral("szCs"), halfPoints(points));
    }
    if (changed("weight")) {
        toggle(QStringLiteral("b"), QStringLiteral("bCs"), f.fontWeight() >= QFont::DemiBold);
    }
    if (changed("italic")) {
        toggle(QStringLiteral("i"), QStringLiteral("iCs"), f.fontItalic());
    }
    if (changed("caps")) {
        toggle(QStringLiteral("caps"), QString(), f.fontCapitalization() == QFont::AllUppercase);
        toggle(QStringLiteral("smallCaps"), QString(), f.fontCapitalization() == QFont::SmallCaps);
    }
    if (changed("strike") && !deleteMark) {
        rPr->removeChildren(kW, u"dstrike");
        toggle(QStringLiteral("strike"), QString(), f.fontStrikeOut());
    }
    if (changed("fg") && !markColour) {
        rPr->removeChildren(kW, u"color");
        if (f.hasProperty(QTextFormat::ForegroundBrush) && f.foreground().style() != Qt::NoBrush) {
            setChildVal(*rPr, QStringLiteral("color"), hex(f.foreground().color()));
        } else if (imported) {
            setChildVal(*rPr, QStringLiteral("color"), QStringLiteral("auto"));
        }
    }
    if (changed("bg")) {
        rPr->removeChildren(kW, u"highlight");
        rPr->removeChildren(kW, u"shd");
        if (f.hasProperty(QTextFormat::BackgroundBrush) && f.background().style() != Qt::NoBrush) {
            const QColor c = f.background().color();
            const QString name = Ooxml::nearestHighlightName(c);
            if (!name.isEmpty() && Ooxml::highlightColor(name) == c.toRgb()) {
                setChildVal(*rPr, QStringLiteral("highlight"), name);
            } else {
                XmlElement &shd = wChild(*rPr, QStringLiteral("shd"));
                shd.setAttribute(kW, QStringLiteral("val"), QStringLiteral("clear"));
                shd.setAttribute(kW, QStringLiteral("color"), QStringLiteral("auto"));
                shd.setAttribute(kW, QStringLiteral("fill"), hex(c));
            }
        }
    }
    if (changed("ustyle") && !(insertMark && f.underlineStyle() == QTextCharFormat::SingleUnderline)) {
        rPr->removeChildren(kW, u"u");
        static const QHash<int, QString> kNames{
            {QTextCharFormat::SingleUnderline, QStringLiteral("single")},
            {QTextCharFormat::DashUnderline, QStringLiteral("dash")},
            {QTextCharFormat::DotLine, QStringLiteral("dotted")},
            {QTextCharFormat::DashDotLine, QStringLiteral("dotDash")},
            {QTextCharFormat::DashDotDotLine, QStringLiteral("dotDotDash")},
            {QTextCharFormat::WaveUnderline, QStringLiteral("wave")},
            {QTextCharFormat::SpellCheckUnderline, QStringLiteral("single")}};
        const QString u = kNames.value(f.underlineStyle());
        if (!u.isEmpty()) {
            setChildVal(*rPr, QStringLiteral("u"), u);
        } else if (imported) {
            setChildVal(*rPr, QStringLiteral("u"), QStringLiteral("none"));
        }
    }
    if (changed("valign")) {
        rPr->removeChildren(kW, u"vertAlign");
        const auto v = f.verticalAlignment();
        if (v == QTextCharFormat::AlignSuperScript) {
            setChildVal(*rPr, QStringLiteral("vertAlign"), QStringLiteral("superscript"));
        } else if (v == QTextCharFormat::AlignSubScript) {
            setChildVal(*rPr, QStringLiteral("vertAlign"), QStringLiteral("subscript"));
        } else if (imported) {
            setChildVal(*rPr, QStringLiteral("vertAlign"), QStringLiteral("baseline"));
        }
    }
    if (changed("lspacing")) {
        rPr->removeChildren(kW, u"spacing");
        if (f.hasProperty(QTextFormat::FontLetterSpacing) && f.fontLetterSpacingType() == QFont::AbsoluteSpacing
            && f.fontLetterSpacing() != 0) {
            setChildVal(*rPr, QStringLiteral("spacing"), twips(f.fontLetterSpacing()));
        }
    }
    if (changed("stretch")) {
        rPr->removeChildren(kW, u"w");
        if (f.hasProperty(QTextFormat::FontStretch) && f.fontStretch() > 0 && f.fontStretch() != 100) {
            setChildVal(*rPr, QStringLiteral("w"), QString::number(qBound(1, f.fontStretch(), 600)));
        }
    }
    if (changed("baseline")) {
        rPr->removeChildren(kW, u"position");
        if (const qreal points = displayedPointSize(f, m_document->defaultFont());
            f.baselineOffset() != 0 && points > 0) {
            setChildVal(*rPr, QStringLiteral("position"),
                        QString::number(qRound(f.baselineOffset() / 100.0 * points * 2)));
        }
    }
    sortChildren(*rPr);
    return rPr->children.isEmpty() ? nullptr : rPr;
}

} // namespace QindaOffice::Docx::detail
