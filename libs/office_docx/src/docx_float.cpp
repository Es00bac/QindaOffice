// SPDX-License-Identifier: GPL-3.0-or-later
// Floating pictures as Word writes them: PictureFloat ⇄ wp:anchor. The
// display (Qt's floating frames, the picture handler, the painter) is in
// docx_float_display.cpp.

#include "docx_float.h"

#include "docx_properties.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"

#include <QHash>
#include <QTextCharFormat>

namespace QindaOffice::Docx {

using Ooxml::XmlChild;
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
namespace Units = Ooxml::Units;

namespace {

const QString kWP = QString::fromUtf16(Ooxml::Ns::WP);
const QString kA = QString::fromUtf16(Ooxml::Ns::A);

QString emu(qreal px)
{
    return QString::number(Units::roundToInt64(Units::pxToEmu(px)));
}

qreal px(const QString &emuText)
{
    return Units::emuToPx(Units::parseEmu(emuText));
}

bool flag(const XmlElement &e, QStringView name, bool fallback)
{
    const QString v = e.attr(name);
    if (v.isEmpty()) {
        return fallback;
    }
    return v == u"1" || v == u"true" || v == u"on";
}

void readPosition(const XmlElement *position, QString *relative, QString *align, qreal *offset)
{
    if (position == nullptr) {
        return;
    }
    *relative = position->attr(u"relativeFrom", *relative);
    if (const XmlElement *a = position->child(kWP, u"align")) {
        *align = a->text().trimmed();
    } else if (const XmlElement *o = position->child(kWP, u"posOffset")) {
        align->clear();
        *offset = px(o->text().trimmed());
    }
}

XmlElementPtr position(const QString &name, const QString &relative, const QString &align, qreal offset)
{
    auto e = std::make_shared<XmlElement>(kWP, name);
    e->setAttribute(QString(), QStringLiteral("relativeFrom"), relative);
    if (!align.isEmpty()) {
        e->appendElement(kWP, QStringLiteral("align")).appendText(align);
    } else {
        e->appendElement(kWP, QStringLiteral("posOffset")).appendText(emu(offset));
    }
    return e;
}

// Word's wrap element; a tight/through polygon the source had is kept.
XmlElementPtr wrapElement(const PictureFloat &layout, const XmlElement *old)
{
    static const QHash<QString, QString> kNames{{QStringLiteral("square"), QStringLiteral("wrapSquare")},
                                                {QStringLiteral("tight"), QStringLiteral("wrapTight")},
                                                {QStringLiteral("through"), QStringLiteral("wrapThrough")},
                                                {QStringLiteral("topAndBottom"), QStringLiteral("wrapTopAndBottom")}};
    const QString name = kNames.value(layout.wrap, QStringLiteral("wrapNone"));
    auto e = std::make_shared<XmlElement>(kWP, name);
    if (name == u"wrapSquare" || name == u"wrapTight" || name == u"wrapThrough") {
        e->setAttribute(QString(), QStringLiteral("wrapText"), layout.wrapText);
    }
    if (name == u"wrapTight" || name == u"wrapThrough") {
        const XmlElement *polygon = old != nullptr ? old->child(kWP, u"wrapPolygon") : nullptr;
        if (polygon != nullptr) {
            e->appendElement(polygon->clone());
        } else {
            XmlElement &p = e->appendElement(kWP, QStringLiteral("wrapPolygon"));
            p.setAttribute(QString(), QStringLiteral("edited"), QStringLiteral("0"));
            const int points[][2] = {{0, 0}, {0, 21600}, {21600, 21600}, {21600, 0}, {0, 0}};
            for (int i = 0; i < 5; ++i) {
                p.appendElement(kWP, i == 0 ? QStringLiteral("start") : QStringLiteral("lineTo"))
                    .setAttribute(QString(), QStringLiteral("x"), QString::number(points[i][0]))
                    .setAttribute(QString(), QStringLiteral("y"), QString::number(points[i][1]));
            }
        }
    }
    return e;
}

XmlElementPtr childPtr(const XmlElement &parent, QStringView ns, QStringView name)
{
    for (const XmlChild &c : parent.children) {
        if (c.element && c.element->ns == ns && c.element->name == name) {
            return c.element;
        }
    }
    return nullptr;
}

bool isWrap(const XmlElement &e)
{
    return e.ns == kWP && e.name.startsWith(u"wrap");
}

} // namespace

QVariantMap PictureFloat::toMap() const
{
    return {{QStringLiteral("wrap"), wrap},          {QStringLiteral("wrapText"), wrapText},
            {QStringLiteral("hRelative"), hRelative}, {QStringLiteral("hAlign"), hAlign},
            {QStringLiteral("hOffset"), qRound(hOffset * 100) / 100.0}, {QStringLiteral("vRelative"), vRelative},
            {QStringLiteral("vAlign"), vAlign},        {QStringLiteral("vOffset"), qRound(vOffset * 100) / 100.0},
            {QStringLiteral("distTop"), qRound(distTop * 100) / 100.0},
            {QStringLiteral("distBottom"), qRound(distBottom * 100) / 100.0},
            {QStringLiteral("distLeft"), qRound(distLeft * 100) / 100.0},
            {QStringLiteral("distRight"), qRound(distRight * 100) / 100.0},
            {QStringLiteral("allowOverlap"), allowOverlap}, {QStringLiteral("lockAnchor"), lockAnchor},
            {QStringLiteral("layoutInCell"), layoutInCell}, {QStringLiteral("relativeHeight"), relativeHeight}};
}

PictureFloat PictureFloat::fromMap(const QVariantMap &map)
{
    PictureFloat p;
    p.wrap = map.value(QStringLiteral("wrap"), p.wrap).toString();
    p.wrapText = map.value(QStringLiteral("wrapText"), p.wrapText).toString();
    p.hRelative = map.value(QStringLiteral("hRelative"), p.hRelative).toString();
    p.hAlign = map.value(QStringLiteral("hAlign")).toString();
    p.hOffset = map.value(QStringLiteral("hOffset")).toReal();
    p.vRelative = map.value(QStringLiteral("vRelative"), p.vRelative).toString();
    p.vAlign = map.value(QStringLiteral("vAlign")).toString();
    p.vOffset = map.value(QStringLiteral("vOffset")).toReal();
    p.distTop = map.value(QStringLiteral("distTop")).toReal();
    p.distBottom = map.value(QStringLiteral("distBottom")).toReal();
    p.distLeft = map.value(QStringLiteral("distLeft"), p.distLeft).toReal();
    p.distRight = map.value(QStringLiteral("distRight"), p.distRight).toReal();
    p.allowOverlap = map.value(QStringLiteral("allowOverlap"), true).toBool();
    p.lockAnchor = map.value(QStringLiteral("lockAnchor")).toBool();
    p.layoutInCell = map.value(QStringLiteral("layoutInCell"), true).toBool();
    p.relativeHeight = map.value(QStringLiteral("relativeHeight"), p.relativeHeight).toString();
    return p;
}

PictureFloat pictureFloat(const QTextCharFormat &format)
{
    return format.hasProperty(kPictureFloat) ? PictureFloat::fromMap(format.property(kPictureFloat).toMap())
                                             : PictureFloat();
}

PictureFloat readPictureFloat(const XmlElement &frame)
{
    PictureFloat p;
    if (frame.name != u"anchor") {
        return p;
    }
    p.distTop = px(frame.attr(u"distT"));
    p.distBottom = px(frame.attr(u"distB"));
    p.distLeft = px(frame.attr(u"distL"));
    p.distRight = px(frame.attr(u"distR"));
    p.allowOverlap = flag(frame, u"allowOverlap", true);
    p.lockAnchor = flag(frame, u"locked", false);
    p.layoutInCell = flag(frame, u"layoutInCell", true);
    p.relativeHeight = frame.attr(u"relativeHeight", p.relativeHeight);
    const bool behind = flag(frame, u"behindDoc", false);
    p.wrap = behind ? QStringLiteral("behind") : QStringLiteral("inFront");
    for (const XmlElement *e : frame.elements()) {
        if (!isWrap(*e)) {
            continue;
        }
        if (e->name == u"wrapSquare") {
            p.wrap = QStringLiteral("square");
        } else if (e->name == u"wrapTight") {
            p.wrap = QStringLiteral("tight");
        } else if (e->name == u"wrapThrough") {
            p.wrap = QStringLiteral("through");
        } else if (e->name == u"wrapTopAndBottom") {
            p.wrap = QStringLiteral("topAndBottom");
        }
        p.wrapText = e->attr(u"wrapText", p.wrapText);
    }
    if (flag(frame, u"simplePos", false)) {
        // Absolute page position (wp:simplePos), rare: Word's older form.
        const XmlElement *simple = frame.child(kWP, u"simplePos");
        p.hRelative = QStringLiteral("page");
        p.vRelative = QStringLiteral("page");
        p.hOffset = simple != nullptr ? px(simple->attr(u"x")) : 0;
        p.vOffset = simple != nullptr ? px(simple->attr(u"y")) : 0;
        return p;
    }
    readPosition(frame.child(kWP, u"positionH"), &p.hRelative, &p.hAlign, &p.hOffset);
    readPosition(frame.child(kWP, u"positionV"), &p.vRelative, &p.vAlign, &p.vOffset);
    return p;
}

void writePictureFloat(XmlElement &drawing, const PictureFloat &layout, const QString &cx, const QString &cy)
{
    qsizetype index = -1;
    for (qsizetype i = 0; i < drawing.children.size(); ++i) {
        const XmlElementPtr &e = drawing.children.at(i).element;
        if (e && e->ns == kWP && (e->name == u"inline" || e->name == u"anchor")) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        return;
    }
    const XmlElementPtr frame = drawing.children.at(index).element;
    auto part = [&frame](QStringView ns, QStringView name) { return childPtr(*frame, ns, name); };
    auto extent = [&] {
        if (XmlElementPtr e = part(kWP, u"extent")) {
            return e;
        }
        auto e = std::make_shared<XmlElement>(kWP, QStringLiteral("extent"));
        e->setAttribute(QString(), QStringLiteral("cx"), cx);
        e->setAttribute(QString(), QStringLiteral("cy"), cy);
        return e;
    };
    XmlElementPtr out;
    if (!layout.isFloating()) {
        if (frame->name == u"inline") {
            return;
        }
        out = std::make_shared<XmlElement>(kWP, QStringLiteral("inline"));
        for (const char *attr : {"distT", "distB", "distL", "distR"}) {
            out->setAttribute(QString(), QString::fromLatin1(attr), QStringLiteral("0"));
        }
        out->appendElement(extent());
        for (const char16_t *name : {u"effectExtent", u"docPr", u"cNvGraphicFramePr"}) {
            if (XmlElementPtr e = part(kWP, name)) {
                out->appendElement(e);
            }
        }
        if (XmlElementPtr graphic = part(kA, u"graphic")) {
            out->appendElement(graphic);
        }
    } else {
        out = std::make_shared<XmlElement>(kWP, QStringLiteral("anchor"));
        out->setAttribute(QString(), QStringLiteral("distT"), emu(layout.distTop));
        out->setAttribute(QString(), QStringLiteral("distB"), emu(layout.distBottom));
        out->setAttribute(QString(), QStringLiteral("distL"), emu(layout.distLeft));
        out->setAttribute(QString(), QStringLiteral("distR"), emu(layout.distRight));
        out->setAttribute(QString(), QStringLiteral("simplePos"), QStringLiteral("0"));
        out->setAttribute(QString(), QStringLiteral("relativeHeight"), layout.relativeHeight);
        out->setAttribute(QString(), QStringLiteral("behindDoc"), layout.wrap == u"behind" ? QStringLiteral("1") : QStringLiteral("0"));
        out->setAttribute(QString(), QStringLiteral("locked"), layout.lockAnchor ? QStringLiteral("1") : QStringLiteral("0"));
        out->setAttribute(QString(), QStringLiteral("layoutInCell"), layout.layoutInCell ? QStringLiteral("1") : QStringLiteral("0"));
        out->setAttribute(QString(), QStringLiteral("allowOverlap"), layout.allowOverlap ? QStringLiteral("1") : QStringLiteral("0"));
        for (const Ooxml::XmlAttribute &a : frame->attributes) {
            if (!a.ns.isEmpty()) {
                out->setAttribute(a.ns, a.name, a.value); // wp14:anchorId, wp14:editId
            }
        }
        XmlElement &simple = out->appendElement(kWP, QStringLiteral("simplePos"));
        simple.setAttribute(QString(), QStringLiteral("x"), QStringLiteral("0"));
        simple.setAttribute(QString(), QStringLiteral("y"), QStringLiteral("0"));
        out->appendElement(position(QStringLiteral("positionH"), layout.hRelative, layout.hAlign, layout.hOffset));
        out->appendElement(position(QStringLiteral("positionV"), layout.vRelative, layout.vAlign, layout.vOffset));
        out->appendElement(extent());
        if (XmlElementPtr effect = part(kWP, u"effectExtent")) {
            out->appendElement(effect);
        } else {
            XmlElement &e = out->appendElement(kWP, QStringLiteral("effectExtent"));
            for (const char *side : {"l", "t", "r", "b"}) {
                e.setAttribute(QString(), QString::fromLatin1(side), QStringLiteral("0"));
            }
        }
        const XmlElement *oldWrap = nullptr;
        for (const XmlElement *e : frame->elements()) {
            oldWrap = isWrap(*e) ? e : oldWrap;
        }
        out->appendElement(wrapElement(layout, oldWrap));
        if (XmlElementPtr docPr = part(kWP, u"docPr")) {
            out->appendElement(docPr);
        }
        if (XmlElementPtr locks = part(kWP, u"cNvGraphicFramePr")) {
            out->appendElement(locks);
        } else {
            out->appendElement(kWP, QStringLiteral("cNvGraphicFramePr"))
                .appendElement(kA, QStringLiteral("graphicFrameLocks"))
                .setAttribute(QString(), QStringLiteral("noChangeAspect"), QStringLiteral("1"));
        }
        if (XmlElementPtr graphic = part(kA, u"graphic")) {
            out->appendElement(graphic);
        }
        // What follows the graphic in Word's anchors (wp14:sizeRelH/V) stays.
        bool afterGraphic = false;
        for (const XmlChild &c : frame->children) {
            if (c.element && afterGraphic) {
                out->appendElement(c.element);
            }
            afterGraphic = afterGraphic || (c.element && c.element->ns == kA && c.element->name == u"graphic");
        }
    }
    drawing.children[index].element = out;
}

QTextFrameFormat::Position floatPosition(const PictureFloat &layout)
{
    if (!layout.isFloating() || layout.overText()) {
        return QTextFrameFormat::InFlow;
    }
    return layout.hAlign == u"right" || layout.hAlign == u"outside" ? QTextFrameFormat::FloatRight
                                                                    : QTextFrameFormat::FloatLeft;
}

} // namespace QindaOffice::Docx
