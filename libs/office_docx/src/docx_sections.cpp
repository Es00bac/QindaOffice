// SPDX-License-Identifier: GPL-3.0-or-later
// Word sections (docx_sections.h): the settings a w:sectPr says, read,
// patched and made; page number formats; the display-only right margin.
// Sections in the body and their headers: docx_sections_body.cpp.

#include "docx_sections.h"

#include "docx_properties.h"
#include "docx_reader_p.h"
#include "docx_styles.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QRegularExpression>

namespace QindaOffice::Docx {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
namespace Units = Ooxml::Units;

namespace {

const QString kW = QString::fromUtf16(Ooxml::Ns::W);

const char *startName(SectionStart start)
{
    switch (start) {
    case SectionStart::Continuous:
        return "continuous";
    case SectionStart::EvenPage:
        return "evenPage";
    case SectionStart::OddPage:
        return "oddPage";
    case SectionStart::NextColumn:
        return "nextColumn";
    case SectionStart::NextPage:
        break;
    }
    return "nextPage";
}

SectionStart startFrom(const QString &name)
{
    if (name == u"continuous") {
        return SectionStart::Continuous;
    }
    if (name == u"evenPage") {
        return SectionStart::EvenPage;
    }
    if (name == u"oddPage") {
        return SectionStart::OddPage;
    }
    if (name == u"nextColumn") {
        return SectionStart::NextColumn;
    }
    return SectionStart::NextPage;
}

QString twips(qreal px)
{
    return QString::number(qRound(Units::pxToTwips(px)));
}

qreal px(const XmlElement *element, QStringView name, qreal fallbackTwips)
{
    return Units::twipsToPx(qAbs(Units::parseTwips(wAttr(element, name), fallbackTwips)));
}

XmlElement &child(XmlElement &parent, const QString &name)
{
    if (XmlElement *existing = parent.child(kW, name)) {
        return *existing;
    }
    return parent.appendElement(kW, name);
}

void writePage(XmlElement &sectPr, const PageLayout &now, const PageLayout &was, bool fresh)
{
    if (fresh || now.pageSize != was.pageSize) {
        XmlElement &size = child(sectPr, QStringLiteral("pgSz"));
        const QString code = size.attribute(kW, u"code");
        size.attributes.clear();
        size.setAttribute(kW, QStringLiteral("w"), twips(now.pageSize.width()));
        size.setAttribute(kW, QStringLiteral("h"), twips(now.pageSize.height()));
        if (now.landscape()) {
            size.setAttribute(kW, QStringLiteral("orient"), QStringLiteral("landscape"));
        }
        // A paper code names the paper, whichever way round it is used.
        if (!code.isEmpty() && (now.pageSize == was.pageSize || now.pageSize == was.pageSize.transposed())) {
            size.setAttribute(kW, QStringLiteral("code"), code);
        }
    }
    if (fresh || now.margins != was.margins || now.headerDistance != was.headerDistance
        || now.footerDistance != was.footerDistance || now.gutter != was.gutter) {
        XmlElement &margin = child(sectPr, QStringLiteral("pgMar"));
        margin.attributes.clear();
        margin.setAttribute(kW, QStringLiteral("top"), twips(now.margins.top()));
        margin.setAttribute(kW, QStringLiteral("right"), twips(now.margins.right()));
        margin.setAttribute(kW, QStringLiteral("bottom"), twips(now.margins.bottom()));
        margin.setAttribute(kW, QStringLiteral("left"), twips(now.margins.left()));
        margin.setAttribute(kW, QStringLiteral("header"), twips(now.headerDistance));
        margin.setAttribute(kW, QStringLiteral("footer"), twips(now.footerDistance));
        margin.setAttribute(kW, QStringLiteral("gutter"), twips(now.gutter));
    }
}

void writeNumbering(XmlElement &sectPr, const PageNumbering &now)
{
    XmlElement *type = sectPr.child(kW, u"pgNumType");
    const bool any = now.start >= 0 || !now.format.isEmpty() || now.chapterStyle > 0;
    if (!any) {
        sectPr.removeChildren(kW, u"pgNumType");
        return;
    }
    if (type == nullptr) {
        type = &sectPr.appendElement(kW, QStringLiteral("pgNumType"));
    }
    type->attributes.clear();
    if (!now.format.isEmpty() && now.format != u"decimal") {
        type->setAttribute(kW, QStringLiteral("fmt"), now.format);
    }
    if (now.start >= 0) {
        type->setAttribute(kW, QStringLiteral("start"), QString::number(now.start));
    }
    if (now.chapterStyle > 0) {
        type->setAttribute(kW, QStringLiteral("chapStyle"), QString::number(now.chapterStyle));
        if (!now.chapterSeparator.isEmpty()) {
            type->setAttribute(kW, QStringLiteral("chapSep"), now.chapterSeparator);
        }
    }
}

void writeColumns(XmlElement &sectPr, const SectionColumns &now, const SectionColumns &was)
{
    XmlElement &cols = child(sectPr, QStringLiteral("cols"));
    if (now.count != was.count) {
        // Columns of their own widths described the old count.
        cols.removeChildren(kW, u"col");
        cols.removeAttribute(kW, u"equalWidth");
    }
    cols.removeAttribute(kW, u"num");
    if (now.count > 1) {
        cols.setAttribute(kW, QStringLiteral("num"), QString::number(now.count));
    }
    cols.setAttribute(kW, QStringLiteral("space"), twips(now.space));
    cols.removeAttribute(kW, u"sep");
    if (now.separator) {
        cols.setAttribute(kW, QStringLiteral("sep"), QStringLiteral("1"));
    }
}

} // namespace

// ---- settings ------------------------------------------------------------------------

QVariantMap SectionSettings::toMap() const
{
    const PageLayout &l = layout;
    return {{QStringLiteral("w"), l.pageSize.width()},
            {QStringLiteral("h"), l.pageSize.height()},
            {QStringLiteral("left"), l.margins.left()},
            {QStringLiteral("top"), l.margins.top()},
            {QStringLiteral("right"), l.margins.right()},
            {QStringLiteral("bottom"), l.margins.bottom()},
            {QStringLiteral("header"), l.headerDistance},
            {QStringLiteral("footer"), l.footerDistance},
            {QStringLiteral("gutter"), l.gutter},
            {QStringLiteral("titlePg"), titlePage},
            {QStringLiteral("start"), int(flow.start)},
            {QStringLiteral("pgStart"), flow.numbering.start},
            {QStringLiteral("pgFmt"), flow.numbering.format},
            {QStringLiteral("chapStyle"), flow.numbering.chapterStyle},
            {QStringLiteral("chapSep"), flow.numbering.chapterSeparator},
            {QStringLiteral("cols"), flow.columns.count},
            {QStringLiteral("colSpace"), flow.columns.space},
            {QStringLiteral("colSep"), flow.columns.separator},
            {QStringLiteral("vAlign"), flow.verticalAlign}};
}

SectionSettings SectionSettings::fromMap(const QVariantMap &map)
{
    SectionSettings s;
    auto real = [&map](const char *key, qreal fallback) {
        return map.value(QString::fromLatin1(key), fallback).toReal();
    };
    PageLayout &l = s.layout;
    l.pageSize = QSizeF(real("w", l.pageSize.width()), real("h", l.pageSize.height()));
    l.margins = QMarginsF(real("left", l.margins.left()), real("top", l.margins.top()),
                          real("right", l.margins.right()), real("bottom", l.margins.bottom()));
    l.headerDistance = real("header", l.headerDistance);
    l.footerDistance = real("footer", l.footerDistance);
    l.gutter = real("gutter", l.gutter);
    s.titlePage = map.value(QStringLiteral("titlePg")).toBool();
    s.flow.start = SectionStart(qBound(0, map.value(QStringLiteral("start")).toInt(), 4));
    s.flow.numbering.start = map.value(QStringLiteral("pgStart"), -1).toInt();
    s.flow.numbering.format = map.value(QStringLiteral("pgFmt")).toString();
    s.flow.numbering.chapterStyle = map.value(QStringLiteral("chapStyle")).toInt();
    s.flow.numbering.chapterSeparator = map.value(QStringLiteral("chapSep")).toString();
    s.flow.columns.count = qMax(1, map.value(QStringLiteral("cols"), 1).toInt());
    s.flow.columns.space = real("colSpace", 48);
    s.flow.columns.separator = map.value(QStringLiteral("colSep")).toBool();
    s.flow.verticalAlign = map.value(QStringLiteral("vAlign")).toString();
    return s;
}

SectionSettings readSectionSettings(const XmlElement *sectPr)
{
    SectionSettings s;
    // Word's defaults for a section that says nothing: US Letter, 1-inch
    // margins, half-inch header and footer distances.
    s.layout.pageSize = QSizeF(816, 1056);
    s.layout.margins = QMarginsF(96, 96, 96, 96);
    s.layout.headerDistance = 48;
    s.layout.footerDistance = 48;
    if (sectPr == nullptr) {
        return s;
    }
    if (const XmlElement *size = sectPr->child(kW, u"pgSz")) {
        const qreal w = Units::parseTwips(wAttr(size, u"w"), 12240);
        const qreal h = Units::parseTwips(wAttr(size, u"h"), 15840);
        if (w > 0 && h > 0) {
            s.layout.pageSize = QSizeF(Units::twipsToPx(w), Units::twipsToPx(h));
        }
    }
    if (const XmlElement *margin = sectPr->child(kW, u"pgMar")) {
        s.layout.margins = QMarginsF(px(margin, u"left", 1440), px(margin, u"top", 1440), px(margin, u"right", 1440),
                                     px(margin, u"bottom", 1440));
        s.layout.headerDistance = px(margin, u"header", 720);
        s.layout.footerDistance = px(margin, u"footer", 720);
        s.layout.gutter = px(margin, u"gutter", 0);
    }
    s.titlePage = onOff(sectPr, u"titlePg");
    s.flow.start = startFrom(childVal(sectPr, u"type", QStringLiteral("nextPage")));
    if (const XmlElement *type = sectPr->child(kW, u"pgNumType")) {
        bool ok = false;
        const int start = wAttr(type, u"start").toInt(&ok);
        s.flow.numbering.start = ok ? qMax(0, start) : -1;
        s.flow.numbering.format = wAttr(type, u"fmt");
        if (s.flow.numbering.format == u"decimal") {
            s.flow.numbering.format.clear();
        }
        s.flow.numbering.chapterStyle = qBound(0, wAttr(type, u"chapStyle").toInt(), 9);
        s.flow.numbering.chapterSeparator = wAttr(type, u"chapSep");
    }
    if (const XmlElement *cols = sectPr->child(kW, u"cols")) {
        s.flow.columns.count = qMax(1, wAttr(cols, u"num", QStringLiteral("1")).toInt());
        s.flow.columns.space = Units::twipsToPx(Units::parseTwips(wAttr(cols, u"space"), 720));
        const QString sep = wAttr(cols, u"sep");
        s.flow.columns.separator = sep == u"1" || sep == u"true" || sep == u"on";
    }
    s.flow.verticalAlign = childVal(sectPr, u"vAlign");
    if (s.flow.verticalAlign == u"top") {
        s.flow.verticalAlign.clear();
    }
    return s;
}

void writeSectionSettings(XmlElement &sectPr, const SectionSettings &now, const SectionSettings &was)
{
    writePage(sectPr, now.layout, was.layout, false);
    if (now.flow.start != was.flow.start) {
        sectPr.removeChildren(kW, u"type");
        if (now.flow.start != SectionStart::NextPage) {
            sectPr.appendElement(kW, QStringLiteral("type"))
                .setAttribute(kW, QStringLiteral("val"), QString::fromLatin1(startName(now.flow.start)));
        }
    }
    if (now.titlePage != was.titlePage) {
        sectPr.removeChildren(kW, u"titlePg");
        if (now.titlePage) {
            sectPr.appendElement(kW, QStringLiteral("titlePg"));
        }
    }
    if (now.flow.numbering != was.flow.numbering) {
        writeNumbering(sectPr, now.flow.numbering);
    }
    if (now.flow.columns != was.flow.columns) {
        writeColumns(sectPr, now.flow.columns, was.flow.columns);
    }
    if (now.flow.verticalAlign != was.flow.verticalAlign) {
        sectPr.removeChildren(kW, u"vAlign");
        if (!now.flow.verticalAlign.isEmpty()) {
            sectPr.appendElement(kW, QStringLiteral("vAlign"))
                .setAttribute(kW, QStringLiteral("val"), now.flow.verticalAlign);
        }
    }
}

XmlElementPtr newSectionElement(const SectionSettings &settings)
{
    auto sectPr = std::make_shared<XmlElement>(kW, QStringLiteral("sectPr"));
    writePage(*sectPr, settings.layout, settings.layout, true);
    SectionColumns one;
    one.count = 0; // differs: written
    writeColumns(*sectPr, settings.flow.columns, one);
    sectPr->appendElement(kW, QStringLiteral("docGrid")).setAttribute(kW, QStringLiteral("linePitch"), QStringLiteral("360"));
    SectionSettings plain = settings;
    plain.flow = SectionFlow();
    plain.flow.columns = settings.flow.columns;
    plain.titlePage = false;
    writeSectionSettings(*sectPr, settings, plain);
    return sectPr;
}

// ---- page numbers --------------------------------------------------------------------

QString formatPageNumber(int number, const QString &format)
{
    if (format == u"numberInDash") {
        return QStringLiteral("- %1 -").arg(number);
    }
    if (format == u"lowerRoman" || format == u"upperRoman" || format == u"lowerLetter" || format == u"upperLetter"
        || format == u"decimalZero") {
        return detail::formatListNumber(qMax(1, number), format);
    }
    return QString::number(number);
}

QString fieldNumberFormat(const QString &instruction)
{
    static const QRegularExpression kSwitch(QStringLiteral("\\\\\\*\\s*(\\w+)"));
    for (auto it = kSwitch.globalMatch(instruction); it.hasNext();) {
        const QString name = it.next().captured(1);
        if (name == u"roman") {
            return QStringLiteral("lowerRoman");
        }
        if (name == u"Roman" || name == u"ROMAN") {
            return QStringLiteral("upperRoman");
        }
        if (name == u"alphabetic") {
            return QStringLiteral("lowerLetter");
        }
        if (name == u"ALPHABETIC" || name == u"Alphabetic") {
            return QStringLiteral("upperLetter");
        }
        if (name == u"Arabic" || name == u"arabic") {
            return QStringLiteral("decimal");
        }
    }
    return {};
}

// ---- the display-only right margin ---------------------------------------------------

namespace {

// A paragraph's right margin is a block property, a table's a frame one.
int rightKey(const QTextFormat &format)
{
    return format.isFrameFormat() ? int(QTextFormat::FrameRightMargin) : int(QTextFormat::BlockRightMargin);
}

qreal rightOf(const QTextFormat &format)
{
    return format.isFrameFormat() ? format.toFrameFormat().rightMargin()
                                  : format.doubleProperty(QTextFormat::BlockRightMargin);
}

void setRight(QTextFormat &format, qreal margin)
{
    if (qFuzzyIsNull(margin) && !format.isFrameFormat()) {
        format.clearProperty(QTextFormat::BlockRightMargin);
    } else {
        format.setProperty(rightKey(format), margin);
    }
}

} // namespace

qreal ownRightMargin(const QTextFormat &format)
{
    const qreal margin = rightOf(format);
    const QVariantList pad = format.property(kSectionPad).toList();
    if (pad.size() == 2 && qAbs(margin - pad.at(1).toReal()) < 0.01) {
        return margin - pad.at(0).toReal();
    }
    return margin;
}

void setOwnRightMargin(QTextFormat &format, qreal margin)
{
    const QVariantList pad = format.property(kSectionPad).toList();
    const qreal extra = pad.size() == 2 ? pad.at(0).toReal() : 0.0;
    setRight(format, margin + extra);
    if (extra > 0) {
        format.setProperty(kSectionPad, QVariantList{extra, margin + extra});
    }
}

void setSectionPad(QTextFormat &format, qreal pad)
{
    const qreal own = ownRightMargin(format);
    if (pad <= 0.01) {
        format.clearProperty(kSectionPad);
        setRight(format, own);
        return;
    }
    setRight(format, own + pad);
    format.setProperty(kSectionPad, QVariantList{pad, own + pad});
}

} // namespace QindaOffice::Docx
