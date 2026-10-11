// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_graphics_p.h"
#include "drawing_interchange.h"
#include "ooxml_namespaces.h"

namespace QindaOffice::Docx::Graphics {
QVariantMap newDiagram()
{
    const QString dgm = QString::fromUtf16(Ooxml::Ns::DGM);
    const QString a = QString::fromUtf16(Ooxml::Ns::A);
    const QString dsp = QStringLiteral("http://schemas.microsoft.com/office/drawing/2008/diagram");
    auto root = std::make_shared<XmlElement>(dgm, QStringLiteral("dataModel"));
    auto points = std::make_shared<XmlElement>(dgm, QStringLiteral("ptLst"));
    auto connections = std::make_shared<XmlElement>(dgm, QStringLiteral("cxnLst"));
    auto doc = std::make_shared<XmlElement>(dgm, QStringLiteral("pt"));
    doc->setAttribute({}, QStringLiteral("modelId"), QStringLiteral("0"));
    doc->setAttribute({}, QStringLiteral("type"), QStringLiteral("doc")); points->appendElement(doc);
    auto drawing = std::make_shared<XmlElement>(dsp, QStringLiteral("drawing"));
    auto tree = std::make_shared<XmlElement>(dsp, QStringLiteral("spTree"));
    auto nv = std::make_shared<XmlElement>(dsp, QStringLiteral("nvGrpSpPr"));
    nv->appendElement(dsp, QStringLiteral("cNvPr")).setAttribute({}, QStringLiteral("id"), QStringLiteral("0")).setAttribute({}, QStringLiteral("name"), QString());
    nv->appendElement(dsp, QStringLiteral("cNvGrpSpPr")); tree->appendElement(nv);
    tree->appendElement(dsp, QStringLiteral("grpSpPr"));
    QStringList ids, texts{QStringLiteral("Plan"), QStringLiteral("Create"), QStringLiteral("Deliver")};
    for (int i = 0; i < 3; ++i) {
        const QString id = QString::number(i + 1); ids.append(id);
        auto pt = std::make_shared<XmlElement>(dgm, QStringLiteral("pt")); pt->setAttribute({}, QStringLiteral("modelId"), id);
        auto text = std::make_shared<XmlElement>(dgm, QStringLiteral("t"));
        text->appendElement(a, QStringLiteral("bodyPr")); text->appendElement(a, QStringLiteral("lstStyle"));
        auto p = std::make_shared<XmlElement>(a, QStringLiteral("p"));
        p->appendElement(a, QStringLiteral("pPr")).setAttribute({}, QStringLiteral("algn"), QStringLiteral("ctr"));
        auto r = std::make_shared<XmlElement>(a, QStringLiteral("r"));
        r->appendElement(a, QStringLiteral("rPr")).setAttribute({}, QStringLiteral("sz"), QStringLiteral("1800"))
            .appendElement(a, QStringLiteral("solidFill")).appendElement(a, QStringLiteral("srgbClr")).setAttribute({}, QStringLiteral("val"), QStringLiteral("FFFFFF"));
        r->appendElement(a, QStringLiteral("t")).appendText(texts.at(i)); p->appendElement(r); text->appendElement(p);
        pt->appendElement(text); points->appendElement(pt);
        auto cxn = std::make_shared<XmlElement>(dgm, QStringLiteral("cxn"));
        cxn->setAttribute({}, QStringLiteral("modelId"), QString::number(i + 10)).setAttribute({}, QStringLiteral("type"), QStringLiteral("parOf"))
            .setAttribute({}, QStringLiteral("srcId"), QStringLiteral("0")).setAttribute({}, QStringLiteral("destId"), id)
            .setAttribute({}, QStringLiteral("srcOrd"), QString::number(i)).setAttribute({}, QStringLiteral("destOrd"), QStringLiteral("0"));
        connections->appendElement(cxn);
        auto sp = std::make_shared<XmlElement>(dsp, QStringLiteral("sp")); sp->setAttribute({}, QStringLiteral("modelId"), id);
        auto nvSp = std::make_shared<XmlElement>(dsp, QStringLiteral("nvSpPr"));
        nvSp->appendElement(dsp, QStringLiteral("cNvPr")).setAttribute({}, QStringLiteral("id"), id).setAttribute({}, QStringLiteral("name"), QStringLiteral("Node %1").arg(i + 1));
        nvSp->appendElement(dsp, QStringLiteral("cNvSpPr")); sp->appendElement(nvSp);
        Show::Shape shape = defaultShape(); shape.rect = QRectF(0, i * 54, 240, 44);
        auto wsp = shapeXml(shape);
        auto props = wsp->child(QString::fromUtf16(Ooxml::Ns::WPS), u"spPr")->clone(); props->ns = dsp;
        if (auto *off = descendant(*props, a, u"off")) off->setAttribute({}, QStringLiteral("y"), QString::number(i * 54 * 12700));
        sp->appendElement(props);
        auto tx = text->clone(); tx->ns = dsp; tx->name = QStringLiteral("txBody");
        tx->child(a, u"bodyPr")->setAttribute({}, QStringLiteral("anchor"), QStringLiteral("ctr"));
        sp->appendElement(tx); tree->appendElement(sp);
    }
    drawing->appendElement(tree); root->appendElement(points); root->appendElement(connections);
    auto extensions = std::make_shared<XmlElement>(dgm, QStringLiteral("extLst"));
    auto ext = std::make_shared<XmlElement>(a, QStringLiteral("ext"));
    ext->setAttribute({}, QStringLiteral("uri"), QStringLiteral("http://schemas.microsoft.com/office/drawing/2008/diagram"));
    ext->appendElement(dsp, QStringLiteral("dataModelExt")).setAttribute({}, QStringLiteral("relId"), QStringLiteral("rId1"));
    extensions->appendElement(ext); root->appendElement(extensions);
    // A simple vertical list is native SmartArt: data, layout, style,
    // colours and cached drawing are all supplied; no raster replacement.
    const QString layout = QStringLiteral(R"(<dgm:layoutDef xmlns:dgm="http://schemas.openxmlformats.org/drawingml/2006/diagram" uniqueId="urn:qindaoffice:smartart:vertical-list:1" minVer="12.0"><dgm:title val="Vertical list"/><dgm:layoutNode name="diagram"><dgm:alg type="lin"><dgm:param type="linDir" val="fromT"/></dgm:alg><dgm:presOf axis="self"/><dgm:forEach axis="ch" ptType="node"><dgm:layoutNode name="node" styleLbl="node"><dgm:alg type="tx"/><dgm:shape type="roundRect"/><dgm:presOf axis="self"/><dgm:constrLst><dgm:constr type="w" refType="w" fact="1"/><dgm:constr type="h" refType="h" fact="0.28"/></dgm:constrLst></dgm:layoutNode></dgm:forEach></dgm:layoutNode></dgm:layoutDef>)");
    const QString style = QStringLiteral(R"(<dgm:styleDef xmlns:dgm="http://schemas.openxmlformats.org/drawingml/2006/diagram" uniqueId="urn:qindaoffice:smartart:style:1" minVer="12.0"><dgm:title val="Simple"/><dgm:styleLbl name="node"/></dgm:styleDef>)");
    const QString colors = QStringLiteral(R"(<dgm:colorsDef xmlns:dgm="http://schemas.openxmlformats.org/drawingml/2006/diagram" xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" uniqueId="urn:qindaoffice:smartart:colors:1" minVer="12.0"><dgm:title val="Blue"/><dgm:styleLbl name="node"><dgm:fillClrLst><a:srgbClr val="4472C4"/></dgm:fillClrLst><dgm:linClrLst><a:srgbClr val="2F5597"/></dgm:linClrLst><dgm:txFillClrLst><a:srgbClr val="FFFFFF"/></dgm:txFillClrLst></dgm:styleLbl></dgm:colorsDef>)");
    auto shapes = Show::DrawingIO::diagram(*drawing, {}, {}, Ooxml::defaultOfficeTheme());
    for (auto &shape : shapes) shape.extra.insert(QStringLiteral("smartArtNode"), shape.extra.value(QStringLiteral("smartArtModelId")));
    return {{QStringLiteral("texts"), texts}, {QStringLiteral("ids"), ids},
            {QStringLiteral("dataXml"), Ooxml::serializeFragment(*root)},
            {QStringLiteral("drawingXml"), Ooxml::serializeFragment(*drawing)},
            {QStringLiteral("render"), Show::shapesToJson(shapes)},
            {QStringLiteral("loXml"), layout}, {QStringLiteral("qsXml"), style}, {QStringLiteral("csXml"), colors}};
}
} // namespace QindaOffice::Docx::Graphics
