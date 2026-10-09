// SPDX-License-Identifier: GPL-3.0-or-later
// Graphic frames (tables, charts, SmartArt, OLE objects), alternate
// content and everything else the model keeps verbatim, for the pptx
// reader (pptx_read_shapes.h). What QindaShow cannot edit is a Preserved
// shape: its XML as a fragment plus its source part, and a render-only
// fallback — SmartArt's pre-rendered drawing part, the Fallback branch of
// mc:AlternateContent, an OLE object's picture, or a labelled box.

#include "pptx_read_shapes.h"

#include "pptx_reader.h"

#include "ooxml_namespaces.h"

namespace QindaOffice::Show::Pptx {

namespace {

const QString kTableUri = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/table");
const QString kChartUri = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/chart");
const QString kDiagramUri = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/diagram");
const QString kOleUri = QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/ole");
const QString kDsp = QStringLiteral("http://schemas.microsoft.com/office/drawing/2008/diagram");

// The first blip anywhere under `element`: the picture an OLE object or
// alternate content shows when its live content cannot be.
QByteArray fallbackPicture(const PartContext &ctx, const Ooxml::XmlElement &element)
{
    const Ooxml::XmlElement *blip = element.descendant(nsA(), u"blip");
    return blip != nullptr ? relatedData(ctx, blip->attribute(nsR(), u"embed")).bytes : QByteArray();
}

// SmartArt keeps a pre-rendered copy of itself (ppt/diagrams/drawingN.xml),
// named by the data part's dsp:dataModelExt and related from the slide.
std::vector<Shape> smartArtDrawing(const ShapeScope &scope, const Ooxml::XmlElement &graphicData)
{
    std::vector<Shape> shapes;
    const Ooxml::XmlElement *relIds = graphicData.child(QString::fromUtf16(Ooxml::Ns::DGM), u"relIds");
    if (relIds == nullptr || !scope.ctx.package) {
        return shapes;
    }
    const RelatedData data = relatedData(scope.ctx, relIds->attribute(nsR(), u"dm"));
    const Ooxml::XmlElementPtr dataRoot = data.partName.isEmpty() ? nullptr : scope.ctx.package->xml(data.partName);
    const Ooxml::XmlElement *ext = dataRoot ? dataRoot->descendant(kDsp, u"dataModelExt") : nullptr;
    if (ext == nullptr) {
        return shapes;
    }
    const RelatedData drawing = relatedData(scope.ctx, ext->attr(u"relId"));
    const Ooxml::XmlElementPtr root = drawing.partName.isEmpty() ? nullptr : scope.ctx.package->xml(drawing.partName);
    const Ooxml::XmlElement *tree = root ? root->child(kDsp, u"spTree") : nullptr;
    if (tree == nullptr) {
        return shapes;
    }
    ShapeScope inner = scope;
    inner.ctx.part = drawing.partName;
    inner.layout = nullptr;
    inner.master = nullptr;
    inner.keepOrigin = false;
    inner.report = nullptr;
    inner.idPrefix = QStringLiteral("dgm");
    QSet<QString> ids;
    inner.usedIds = &ids;
    for (const Shape &shape : readShapeTree(inner, *tree)) {
        shapes.push_back(shape);
    }
    return shapes;
}

QString alternateLabel(const Ooxml::XmlElement &choice)
{
    const QList<const Ooxml::XmlElement *> items = choice.elements();
    if (items.isEmpty()) {
        return QStringLiteral("Office content");
    }
    const Ooxml::XmlElement *first = items.first();
    if (first->name == u"contentPart") {
        return QStringLiteral("Ink");
    }
    if (first->descendant(QString::fromUtf16(Ooxml::Ns::A14), u"m") != nullptr) {
        return QStringLiteral("Equation");
    }
    if (first->name == u"graphicFrame") {
        return QStringLiteral("Chart");
    }
    return QStringLiteral("Office content");
}

} // namespace

Shape readPreserved(const ShapeScope &scope, const Ooxml::XmlElementPtr &element, const QString &label)
{
    Shape s;
    s.kind = ShapeKind::Preserved;
    s.preserved = {Ooxml::serializeFragment(*element), scope.ctx.part, label};
    const Ooxml::XmlElement *cNvPr = element->descendant(nsP(), u"cNvPr");
    readNonVisual(scope, cNvPr, s);
    const Ooxml::XmlElement *xfrm = element->descendant(nsA(), u"xfrm");
    if (xfrm == nullptr) {
        xfrm = element->descendant(nsP(), u"xfrm");
    }
    readTransform(xfrm, s);
    s.imagePng = fallbackPicture(scope.ctx, *element);
    s.fill = Qt::transparent;
    s.strokeWidth = 0;
    return s;
}

Shape readGraphicFrame(const ShapeScope &scope, const Ooxml::XmlElementPtr &element)
{
    const Ooxml::XmlElement &e = *element;
    const QString &ns = e.ns;
    Shape s;
    const Ooxml::XmlElement *nv = e.child(ns, u"nvGraphicFramePr");
    readNonVisual(scope, nv != nullptr ? nv->child(ns, u"cNvPr") : nullptr, s);
    if (const Ooxml::XmlElement *nvPr = nv != nullptr ? nv->child(ns, u"nvPr") : nullptr) {
        if (const Ooxml::XmlElement *ph = nvPr->child(nsP(), u"ph")) {
            s.placeholder.type = ph->attr(u"type", QStringLiteral("obj"));
            s.placeholder.index = ph->hasAttribute(QString(), u"idx") ? ph->attr(u"idx").toLongLong() : -1;
        }
    }
    readTransform(e.child(ns, u"xfrm"), s);
    s.fill = Qt::transparent;
    s.strokeWidth = 0;
    const Ooxml::XmlElement *graphic = e.child(nsA(), u"graphic");
    const Ooxml::XmlElement *data = graphic != nullptr ? graphic->child(nsA(), u"graphicData") : nullptr;
    const QString uri = data != nullptr ? data->attr(u"uri") : QString();
    if (uri == kTableUri && data->child(nsA(), u"tbl") != nullptr) {
        s.kind = ShapeKind::Table;
        s.table = readTable(scope, *data->child(nsA(), u"tbl"));
        return s;
    }
    s.preserved = {Ooxml::serializeFragment(e), scope.ctx.part, QString()};
    if (uri == kChartUri) {
        s.kind = ShapeKind::Chart;
        s.preserved.label = QStringLiteral("Chart");
        const Ooxml::XmlElement *chart = data->child(nsC(), u"chart");
        const RelatedData part = relatedData(scope.ctx, chart != nullptr ? chart->attribute(nsR(), u"id") : QString());
        if (!part.partName.isEmpty()) {
            s.chart = readChart(scope.ctx, part.partName);
        }
        return s;
    }
    s.kind = ShapeKind::Preserved;
    if (uri == kDiagramUri) {
        s.preserved.label = QStringLiteral("SmartArt");
        s.children = smartArtDrawing(scope, *data);
        s.childRect = QRectF(QPointF(0, 0), s.rect.size());
    } else if (uri == kOleUri) {
        s.preserved.label = QStringLiteral("Embedded object");
        s.imagePng = fallbackPicture(scope.ctx, e);
    } else {
        s.preserved.label = QStringLiteral("Office content");
        s.imagePng = fallbackPicture(scope.ctx, e);
    }
    return s;
}

Shape readAlternateContent(const ShapeScope &scope, const Ooxml::XmlElementPtr &element, const ResolvedFill *groupFill)
{
    Shape s;
    s.kind = ShapeKind::Preserved;
    const Ooxml::XmlElement *choice = element->child(nsMc(), u"Choice");
    const Ooxml::XmlElement *fallback = element->child(nsMc(), u"Fallback");
    s.preserved = {Ooxml::serializeFragment(*element), scope.ctx.part,
                   choice != nullptr ? alternateLabel(*choice) : QStringLiteral("Office content")};
    const Ooxml::XmlElement *shown = fallback != nullptr ? fallback : choice;
    if (shown != nullptr) {
        // Render-only copies of the fallback, in slide coordinates.
        ShapeScope inner = scope;
        inner.keepOrigin = false;
        inner.report = nullptr;
        inner.idPrefix = QStringLiteral("alt");
        QSet<QString> ids;
        inner.usedIds = &ids;
        for (const Ooxml::XmlChild &child : shown->children) {
            if (child.element) {
                s.children.push_back(readElement(inner, child.element, groupFill));
            }
        }
    }
    QRectF bounds;
    for (const Shape &child : s.children) {
        bounds = bounds.isNull() ? child.rect.normalized() : bounds.united(child.rect.normalized());
    }
    s.rect = bounds;
    s.childRect = bounds;
    // The Office id the timing and connectors use is the content's own.
    const Ooxml::XmlElement *cNvPr = element->descendant(nsP(), u"cNvPr");
    readNonVisual(scope, cNvPr, s);
    s.fill = Qt::transparent;
    s.strokeWidth = 0;
    if (s.children.empty()) {
        s.imagePng = fallbackPicture(scope.ctx, *element);
    }
    return s;
}

} // namespace QindaOffice::Show::Pptx
