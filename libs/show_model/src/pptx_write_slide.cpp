// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_write_slide.h"

#include "pptx_timing.h"
#include "pptx_write_parts.h"

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

XmlElementPtr make(const QString &ns, const QString &name)
{
    return std::make_shared<XmlElement>(ns, name);
}

bool isTransition(const XmlElement &e)
{
    return e.is(nsP(), u"transition") || (e.is(nsMc(), u"AlternateContent") && e.descendant(nsP(), u"transition") != nullptr);
}

XmlElementPtr defaultGroupProperties(const char *name)
{
    if (QLatin1String(name) == QLatin1String("nvGrpSpPr")) {
        XmlElementPtr nv = make(nsP(), QStringLiteral("nvGrpSpPr"));
        XmlElement &cNvPr = nv->appendElement(nsP(), QStringLiteral("cNvPr"));
        cNvPr.setAttribute(QString(), QStringLiteral("id"), QStringLiteral("1"));
        cNvPr.setAttribute(QString(), QStringLiteral("name"), QString());
        nv->appendElement(nsP(), QStringLiteral("cNvGrpSpPr"));
        nv->appendElement(nsP(), QStringLiteral("nvPr"));
        return nv;
    }
    XmlElementPtr pr = make(nsP(), QStringLiteral("grpSpPr"));
    XmlElement &xfrm = pr->appendElement(nsA(), QStringLiteral("xfrm"));
    for (const auto &[name2, a, b] : {std::tuple{"off", "x", "y"}, std::tuple{"ext", "cx", "cy"},
                                      std::tuple{"chOff", "x", "y"}, std::tuple{"chExt", "cx", "cy"}}) {
        XmlElement &e = xfrm.appendElement(nsA(), QLatin1String(name2));
        e.setAttribute(QString(), QLatin1String(a), QStringLiteral("0"));
        e.setAttribute(QString(), QLatin1String(b), QStringLiteral("0"));
    }
    return pr;
}

XmlElementPtr carried(PartWriter &out, const XmlElement *element, const QString &sourcePart)
{
    XmlElementPtr copy = element->clone();
    out.carry(*copy, sourcePart);
    return copy;
}

QList<BuildEntry> sorted(QList<BuildEntry> builds)
{
    std::sort(builds.begin(), builds.end(), [](const BuildEntry &a, const BuildEntry &b) { return a.shapeId < b.shapeId; });
    return builds;
}

} // namespace

XmlElementPtr slideElement(PartWriter &out, const Slide &slide)
{
    const SlideSource *source = slide.origin.get();
    const bool carry = source != nullptr && source->xml && out.session.copier != nullptr;
    const XmlElement *sourceCSld = carry ? source->xml->child(nsP(), u"cSld") : nullptr;
    XmlElementPtr sld;
    if (carry) {
        sld = source->xml->clone();
        sld->children.removeIf([](const Ooxml::XmlChild &c) {
            return c.element && (c.element->is(nsP(), u"cSld") || c.element->is(nsP(), u"timing") || isTransition(*c.element));
        });
        out.carry(*sld, source->part);
    } else {
        sld = make(nsP(), QStringLiteral("sld"));
    }
    if (slide.hidden) {
        sld->setAttribute(QString(), QStringLiteral("show"), QStringLiteral("0"));
    } else {
        sld->removeAttribute(QString(), u"show");
    }
    if (!slide.showMasterShapes) {
        sld->setAttribute(QString(), QStringLiteral("showMasterSp"), QStringLiteral("0"));
    } else {
        sld->removeAttribute(QString(), u"showMasterSp");
    }

    // p:cSld: the source's own attributes and trailing children around the
    // rebuilt background and shape tree.
    XmlElementPtr cSld = sourceCSld != nullptr ? carried(out, sourceCSld, source->part) : make(nsP(), QStringLiteral("cSld"));
    cSld->children.removeIf([](const Ooxml::XmlChild &c) {
        return c.element && (c.element->is(nsP(), u"bg") || c.element->is(nsP(), u"spTree"));
    });
    const XmlElement *sourceTree = sourceCSld != nullptr ? sourceCSld->child(nsP(), u"spTree") : nullptr;
    XmlElementPtr tree = make(nsP(), QStringLiteral("spTree"));
    for (const char *name : {"nvGrpSpPr", "grpSpPr"}) {
        const XmlElement *own = sourceTree != nullptr ? sourceTree->child(nsP(), QString::fromLatin1(name)) : nullptr;
        tree->appendElement(own != nullptr ? carried(out, own, source->part) : defaultGroupProperties(name));
    }
    const QList<bool> keeps = reserveShapeIds(out, slide.shapes);
    QList<quint32> written;
    for (qsizetype i = 0; i < slide.shapes.size(); ++i) {
        const Shape &shape = slide.shapes.at(i);
        const quint32 id = out.claim(keeps.at(i) ? shape.nvId : 0);
        XmlElementPtr element = writeShape(out, shape, id);
        written.append(element ? id : 0);
        if (element) {
            tree->appendElement(element);
        }
    }
    if (const XmlElement *ext = sourceTree != nullptr ? sourceTree->child(nsP(), u"extLst") : nullptr) {
        tree->appendElement(carried(out, ext, source->part));
    }
    QList<Ooxml::XmlChild> cSldChildren;
    if (slide.background.set) {
        const XmlElement *sourceBg = sourceCSld != nullptr ? sourceCSld->child(nsP(), u"bg") : nullptr;
        XmlElementPtr bg = sourceBg != nullptr && source->background == slide.background ? carried(out, sourceBg, source->part)
                                                                                           : backgroundElement(out, slide.background);
        cSldChildren.append({bg, QString()});
    }
    cSldChildren.append({tree, QString()});
    cSldChildren.append(cSld->children);
    cSld->children = cSldChildren;

    // The colour map override (kept), then the transition and the timing.
    XmlElementPtr colorMap;
    QList<Ooxml::XmlChild> rest;
    for (const Ooxml::XmlChild &c : std::as_const(sld->children)) {
        if (c.element && c.element->is(nsP(), u"clrMapOvr")) {
            colorMap = c.element;
        } else if (c.element) {
            rest.append(c);
        }
    }
    if (!colorMap) {
        colorMap = make(nsP(), QStringLiteral("clrMapOvr"));
        colorMap->appendElement(nsA(), QStringLiteral("masterClrMapping"));
    }
    XmlElementPtr transition;
    if (carry && source->transitionXml && slide.transition == source->transition) {
        transition = carried(out, source->transitionXml.get(), source->part);
    } else {
        transition = transitionElement(slide.transition);
    }
    // Builds: the source's timing while the builds are what was read.
    QList<BuildEntry> current;
    QList<BuildEntry> expected;
    for (qsizetype i = 0; i < slide.shapes.size(); ++i) {
        const Shape &shape = slide.shapes.at(i);
        if (shape.buildStep > 0 && written.at(i) != 0) {
            current.append({written.at(i), shape.buildStep, shape.animation, shape.animationMs});
        }
        if (carry && written.at(i) != 0 && written.at(i) == shape.nvId) {
            for (const BuildEntry &build : source->builds) {
                if (build.shapeId == shape.nvId) {
                    const int ms = build.durationMs > 0 ? qBound(50, build.durationMs, 5000) : 350;
                    expected.append({build.shapeId, build.step, build.animation, ms});
                }
            }
        }
    }
    XmlElementPtr timing;
    if (carry && source->timingXml && sorted(current) == sorted(expected)) {
        timing = pruneTiming(*source->timingXml, out.usedIds());
        if (timing) {
            out.carry(*timing, source->part);
        }
    } else {
        timing = timingElement(current);
    }
    sld->children.clear();
    sld->appendElement(cSld);
    sld->appendElement(colorMap);
    if (transition) {
        sld->appendElement(transition);
    }
    if (timing) {
        sld->appendElement(timing);
    }
    sld->children.append(rest);
    return sld;
}

XmlElementPtr notesElement(PartWriter &out, const Slide &slide)
{
    if (slide.notes.isEmpty()) {
        return nullptr;
    }
    const SlideSource *source = slide.origin.get();
    if (source != nullptr && !source->notesPart.isEmpty() && source->notes == slide.notes && out.session.copier != nullptr) {
        if (XmlElementPtr root = out.session.source->xml(source->notesPart)) {
            out.carry(*root, source->notesPart);
            return root;
        }
    }
    return notesSlideElement(slide.notes);
}

void carryImplicitRelationships(PartWriter &out, const Slide &slide)
{
    const SlideSource *source = slide.origin.get();
    if (source == nullptr || out.session.copier == nullptr) {
        return;
    }
    for (const Ooxml::Relationship &rel : out.session.source->relationships(source->part)) {
        if (rel.type.endsWith(QLatin1String("/comments"))) {
            out.session.copier->copyRelationship(source->part, rel.id, out.part);
        }
    }
}

} // namespace QindaOffice::Show::Pptx
