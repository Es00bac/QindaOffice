// SPDX-License-Identifier: GPL-3.0-or-later
// One page's shapes → model shapes and connectors, and the page's glue
// (visio_read_p.h).
#include "visio_read_p.h"

#include "geometry.h"
#include "ooxml_namespaces.h"
#include "shape_path.h"

#include <QBuffer>
#include <QImage>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Visio {

namespace {

QJsonObject keyObject(const QString &token, const QString &key)
{
    return {{QStringLiteral("src"), token}, {QStringLiteral("key"), key}};
}

void collectIds(const XmlElement &shape, QList<int> &ids)
{
    ids.append(intAttribute(shape, u"ID", -1));
    if (const XmlElement *nested = child(shape, u"Shapes")) {
        for (const XmlElement *s : children(*nested, u"Shape")) {
            collectIds(*s, ids);
        }
    }
}

// The picture of a <ForeignData>: bytes the model can draw (cropped to the
// shape when Visio crops), or empty with `kind` saying why not.
QByteArray pictureOf(const ReadContext &ctx, const XmlElement &foreign, const QStringList &parts, const Lookup &l,
                     const QRectF &box, QString *kind)
{
    *kind = foreign.attribute({}, u"ForeignType");
    const XmlElement *rel = child(foreign, u"Rel");
    const QString id = rel != nullptr ? rel->attribute(Ooxml::Ns::R, u"id") : QString();
    QByteArray bytes;
    for (const QString &part : parts) {
        if (const auto r = ctx.package->relationship(part, id)) {
            bytes = ctx.package->data(ctx.package->targetPart(part, *r));
            break;
        }
    }
    QImage image = bytes.isEmpty() ? QImage() : QImage::fromData(bytes);
    if (image.isNull()) {
        return {};
    }
    const QRectF drawn(l.number(u"ImgOffsetX", 0), l.number(u"ImgOffsetY", 0), l.number(u"ImgWidth", box.width()),
                       l.number(u"ImgHeight", box.height()));
    const bool cropped = std::abs(drawn.left() - box.left()) > 1e-4 || std::abs(drawn.top() - box.top()) > 1e-4
                      || std::abs(drawn.width() - box.width()) > 1e-4 || std::abs(drawn.height() - box.height()) > 1e-4;
    if (!cropped || drawn.width() <= 0 || drawn.height() <= 0) {
        return bytes;
    }
    // The part of the picture inside the shape's box (y up → image rows).
    const double sx = image.width() / drawn.width();
    const double sy = image.height() / drawn.height();
    const QRect pixels(int(std::lround((box.left() - drawn.left()) * sx)),
                       int(std::lround((drawn.top() + drawn.height() - (box.top() + box.height())) * sy)),
                       std::max(1, int(std::lround(box.width() * sx))), std::max(1, int(std::lround(box.height() * sy))));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.copy(pixels).save(&buffer, "PNG");
    return png;
}

// ConLineRouteExt 2 = curved lines; ShapeRouteStyle 2 = straight, any
// other non-zero style a right-angle kind; ObjType 2 = a routable (dynamic)
// connector, right-angle unless told otherwise; else the geometry decides.
Routing routingOf(const Lookup &l, const QList<Figure> &figures)
{
    if (int(l.number(u"ConLineRouteExt", 0)) == 2) {
        return Routing::Curved;
    }
    int segments = 0;
    for (const Figure &f : figures) {
        for (int i = 0; i < f.path.elementCount(); ++i) {
            const QPainterPath::Element e = f.path.elementAt(i);
            if (e.isCurveTo()) {
                return Routing::Curved;
            }
            segments += e.isLineTo() ? 1 : 0;
        }
    }
    const int style = int(l.number(u"ShapeRouteStyle", 0));
    if (style == 2) {
        return Routing::Straight;
    }
    if (style != 0 || int(l.number(u"ObjType", 0)) == 2) {
        return Routing::Orthogonal;
    }
    return segments > 1 ? Routing::Orthogonal : Routing::Straight;
}

} // namespace

PageReader::PageReader(ReadContext &ctx, SourcePage &source, Page &page, const QTransform &pageTransform, int variation)
    : m_ctx(ctx)
    , m_source(source)
    , m_page(page)
    , m_pageTransform(pageTransform)
    , m_variation(variation)
{
}

void PageReader::prescan(const XmlElement *connects)
{
    if (connects == nullptr) {
        return;
    }
    for (const XmlElement *c : children(*connects, u"Connect")) {
        m_glueTargets.insert(intAttribute(*c, u"ToSheet", -1));
    }
}

void PageReader::readShapes(const XmlElement *shapes)
{
    if (shapes == nullptr) {
        return;
    }
    Parent top;
    top.toPage = m_pageTransform;
    for (const Ooxml::XmlChild &c : shapes->children) {
        if (c.element && c.element->is(kNs, u"Shape")) {
            readShape(c.element, top, 0);
        }
    }
}

void PageReader::readShape(const XmlElementPtr &element, const Parent &parent, int depth)
{
    const XmlElement &e = *element;
    const int id = intAttribute(e, u"ID", -1);
    m_source.maxId = std::max(m_source.maxId, id);
    MasterInfo *master = parent.master;
    const int masterShape = intAttribute(e, u"MasterShape", -1);
    const bool ownMaster = e.hasAttribute({}, u"Master");
    if (ownMaster) {
        const auto it = m_ctx.masters.find(intAttribute(e, u"Master", -1));
        master = it != m_ctx.masters.end() ? &*it : nullptr;
    }
    const bool inherits = master != nullptr && (ownMaster || masterShape >= 0);
    const Sheet *base = inherits ? master->sheet(masterShape) : nullptr;
    const Sheet sheet = Sheet::merge(e, base);
    const MasterInfo *inheritedFrom = base != nullptr ? master : nullptr;
    const QString key = itemKey(m_source.id, id);
    if (depth == 0) {
        m_source.order.append(key);
    }
    const auto keep = [&] {
        ++m_ctx.kept;
        if (depth == 0) {
            m_source.unmodelled.insert(key, element);
        }
    };
    // M87: a shape on a hidden layer is read like any other (layers are
    // modelled now); guides and runaway nesting stay kept, not shown.
    if (sheet.type == QLatin1String("Guide") || depth > 32) {
        keep();
        return;
    }
    const Lookup lookup(sheet, m_ctx.doc, m_variation);
    const QTransform local = localTransform(lookup);
    if (sheet.type == QLatin1String("Group")) {
        Parent inner = parent;
        inner.master = ownMaster ? master : parent.master;
        readGroup(element, sheet, inner, local, key, depth, inheritedFrom, masterShape);
        return;
    }
    const bool oneD = sheet.cell(u"BeginX") != nullptr && sheet.cell(u"EndX") != nullptr && sheet.foreignData() == nullptr;
    if (oneD) {
        const QList<Figure> figures =
            readGeometry(sheet, lookup.number(u"Width", 0), lookup.number(u"Height", 0));
        ShapeStyle look;
        readShapeStyle(lookup, look);
        const bool filled = look.fill.alpha() > 0
                         && std::any_of(figures.cbegin(), figures.cend(), [](const Figure &f) { return f.fill; });
        if (!filled) {
            makeConnector(element, sheet, parent, key);
            record(element, key, parent, id, false, inheritedFrom, masterShape);
            return;
        }
    }
    if (!makeShape(element, sheet, local * parent.toPage, parent.groupId, master, key, m_glueTargets.contains(id))) {
        keep();
        return;
    }
    record(element, key, parent, id, false, inheritedFrom, masterShape);
}

void PageReader::readGroup(const XmlElementPtr &element, const Sheet &sheet, const Parent &parent,
                           const QTransform &local, const QString &key, int depth, const MasterInfo *base,
                           int masterShape)
{
    const int id = intAttribute(*element, u"ID", -1);
    Parent inner = parent;
    inner.toPage = local * parent.toPage;
    if (depth == 0) {
        inner.groupKey = key;
        inner.groupId = m_ctx.drawing->newId(QStringLiteral("g"));
        SourceGroup group;
        group.page = m_source.id;
        group.part = m_source.part;
        group.element = element;
        group.visioId = id;
        collectIds(*element, group.ids);
        m_ctx.source->groups.insert(key, group);
    }
    const Lookup lookup(sheet, m_ctx.doc, m_variation);
    const QList<Figure> own = readGeometry(sheet, lookup.number(u"Width", 0), lookup.number(u"Height", 0));
    const bool filledBelow = std::any_of(own.cbegin(), own.cend(), [](const Figure &f) { return f.fill; });
    const bool glued = m_glueTargets.contains(id);
    // The group's own geometry paints below its members; its text above.
    bool made = false;
    if (filledBelow) {
        made = makeShape(element, sheet, inner.toPage, inner.groupId, inner.master, key, glued);
    }
    if (const XmlElement *shapes = child(*element, u"Shapes")) {
        for (const Ooxml::XmlChild &c : shapes->children) {
            if (c.element && c.element->is(kNs, u"Shape")) {
                readShape(c.element, inner, depth + 1);
            }
        }
    }
    if (!filledBelow) {
        made = makeShape(element, sheet, inner.toPage, inner.groupId, inner.master, key, glued);
    }
    if (made) {
        record(element, key, inner, id, true, base, masterShape);
    }
}

bool PageReader::makeShape(const XmlElementPtr &element, const Sheet &sheet, const QTransform &toPage,
                           const QString &groupId, const MasterInfo *master, const QString &key, bool force)
{
    const Lookup lookup(sheet, m_ctx.doc, m_variation);
    const QList<Figure> figures = readGeometry(sheet, lookup.number(u"Width", 0), lookup.number(u"Height", 0));
    const ParsedText text = sheet.text() != nullptr ? parseText(*sheet.text()) : ParsedText();
    const bool hasText = !text.text.trimmed().isEmpty();
    const XmlElement *foreign = sheet.foreignData();
    if (figures.isEmpty() && !hasText && foreign == nullptr && !force) {
        return false;
    }
    const double k = std::hypot(toPage.m11(), toPage.m12());
    const QRectF box = localBox(lookup, figures, k);
    const Frame frame = frameOf(toPage, box);
    Shape shape;
    shape.id = m_ctx.drawing->newId(QStringLiteral("s"));
    shape.type = foreign != nullptr ? QStringLiteral("picture") : QStringLiteral("path");
    shape.rect = frame.rect;
    shape.rotation = frame.rotation;
    shape.flipX = frame.flipX;
    readShapeStyle(lookup, shape.style);
    const QTransform normalize = normalizer(box);
    if (foreign == nullptr) {
        for (const Figure &f : figures) {
            shape.paths.append({normalize.map(f.path), f.fill, f.stroke});
        }
        if (shape.paths.isEmpty()) {
            // Text only: an empty outline that paints nothing.
            QPainterPath none;
            none.moveTo(0, 0);
            none.lineTo(0, 0);
            shape.paths.append({none, false, false});
        }
    } else {
        QStringList parts{m_source.part};
        if (master != nullptr) {
            parts.append(master->part); // a picture the instance inherits
        }
        QString kind;
        shape.image = pictureOf(m_ctx, *foreign, parts, lookup, box, &kind);
        if (shape.image.isEmpty()) {
            (kind.contains(QLatin1String("MetaFile"), Qt::CaseInsensitive) ? m_ctx.metafiles : m_ctx.objects) += 1;
        }
        shape.style.fill = QColor(Qt::transparent);
    }
    if (hasText) {
        const TextRun run = leadingRun(text);
        shape.text = text.text;
        shape.textStyle = readTextStyle(lookup, run.charIx, run.paraIx);
        if (!shape.textStyle.color.isValid()) {
            shape.textStyle.color = readableOn(customOutlineFills(shape) ? shape.style.fill : QColor());
        }
        shape.textBox = textBoxOf(lookup, box);
        const bool several = std::any_of(text.runs.cbegin(), text.runs.cend(),
                                         [&](const TextRun &r) { return r.charIx != run.charIx; });
        m_ctx.richText += several ? 1 : 0;
    }
    shape.data = shapeData(sheet);
    shape.ports = connectionPoints(sheet, box);
    shape.aspectLocked = lookup.number(u"LockAspect", 0) != 0;
    readItemExtras(sheet, shape.layers, shape.links); // M87
    readContainer(sheet, k, shape);
    shape.groupId = groupId;
    shape.extra.insert(QStringLiteral("vsdx"), keyObject(m_ctx.source->token, key));
    m_modelIds.insert(intAttribute(*element, u"ID", -1), shape.id);
    m_keyOf.insert(shape.id, key);
    m_page.shapes.append(shape);
    return true;
}

void PageReader::makeConnector(const XmlElementPtr &element, const Sheet &sheet, const Parent &parent,
                               const QString &key)
{
    const Lookup lookup(sheet, m_ctx.doc, m_variation);
    Connector c;
    c.id = m_ctx.drawing->newId(QStringLiteral("c"));
    c.from.point = parent.toPage.map(QPointF(lookup.number(u"BeginX", 0), lookup.number(u"BeginY", 0)));
    c.to.point = parent.toPage.map(QPointF(lookup.number(u"EndX", 0), lookup.number(u"EndY", 0)));
    const QList<Figure> figures = readGeometry(sheet, lookup.number(u"Width", 0), lookup.number(u"Height", 0));
    c.routing = routingOf(lookup, figures);
    readLine(lookup, c.line, c.lineWidth, c.pattern);
    c.lineWidth = std::max(0.25, c.lineWidth);
    const auto arrow = [&](const char16_t *name) {
        const Cell *cell = lookup.cell(name);
        return arrowOf(cell == nullptr || cell->themed() ? 0 : int(cell->number().value_or(0)));
    };
    c.startArrow = arrow(u"BeginArrow");
    c.endArrow = arrow(u"EndArrow");
    if (sheet.text() != nullptr) {
        const ParsedText text = parseText(*sheet.text());
        if (!text.text.trimmed().isEmpty()) {
            const TextRun run = leadingRun(text);
            c.text = text.text;
            c.textStyle = readTextStyle(lookup, run.charIx, run.paraIx);
            if (!c.textStyle.color.isValid()) {
                c.textStyle.color = QColor(Qt::black);
            }
        }
    }
    c.data = shapeData(sheet);
    readItemExtras(sheet, c.layers, c.links); // M87
    c.groupId = parent.groupId;
    c.extra.insert(QStringLiteral("vsdx"), keyObject(m_ctx.source->token, key));
    m_modelIds.insert(intAttribute(*element, u"ID", -1), c.id);
    m_keyOf.insert(c.id, key);
    m_page.connectors.append(c);
    // Glue the <Connects> may not list (older writers): the formulas.
    m_glueFormulas.insert(c.id, {lookup.cell(u"BeginX") ? lookup.cell(u"BeginX")->formula : QString(),
                                 lookup.cell(u"BegTrigger") ? lookup.cell(u"BegTrigger")->formula : QString(),
                                 lookup.cell(u"EndX") ? lookup.cell(u"EndX")->formula : QString(),
                                 lookup.cell(u"EndTrigger") ? lookup.cell(u"EndTrigger")->formula : QString()});
}

void PageReader::record(const XmlElementPtr &element, const QString &key, const Parent &parent, int visioId,
                        bool groupSheet, const MasterInfo *master, int masterShape)
{
    SourceItem item;
    item.key = key;
    item.page = m_source.id;
    item.part = m_source.part;
    item.element = element;
    item.visioId = visioId;
    item.group = parent.groupKey;
    item.groupSheet = groupSheet;
    // A child that names only a MasterShape inherits through its group's
    // master: it can never be written outside that group.
    item.masterChild = master != nullptr && !element->hasAttribute({}, u"Master");
    item.master = master != nullptr ? master->id : -1;
    item.masterShape = masterShape;
    m_ctx.source->items.insert(key, item);
    if (!item.group.isEmpty()) {
        m_ctx.source->groups[item.group].members.append(key);
    }
}

void PageReader::readConnects(const XmlElement *connects)
{
    const auto glue = [&](const QString &connectorId, bool begin, const QString &shapeId, int toPart) {
        Connector *c = m_page.connector(connectorId);
        const Shape *s = m_page.shape(shapeId);
        if (c == nullptr || s == nullptr) {
            return;
        }
        Endpoint &end = begin ? c->from : c->to;
        end.shapeId = shapeId;
        end.port = toPart >= 100 && toPart - 100 < portCount(*s) ? toPart - 100 : -1;
    };
    if (connects != nullptr) {
        for (const Ooxml::XmlChild &child : connects->children) {
            if (!child.element || !child.element->is(kNs, u"Connect")) {
                continue;
            }
            const XmlElement &c = *child.element;
            m_source.connects.append(child.element);
            const QString from = m_modelIds.value(intAttribute(c, u"FromSheet", -1));
            const QString to = m_modelIds.value(intAttribute(c, u"ToSheet", -1));
            const QString cell = c.attribute({}, u"FromCell");
            const int fromPart = intAttribute(c, u"FromPart", -1);
            const bool begin = cell == QLatin1String("BeginX") || cell == QLatin1String("BeginY") || fromPart == 9;
            const bool end = cell == QLatin1String("EndX") || cell == QLatin1String("EndY") || fromPart == 12;
            if (begin || end) {
                glue(from, begin, to, intAttribute(c, u"ToPart", 3));
            }
        }
    }
    // Ends no <Connect> names, glued by formula only.
    static const QRegularExpression point(QStringLiteral("Sheet\\.(\\d+)!Connections\\.X(\\d+)"));
    static const QRegularExpression trigger(QStringLiteral("_XFTRIGGER\\(Sheet\\.(\\d+)!EventXFMod\\)"));
    for (auto it = m_glueFormulas.cbegin(); it != m_glueFormulas.cend(); ++it) {
        const Connector *c = m_page.connector(it.key());
        for (const bool begin : {true, false}) {
            if (c == nullptr || (begin ? c->from : c->to).glued()) {
                continue;
            }
            const QString formula = begin ? it->at(0) : it->at(2);
            const QRegularExpressionMatch p = point.match(formula);
            const QRegularExpressionMatch t = trigger.match(begin ? it->at(1) : it->at(3));
            if (p.hasMatch()) {
                glue(it.key(), begin, m_modelIds.value(p.captured(1).toInt()), 100 + p.captured(2).toInt() - 1);
            } else if (t.hasMatch() && formula.contains(QLatin1String("_WALKGLUE"))) {
                glue(it.key(), begin, m_modelIds.value(t.captured(1).toInt()), 3);
            }
            c = m_page.connector(it.key());
        }
    }
}

void PageReader::snapshot()
{
    for (const Shape &s : std::as_const(m_page.shapes)) {
        const QString key = m_keyOf.value(s.id);
        if (m_ctx.source->items.contains(key)) {
            m_ctx.source->items[key].shape = s;
        }
    }
    for (const Connector &c : std::as_const(m_page.connectors)) {
        const QString key = m_keyOf.value(c.id);
        if (m_ctx.source->items.contains(key)) {
            m_ctx.source->items[key].connector = c;
        }
    }
}

} // namespace QindaOffice::Diagram::Visio
