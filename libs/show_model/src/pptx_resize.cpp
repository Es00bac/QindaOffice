// SPDX-License-Identifier: GPL-3.0-or-later
// A new slide size written into a package (pptx_resize.h).

#include "pptx_resize.h"

#include "pptx_context.h"

#include "ooxml_package.h"

#include <cmath>

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlElement;

QString scaled(const QString &value, qreal factor, qreal offset = 0)
{
    bool ok = false;
    const double v = value.toDouble(&ok);
    return ok ? QString::number(qint64(std::llround(v * factor + offset))) : value;
}

void scaleAttribute(XmlElement &e, QStringView name, qreal factor, qreal offset = 0)
{
    for (Ooxml::XmlAttribute &a : e.attributes) {
        if (a.ns.isEmpty() && a.name == name) {
            a.value = scaled(a.value, factor, offset);
        }
    }
}

// A transform's box: its offset about the slide's top-left, its extent.
void scaleTransform(XmlElement *xfrm, qreal factor, const QPointF &shift)
{
    if (xfrm == nullptr) {
        return;
    }
    for (Ooxml::XmlChild &c : xfrm->children) {
        if (!c.element || c.element->ns != nsA()) {
            continue;
        }
        if (c.element->name == u"off") {
            scaleAttribute(*c.element, u"x", factor, shift.x() * 12700.0);
            scaleAttribute(*c.element, u"y", factor, shift.y() * 12700.0);
        } else if (c.element->name == u"ext") {
            scaleAttribute(*c.element, u"cx", factor);
            scaleAttribute(*c.element, u"cy", factor);
        }
    }
}

XmlElement *childNamed(XmlElement &parent, QStringView name)
{
    for (Ooxml::XmlChild &c : parent.children) {
        if (c.element && c.element->name == name) {
            return c.element.get();
        }
    }
    return nullptr;
}

// One shape of a shape tree: its own box (a group's child space stays).
void scaleShape(XmlElement &shape, qreal factor, const QPointF &shift)
{
    const QString &name = shape.name;
    if (name == u"AlternateContent") {
        for (Ooxml::XmlChild &branch : shape.children) {
            if (branch.element) {
                for (Ooxml::XmlChild &inner : branch.element->children) {
                    if (inner.element) {
                        scaleShape(*inner.element, factor, shift);
                    }
                }
            }
        }
        return;
    }
    if (name == u"graphicFrame") {
        scaleTransform(childNamed(shape, u"xfrm"), factor, shift);
        return;
    }
    for (const char16_t *properties : {u"spPr", u"grpSpPr"}) {
        if (XmlElement *pr = childNamed(shape, properties)) {
            scaleTransform(pr->child(nsA(), u"xfrm"), factor, shift);
        }
    }
}

// Every length of text and tables anywhere under `e`.
void scaleText(XmlElement &e, qreal factor)
{
    const QString &name = e.name;
    if (e.ns == nsA()) {
        if (name == u"rPr" || name == u"defRPr" || name == u"endParaRPr") {
            scaleAttribute(e, u"sz", factor);
            scaleAttribute(e, u"spc", factor);
        } else if (name == u"pPr" || (name.startsWith(QLatin1String("lvl")) && name.endsWith(QLatin1String("pPr")))
                   || name == u"defPPr") {
            scaleAttribute(e, u"marL", factor);
            scaleAttribute(e, u"marR", factor);
            scaleAttribute(e, u"indent", factor);
            scaleAttribute(e, u"defTabSz", factor);
        } else if (name == u"spcPts" || name == u"buSzPts") {
            scaleAttribute(e, u"val", factor);
        } else if (name == u"tab") {
            scaleAttribute(e, u"pos", factor);
        } else if (name == u"bodyPr") {
            for (const char16_t *inset : {u"lIns", u"tIns", u"rIns", u"bIns"}) {
                scaleAttribute(e, inset, factor);
            }
        } else if (name == u"gridCol") {
            scaleAttribute(e, u"w", factor);
        } else if (name == u"tr") {
            scaleAttribute(e, u"h", factor);
        } else if (name == u"tcPr") {
            for (const char16_t *margin : {u"marL", u"marR", u"marT", u"marB"}) {
                scaleAttribute(e, margin, factor);
            }
        }
    }
    for (Ooxml::XmlChild &c : e.children) {
        if (c.element) {
            scaleText(*c.element, factor);
        }
    }
}

QByteArray scaledPart(const QByteArray &bytes, qreal factor, const QPointF &shift)
{
    const Ooxml::XmlElementPtr root = Ooxml::parseXml(bytes);
    if (!root) {
        return bytes;
    }
    if (XmlElement *cSld = root->child(nsP(), u"cSld")) {
        if (XmlElement *tree = cSld->child(nsP(), u"spTree")) {
            for (Ooxml::XmlChild &c : tree->children) {
                if (c.element && c.element->name != u"nvGrpSpPr" && c.element->name != u"grpSpPr" && c.element->name != u"extLst") {
                    scaleShape(*c.element, factor, shift);
                }
            }
        }
    }
    scaleText(*root, factor);
    return Ooxml::serializeDocument(*root);
}

} // namespace

void scalePackage(Ooxml::PackageWriter &package, const QSizeF &from, const QSizeF &to, qreal factor)
{
    const QPointF shift((to.width() - from.width() * factor) / 2.0, (to.height() - from.height() * factor) / 2.0);
    for (const QString &part : package.partNames()) {
        const QString type = package.contentTypeOf(part);
        const bool slideLike = type.endsWith(QLatin1String(".slide+xml")) || type.endsWith(QLatin1String(".slideLayout+xml"))
            || type.endsWith(QLatin1String(".slideMaster+xml"));
        if (slideLike) {
            package.addPart(part, scaledPart(package.partData(part), factor, shift), type);
        } else if (type.endsWith(QLatin1String("presentation.main+xml"))) {
            const Ooxml::XmlElementPtr root = Ooxml::parseXml(package.partData(part));
            if (!root) {
                continue;
            }
            if (XmlElement *size = root->child(nsP(), u"sldSz")) {
                size->setAttribute(QString(), QStringLiteral("cx"), QString::number(qint64(std::llround(to.width() * 12700.0))));
                size->setAttribute(QString(), QStringLiteral("cy"), QString::number(qint64(std::llround(to.height() * 12700.0))));
                size->removeAttribute(QString(), u"type");
            }
            if (XmlElement *defaults = root->child(nsP(), u"defaultTextStyle")) {
                scaleText(*defaults, factor);
            }
            package.addPart(part, Ooxml::serializeDocument(*root), type);
        }
    }
}

} // namespace QindaOffice::Show::Pptx
