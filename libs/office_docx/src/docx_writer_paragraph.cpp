// SPDX-License-Identifier: GPL-3.0-or-later
// The writer's w:pPr / w:rPr re-derivations for the Word values Qt's
// formats cannot hold (M70, D-079): a paragraph's own spacing, contextual
// spacing, outline level, widow control, tab stops with leaders and
// borders; a run's underline style and colour, double strikethrough, hidden
// text and kerning. Called by BodyWriter::paragraphProperties and
// ::runProperties (docx_writer_format.cpp) with their snapshot comparison;
// an unchanged value is never touched, so its source markup stays (D-068
// rule 3).

#include "docx_borders.h"
#include "docx_format.h"
#include "docx_lists.h"
#include "docx_paragraph_format.h"
#include "docx_properties.h"
#include "docx_writer_p.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QHash>
#include <QTextList>

namespace QindaOffice::Docx::detail {

using Ooxml::XmlElement;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

// The direct w:tabs that make the style's stops into `wanted` under the
// reader's merge (docx_styles.cpp mergeTabs: a stop replaces the style's at
// its position, w:val="clear" removes one).
void writeTabs(XmlElement &pPr, const QList<TabStop> &wanted, const QList<TabStop> &style)
{
    pPr.removeChildren(kW, u"tabs");
    QMap<int, TabStop> out; // position → the stop written there (clear = alignment "clear")
    for (const TabStop &s : style) {
        const bool kept = std::any_of(wanted.cbegin(), wanted.cend(), [&](const TabStop &w) { return w == s; });
        if (!kept) {
            out.insert(s.position, TabStop{s.position, QStringLiteral("clear"), QStringLiteral("none")});
        }
    }
    for (const TabStop &w : wanted) {
        if (!style.contains(w)) {
            out.insert(w.position, w);
        }
    }
    if (out.isEmpty()) {
        return;
    }
    XmlElement &tabs = wChild(pPr, QStringLiteral("tabs"));
    for (const TabStop &t : std::as_const(out)) {
        XmlElement &tab = tabs.appendElement(kW, QStringLiteral("tab"));
        setVal(tab, t.alignment);
        if (t.alignment != u"clear" && t.leader != u"none" && !t.leader.isEmpty()) {
            tab.setAttribute(kW, QStringLiteral("leader"), t.leader);
        }
        tab.setAttribute(kW, QStringLiteral("pos"), QString::number(t.position));
    }
}

} // namespace

void writeParagraphSpacing(XmlElement &pPr, const QTextBlockFormat &bf, bool imported, const ChangedFn &changed)
{
    const bool own = bf.hasProperty(kParagraphSpacing);
    const bool spacingChanged = own ? changed("sp") : (changed("top") || changed("bottom"));
    if (!spacingChanged && !changed("lh") && !changed("lhType")) {
        return;
    }
    XmlElement &spacing = wChild(pPr, QStringLiteral("spacing"));
    if (own && (!imported || changed("sp"))) {
        const OwnSpacing s = ownSpacing(bf);
        spacing.removeAttribute(kW, u"beforeLines").removeAttribute(kW, u"afterLines");
        spacing.setAttribute(kW, QStringLiteral("before"), QString::number(s.before));
        if (s.autoBefore) {
            spacing.setAttribute(kW, QStringLiteral("beforeAutospacing"), QStringLiteral("1"));
        } else {
            spacing.removeAttribute(kW, u"beforeAutospacing");
        }
        spacing.setAttribute(kW, QStringLiteral("after"), QString::number(s.after));
        if (s.autoAfter) {
            spacing.setAttribute(kW, QStringLiteral("afterAutospacing"), QStringLiteral("1"));
        } else {
            spacing.removeAttribute(kW, u"afterAutospacing");
        }
    } else if (!own) {
        if (!imported || changed("top")) {
            spacing.removeAttribute(kW, u"beforeAutospacing").removeAttribute(kW, u"beforeLines");
            spacing.setAttribute(kW, QStringLiteral("before"), twips(bf.topMargin()));
        }
        if (!imported || changed("bottom")) {
            spacing.removeAttribute(kW, u"afterAutospacing").removeAttribute(kW, u"afterLines");
            spacing.setAttribute(kW, QStringLiteral("after"), twips(bf.bottomMargin()));
        }
    }
    if (!imported || changed("lh") || changed("lhType")) {
        const LineSpacing line = ownLineSpacing(bf);
        QString value = QStringLiteral("240");
        QString rule = QStringLiteral("auto");
        if (line.type == QTextBlockFormat::ProportionalHeight) {
            value = QString::number(qRound(line.value * 2.4));
        } else if (line.type == QTextBlockFormat::FixedHeight) {
            value = twips(line.value);
            rule = QStringLiteral("exact");
        } else if (line.type == QTextBlockFormat::MinimumHeight) {
            value = twips(line.value);
            rule = QStringLiteral("atLeast");
        }
        spacing.setAttribute(kW, QStringLiteral("line"), value);
        spacing.setAttribute(kW, QStringLiteral("lineRule"), rule);
    }
}

void writeParagraphExtras(XmlElement &pPr, const QTextBlockFormat &bf, bool imported, const ChangedFn &changed,
                          const StyleSheet &styles, const QString &styleId)
{
    if (changed("ctx")) {
        pPr.removeChildren(kW, u"contextualSpacing");
        if (bf.boolProperty(kContextualSpacing)) {
            wChild(pPr, QStringLiteral("contextualSpacing"));
        } else if (imported) {
            setChildVal(pPr, QStringLiteral("contextualSpacing"), QStringLiteral("0"));
        }
    }
    if (bf.hasProperty(kOutlineLevel) && changed("outline")) {
        setChildVal(pPr, QStringLiteral("outlineLvl"), QString::number(qBound(0, bf.intProperty(kOutlineLevel), 9)));
    }
    if (changed("widow")) {
        pPr.removeChildren(kW, u"widowControl");
        const bool on = !bf.hasProperty(kWidowControl) || bf.boolProperty(kWidowControl);
        if (!on) {
            setChildVal(pPr, QStringLiteral("widowControl"), QStringLiteral("0"));
        } else if (imported) {
            wChild(pPr, QStringLiteral("widowControl"));
        }
    }
    if (bf.hasProperty(kTabStops) && changed("wtabs")) {
        const Ooxml::XmlElementPtr base = styles.paragraphProperties(styleId);
        writeTabs(pPr, parseTabStops(bf.stringProperty(kTabStops)),
                  readTabStops(base ? base->child(kW, u"tabs") : nullptr));
    }
    if (bf.hasProperty(kParagraphBorderSpec) && changed("pbdr")) {
        const WordBorders now = parseBordersSpec(bf.stringProperty(kParagraphBorderSpec));
        // The snapshot keeps the borders as read: the sides still equal to
        // them keep their source elements (theme colours and all).
        const QVariantMap snapshot = bf.property(kParagraphSnapshot).toMap();
        const WordBorders then = parseBordersSpec(snapshot.value(QStringLiteral("pbdr")).toString());
        const Ooxml::XmlElementPtr source = Ooxml::parseFragment(bf.stringProperty(kParagraphXml));
        const XmlElement *sourcePBdr = source ? source->child(kW, u"pBdr") : nullptr;
        pPr.removeChildren(kW, u"pBdr");
        if (!now.isEmpty()) {
            writeBorders(wChild(pPr, QStringLiteral("pBdr")), now, then, sourcePBdr);
        }
    }
}

void writeRunExtras(XmlElement &rPr, const QTextCharFormat &f, bool imported, const ChangedFn &changed,
                    bool insertMark)
{
    const bool wordUnderline = !f.stringProperty(kUnderlineWord).isEmpty()
        && f.underlineStyle() != QTextCharFormat::NoUnderline;
    if ((changed("ustyle") || changed("uword") || changed("ucolor"))
        && !(insertMark && f.underlineStyle() == QTextCharFormat::SingleUnderline && !wordUnderline)) {
        rPr.removeChildren(kW, u"u");
        static const QHash<int, QString> kNames{
            {QTextCharFormat::SingleUnderline, QStringLiteral("single")},
            {QTextCharFormat::DashUnderline, QStringLiteral("dash")},
            {QTextCharFormat::DotLine, QStringLiteral("dotted")},
            {QTextCharFormat::DashDotLine, QStringLiteral("dotDash")},
            {QTextCharFormat::DashDotDotLine, QStringLiteral("dotDotDash")},
            {QTextCharFormat::WaveUnderline, QStringLiteral("wave")},
            {QTextCharFormat::SpellCheckUnderline, QStringLiteral("single")}};
        const QString u = wordUnderline ? f.stringProperty(kUnderlineWord) : kNames.value(f.underlineStyle());
        if (!u.isEmpty()) {
            XmlElement &e = setChildVal(rPr, QStringLiteral("u"), u);
            if (f.hasProperty(QTextFormat::TextUnderlineColor) && f.underlineColor().isValid()) {
                e.setAttribute(kW, QStringLiteral("color"), f.underlineColor().name(QColor::HexRgb).mid(1).toUpper());
            }
        } else if (imported) {
            setChildVal(rPr, QStringLiteral("u"), QStringLiteral("none"));
        }
    }
    if (changed("dstrike") && f.boolProperty(kDoubleStrike) && f.fontStrikeOut()) {
        rPr.removeChildren(kW, u"strike");
        rPr.removeChildren(kW, u"dstrike");
        wChild(rPr, QStringLiteral("dstrike"));
    } else if (changed("dstrike") && imported && !f.boolProperty(kDoubleStrike)) {
        rPr.removeChildren(kW, u"dstrike");
    }
    if (changed("hidden")) {
        rPr.removeChildren(kW, u"vanish");
        if (f.boolProperty(kHiddenText)) {
            wChild(rPr, QStringLiteral("vanish"));
        } else if (imported) {
            setChildVal(rPr, QStringLiteral("vanish"), QStringLiteral("0"));
        }
    }
    if (changed("kern")) {
        rPr.removeChildren(kW, u"kern");
        if (const int kern = f.intProperty(kKerning); kern > 0) {
            setChildVal(rPr, QStringLiteral("kern"), QString::number(kern));
        }
    }
}

bool BodyWriter::listProperties(XmlElement &pPr, const QTextBlock &block, const QString &styleId,
                                const std::function<bool(const char *)> &changed)
{
    const XmlElement *styleNum = m_env.sourceStyles.styleNumbering(styleId);
    const QTextList *list = block.textList();
    if (list == nullptr) {
        // No longer numbered: numId 0 only where the style would number it.
        if (changed("inlist")) {
            pPr.removeChildren(kW, u"numPr");
            if (styleNum != nullptr) {
                setChildVal(wChild(pPr, QStringLiteral("numPr")), QStringLiteral("numId"), QStringLiteral("0"));
            }
        }
        return false;
    }
    const NumberingBuilder::Ref ref = m_env.numbering->refFor(list, m_document->indentWidth());
    const XmlElement *own = pPr.child(kW, u"numPr");
    const XmlElement *now = own != nullptr ? own : styleNum;
    if (now == nullptr || childVal(now, u"numId").toInt() != ref.numId
        || childVal(now, u"ilvl", QStringLiteral("0")).toInt() != ref.ilvl) {
        XmlElement &numPr = wChild(pPr, QStringLiteral("numPr"));
        numPr.children.clear();
        setChildVal(numPr, QStringLiteral("ilvl"), QString::number(ref.ilvl));
        setChildVal(numPr, QStringLiteral("numId"), QString::number(ref.numId));
    }
    const QTextListFormat lf = list->format();
    if (!isWordList(lf)) {
        return false;
    }
    // A Word list paragraph at its level's indents takes them from the
    // level, as Word writes it; its first-line offset is display only.
    if (changed("left") || changed("right") || changed("lhang") || changed("inlist")) {
        const QTextBlockFormat bf = block.blockFormat();
        const NumberingLevel &level = listDefinition(lf.stringProperty(kListDefinition))->levels[size_t(qBound(0, ref.ilvl, 8))];
        const int left = qRound(Units::pxToTwips(bf.leftMargin()));
        const int hanging = qRound(Units::pxToTwips(bf.doubleProperty(kListHanging)));
        const int right = qRound(Units::pxToTwips(bf.rightMargin()));
        pPr.removeChildren(kW, u"ind");
        if (qAbs(left - levelLeft(level)) > 1 || qAbs(hanging - levelHanging(level)) > 1 || right != 0) {
            XmlElement &ind = wChild(pPr, QStringLiteral("ind"));
            ind.setAttribute(kW, QStringLiteral("left"), QString::number(left));
            ind.setAttribute(kW, QStringLiteral("right"), QString::number(right));
            ind.setAttribute(kW, hanging >= 0 ? QStringLiteral("hanging") : QStringLiteral("firstLine"),
                             QString::number(qAbs(hanging)));
        }
    }
    return true;
}

} // namespace QindaOffice::Docx::detail
