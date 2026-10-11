// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_graphics_p.h"
#include "ooxml_namespaces.h"
#include "ooxml_units.h"
#include <QJsonObject>

namespace QindaOffice::Docx::Graphics {
namespace {
const QString kW = QString::fromUtf16(Ooxml::Ns::W);
const QString kA = QString::fromUtf16(Ooxml::Ns::A);
const QString kWP = QString::fromUtf16(Ooxml::Ns::WP);
const QString kWPS = QString::fromUtf16(Ooxml::Ns::WPS);
XmlElementPtr make(const QString &ns, const QString &name) { return std::make_shared<XmlElement>(ns, name); }
XmlElementPtr fill(const QColor &color)
{
    auto value = make(kA, color.alpha() == 0 ? QStringLiteral("noFill") : QStringLiteral("solidFill"));
    if (color.alpha() != 0) {
        auto rgb = make(kA, QStringLiteral("srgbClr"));
        rgb->setAttribute({}, QStringLiteral("val"), color.name().mid(1)); value->appendElement(rgb);
    }
    return value;
}
void removeFills(XmlElement &element)
{
    for (const auto *name : {u"noFill", u"solidFill", u"gradFill", u"blipFill", u"pattFill", u"grpFill"})
        element.removeChildren(kA, name);
}
} // namespace
XmlElement *descendant(XmlElement &root, QStringView ns, QStringView name)
{
    if (root.is(ns, name)) return &root;
    for (auto &child : root.children) if (child.element)
        if (auto *found = descendant(*child.element, ns, name)) return found;
    return nullptr;
}
QString related(const std::shared_ptr<const Ooxml::Package> &source, const QString &part, const QString &id)
{
    const auto rel = source ? source->relationship(part, id) : std::nullopt;
    return rel && !rel->external ? source->targetPart(part, *rel) : QString();
}
XmlElementPtr drawingRun(const QString &kind, const QSizeF &size)
{
    auto run = make(kW, QStringLiteral("r"));
    auto drawing = make(kW, QStringLiteral("drawing"));
    auto frame = make(kWP, QStringLiteral("inline"));
    auto extent = make(kWP, QStringLiteral("extent"));
    extent->setAttribute({}, QStringLiteral("cx"), QString::number(qRound64(size.width() * 9525)));
    extent->setAttribute({}, QStringLiteral("cy"), QString::number(qRound64(size.height() * 9525)));
    frame->appendElement(extent);
    auto props = make(kWP, QStringLiteral("docPr"));
    props->setAttribute({}, QStringLiteral("id"), QStringLiteral("1"));
    props->setAttribute({}, QStringLiteral("name"), kind); frame->appendElement(props);
    auto graphic = make(kA, QStringLiteral("graphic"));
    auto data = make(kA, QStringLiteral("graphicData"));
    data->setAttribute({}, QStringLiteral("uri"), kind == u"shape" ? kWPS :
        QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/") + (kind == u"chart" ? QStringLiteral("chart") : QStringLiteral("diagram")));
    graphic->appendElement(data); frame->appendElement(graphic); drawing->appendElement(frame); run->appendElement(drawing);
    return run;
}
Show::Shape defaultShape()
{
    Show::Shape shape;
    shape.kind = Show::ShapeKind::AutoShape;
    shape.geometry = QStringLiteral("roundRect");
    shape.rect = QRectF(0, 0, 240, 120);
    shape.fill = QColor(QStringLiteral("#4472c4"));
    shape.stroke = QColor(QStringLiteral("#2f5597"));
    shape.strokeWidth = 1;
    shape.textBody.anchor = Show::TextAnchor::Middle;
    replaceText(shape, QStringLiteral("Shape text"));
    return shape;
}
void replaceText(Show::Shape &shape, const QString &text)
{
    Show::TextParagraph first = shape.paragraphs.isEmpty() ? Show::TextParagraph() : shape.paragraphs.first();
    Show::TextRun run = first.runs.isEmpty() ? Show::TextRun() : first.runs.first();
    if (shape.paragraphs.isEmpty()) { run.pointSize = 16; run.color = Qt::white; first.alignment = Qt::AlignCenter; }
    shape.paragraphs.clear();
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        run.text = line; first.runs = {run}; shape.paragraphs.append(first);
    }
}
XmlElementPtr shapeXml(const Show::Shape &shape, const XmlElement *original, const Show::Shape *previous)
{
    auto wsp = original ? original->clone() : make(kWPS, QStringLiteral("wsp"));
    if (!original) wsp->appendElement(make(kWPS, QStringLiteral("cNvSpPr")));
    auto *props = wsp->child(kWPS, u"spPr");
    if (!props) { auto value = make(kWPS, QStringLiteral("spPr")); props = value.get(); wsp->appendElement(value); }
    if (!props->child(kA, u"xfrm")) {
        auto xfrm = make(kA, QStringLiteral("xfrm"));
        auto off = make(kA, QStringLiteral("off"));
        off->setAttribute({}, QStringLiteral("x"), QStringLiteral("0")); off->setAttribute({}, QStringLiteral("y"), QStringLiteral("0"));
        auto ext = make(kA, QStringLiteral("ext"));
        ext->setAttribute({}, QStringLiteral("cx"), QString::number(qRound64(shape.rect.width() * 12700)));
        ext->setAttribute({}, QStringLiteral("cy"), QString::number(qRound64(shape.rect.height() * 12700)));
        xfrm->appendElement(off); xfrm->appendElement(ext); props->appendElement(xfrm);
    }
    if (!previous || shape.geometry != previous->geometry) {
        props->removeChildren(kA, u"prstGeom"); props->removeChildren(kA, u"custGeom");
        auto geom = make(kA, QStringLiteral("prstGeom"));
        geom->setAttribute({}, QStringLiteral("prst"), shape.geometry);
        geom->appendElement(make(kA, QStringLiteral("avLst")));
        props->insertElementBefore(geom, {u"noFill", u"solidFill", u"gradFill", u"ln", u"effectLst", u"extLst"});
    }
    if (!previous || shape.fill != previous->fill) {
        removeFills(*props); props->insertElementBefore(fill(shape.fill), {u"ln", u"effectLst", u"effectDag", u"scene3d", u"sp3d", u"extLst"});
    }
    if (!previous || shape.stroke != previous->stroke || shape.strokeWidth != previous->strokeWidth) {
        auto *line = props->child(kA, u"ln");
        if (!line) { auto ln = make(kA, QStringLiteral("ln")); line = ln.get(); props->insertElementBefore(ln, {u"effectLst", u"effectDag", u"scene3d", u"sp3d", u"extLst"}); }
        line->setAttribute({}, QStringLiteral("w"), QString::number(qRound64(shape.strokeWidth * 12700)));
        removeFills(*line); line->insertElementBefore(fill(shape.stroke), {u"prstDash", u"custDash", u"round", u"bevel", u"miter", u"headEnd", u"tailEnd", u"extLst"});
    }
    if (!previous || shape.displayText() != previous->displayText()) {
        auto *box = descendant(*wsp, kW, u"txbxContent");
        if (!box) {
            auto txbx = make(kWPS, QStringLiteral("txbx"));
            auto content = make(kW, QStringLiteral("txbxContent")); box = content.get();
            txbx->appendElement(content); wsp->insertElementBefore(txbx, {u"bodyPr", u"extLst"});
        }
        const auto *oldPr = box->descendant(kW, u"rPr");
        auto rPr = oldPr ? oldPr->clone() : make(kW, QStringLiteral("rPr"));
        if (!oldPr && !shape.paragraphs.isEmpty() && !shape.paragraphs.first().runs.isEmpty()) {
            const auto &run = shape.paragraphs.first().runs.first();
            rPr->appendElement(kW, QStringLiteral("color")).setAttribute(kW, QStringLiteral("val"), run.color.name().mid(1));
            rPr->appendElement(kW, QStringLiteral("sz")).setAttribute(kW, QStringLiteral("val"), QString::number(qRound(run.pointSize * 2)));
        }
        box->children.clear();
        for (const QString &line : shape.displayText().split(QLatin1Char('\n'))) {
            auto p = make(kW, QStringLiteral("p")); auto r = make(kW, QStringLiteral("r"));
            if (!previous) p->appendElement(kW, QStringLiteral("pPr")).appendElement(kW, QStringLiteral("jc")).setAttribute(kW, QStringLiteral("val"), QStringLiteral("center"));
            r->appendElement(rPr->clone()); auto t = make(kW, QStringLiteral("t")); t->appendText(line);
            t->setAttribute(QStringLiteral("http://www.w3.org/XML/1998/namespace"), QStringLiteral("space"), QStringLiteral("preserve"));
            r->appendElement(t); p->appendElement(r); box->appendElement(p);
        }
    }
    if (!wsp->child(kWPS, u"bodyPr")) {
        auto body = make(kWPS, QStringLiteral("bodyPr")); body->setAttribute({}, QStringLiteral("anchor"), QStringLiteral("ctr")); wsp->appendElement(body);
    }
    return wsp;
}
} // namespace QindaOffice::Docx::Graphics
