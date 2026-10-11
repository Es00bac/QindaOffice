// SPDX-License-Identifier: GPL-3.0-or-later
// What every pptx shape reader shares (pptx_read_shapes.h): the master's
// text styles by placeholder type, placeholder matching (layout by index,
// master by type), the placeholder templates of a layout or master, the
// transform and non-visual properties, unique model ids, provenance, and
// the read report's counts.

#include "pptx_read_shapes.h"

#include "pptx_origin.h"
#include "pptx_reader.h"

#include "ooxml_units.h"

namespace QindaOffice::Show::Pptx {

namespace {

bool isTitle(const QString &type)
{
    return type == u"title" || type == u"ctrTitle";
}

} // namespace

const ListStyle &MasterTextStyles::forPlaceholder(const QString &type) const
{
    if (isTitle(type)) {
        return title;
    }
    if (type == u"dt" || type == u"ftr" || type == u"sldNum" || type == u"hdr") {
        return other;
    }
    return body;
}

const PlaceholderTemplate *PlaceholderSet::match(const Placeholder &ph) const
{
    if (ph.index >= 0) {
        for (const PlaceholderTemplate &item : m_items) {
            if (item.ph.index == ph.index) {
                return &item;
            }
        }
    }
    for (const PlaceholderTemplate &item : m_items) {
        if (item.ph.type == ph.type || (isTitle(item.ph.type) && isTitle(ph.type))) {
            return &item;
        }
    }
    if (ph.type == u"obj" || ph.type == u"body") {
        for (const PlaceholderTemplate &item : m_items) {
            if (item.ph.type == u"body" || item.ph.type == u"obj") {
                return &item;
            }
        }
    }
    return nullptr;
}

const PlaceholderTemplate *PlaceholderSet::matchType(const QString &type) const
{
    const bool exact = type == u"dt" || type == u"ftr" || type == u"sldNum" || type == u"hdr";
    for (const PlaceholderTemplate &item : m_items) {
        if (isTitle(type) ? isTitle(item.ph.type) : exact ? item.ph.type == type : item.ph.type == u"body") {
            return &item;
        }
    }
    return nullptr;
}

PlaceholderSet collectPlaceholders(const PartContext &ctx, const Ooxml::XmlElement &spTree)
{
    PlaceholderSet set;
    for (const Ooxml::XmlChild &child : spTree.children) {
        if (!child.element || child.element->name != u"sp") {
            continue;
        }
        const Ooxml::XmlElement &sp = *child.element;
        const Ooxml::XmlElement *nv = sp.child(sp.ns, u"nvSpPr");
        const Ooxml::XmlElement *nvPr = nv != nullptr ? nv->child(sp.ns, u"nvPr") : nullptr;
        const Ooxml::XmlElement *ph = nvPr != nullptr ? nvPr->child(nsP(), u"ph") : nullptr;
        if (ph == nullptr) {
            continue;
        }
        PlaceholderTemplate t;
        t.ph.type = ph->attr(u"type", QStringLiteral("obj"));
        t.ph.index = ph->hasAttribute(QString(), u"idx") ? ph->attr(u"idx").toLongLong() : -1;
        t.element = child.element;
        t.ctx = ctx;
        Shape geometry;
        const Ooxml::XmlElement *spPr = sp.child(sp.ns, u"spPr");
        t.hasXfrm = readTransform(spPr != nullptr ? spPr->child(nsA(), u"xfrm") : nullptr, geometry);
        t.rect = geometry.rect;
        t.rotation = geometry.rotation;
        t.flipH = geometry.flipH;
        t.flipV = geometry.flipV;
        const Ooxml::XmlElement *txBody = sp.child(sp.ns, u"txBody");
        t.lstStyle = parseListStyle(ctx, txBody != nullptr ? txBody->child(nsA(), u"lstStyle") : nullptr);
        set.add(std::move(t));
    }
    return set;
}

bool readTransform(const Ooxml::XmlElement *xfrm, Shape &shape)
{
    if (xfrm == nullptr) {
        return false;
    }
    const Ooxml::XmlElement *off = xfrm->child(nsA(), u"off");
    const Ooxml::XmlElement *ext = xfrm->child(nsA(), u"ext");
    shape.rect = QRectF(off != nullptr ? emuToPt(off->attr(u"x")) : 0.0, off != nullptr ? emuToPt(off->attr(u"y")) : 0.0,
                        ext != nullptr ? emuToPt(ext->attr(u"cx")) : 0.0, ext != nullptr ? emuToPt(ext->attr(u"cy")) : 0.0);
    shape.rotation = xfrm->attr(u"rot").toDouble() / 60000.0;
    shape.flipH = Ooxml::Units::parseOnOff(xfrm->attr(u"flipH"), false);
    shape.flipV = Ooxml::Units::parseOnOff(xfrm->attr(u"flipV"), false);
    return off != nullptr || ext != nullptr;
}

void readNonVisual(const ShapeScope &scope, const Ooxml::XmlElement *cNvPr, Shape &shape)
{
    if (cNvPr == nullptr) {
        shape.id = shapeIdFor(scope, 0);
        return;
    }
    shape.nvId = cNvPr->attr(u"id").toUInt();
    shape.id = shapeIdFor(scope, shape.nvId);
    shape.name = cNvPr->attr(u"name");
    shape.description = cNvPr->attr(u"descr");
    shape.hidden = Ooxml::Units::parseOnOff(cNvPr->attr(u"hidden"), false);
    // M91 (D-099): action settings on click and on mouse-over.
    shape.click = actionFrom(scope.ctx, cNvPr->child(nsA(), u"hlinkClick"));
    shape.hover = actionFrom(scope.ctx, cNvPr->child(nsA(), u"hlinkHover"));
}

QString shapeIdFor(const ShapeScope &scope, quint32 nvId)
{
    QString id = scope.idPrefix + QString::number(nvId);
    if (scope.usedIds != nullptr) {
        int suffix = 1;
        const QString base = id;
        while (scope.usedIds->contains(id)) {
            id = base + QLatin1Char('_') + QString::number(++suffix);
        }
        scope.usedIds->insert(id);
    }
    return id;
}

void recordOrigin(const ShapeScope &scope, const Ooxml::XmlElementPtr &element, Shape &shape)
{
    if (!scope.keepOrigin) {
        return;
    }
    auto source = std::make_shared<ShapeSource>();
    source->part = scope.ctx.part;
    source->xml = element;
    source->pristine = shape; // provenance not yet set: no cycle
    shape.origin.source = std::move(source);
}

void countShape(const ShapeScope &scope, const Shape &shape)
{
    if (scope.report == nullptr) {
        return;
    }
    static const char *const kNames[] = {"text box", "rectangle", "ellipse", "line", "arrow", "picture",
                                         "autoshape", "table", "chart", "group", "preserved"};
    ++scope.report->shapes;
    ++scope.report->kinds[QLatin1String(kNames[int(shape.kind)])];
    if (shape.kind == ShapeKind::Preserved) {
        ++scope.report->preserved;
    }
}

} // namespace QindaOffice::Show::Pptx
