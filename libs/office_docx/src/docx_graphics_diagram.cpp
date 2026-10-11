// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_graphics_p.h"
#include "drawing_interchange.h"
#include "ooxml_namespaces.h"
#include <QJsonObject>

namespace QindaOffice::Docx::Graphics {
namespace {
const QString kDgm = QString::fromUtf16(Ooxml::Ns::DGM);
const QString kA = QString::fromUtf16(Ooxml::Ns::A);
const QString kR = QString::fromUtf16(Ooxml::Ns::R);
const QString kDsp = QStringLiteral("http://schemas.microsoft.com/office/drawing/2008/diagram");
QString diagramText(const XmlElement &point)
{
    const auto *body = point.child(kDgm, u"t");
    if (!body) return {};
    QStringList lines;
    for (const auto *p : body->elements(kA, u"p")) {
        QString text;
        for (const auto *r : p->elements()) {
            if (r->name == u"br") text += QLatin1Char('\n');
            else if (r->name == u"r" || r->name == u"fld")
                if (const auto *t = r->child(kA, u"t")) text += t->text();
        }
        lines.append(text);
    }
    return lines.join(QLatin1Char('\n'));
}
void replaceBody(XmlElement &body, const QString &text)
{
    const auto *first = body.child(kA, u"p");
    const auto *oldPr = first ? first->child(kA, u"pPr") : nullptr;
    const auto *oldRun = first ? first->descendant(kA, u"rPr") : nullptr;
    auto pPr = oldPr ? oldPr->clone() : nullptr;
    auto rPr = oldRun ? oldRun->clone() : nullptr;
    body.removeChildren(kA, u"p");
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        auto p = std::make_shared<XmlElement>(kA, QStringLiteral("p"));
        if (pPr) p->appendElement(pPr->clone());
        auto r = std::make_shared<XmlElement>(kA, QStringLiteral("r"));
        if (rPr) r->appendElement(rPr->clone());
        r->appendElement(kA, QStringLiteral("t")).appendText(line);
        p->appendElement(r); body.appendElement(p);
    }
}
QHash<QString, QString> pointAliases(const XmlElement &root)
{
    QHash<QString, QString> map;
    if (const auto *list = root.child(kDgm, u"ptLst")) for (const auto *p : list->elements(kDgm, u"pt")) {
        const QString id = p->attr(u"modelId"); map.insert(id, id);
        if (const auto *pr = p->child(kDgm, u"prSet")) {
            const QString associated = pr->attr(u"presAssocID");
            if (!associated.isEmpty()) map.insert(id, associated);
        }
    }
    if (const auto *list = root.child(kDgm, u"cxnLst")) for (const auto *c : list->elements(kDgm, u"cxn"))
        if (c->attr(u"type") == u"presOf") map.insert(c->attr(u"destId"), c->attr(u"srcId"));
    return map;
}
} // namespace
QVariantMap readDiagram(const XmlElement &data, const std::shared_ptr<const Ooxml::Package> &source,
                        const QString &story, const Ooxml::Theme &theme)
{
    const auto *ids = data.child(kDgm, u"relIds");
    if (!ids || !source) return {};
    const QString dataPart = related(source, story, ids->attribute(kR, u"dm"));
    const auto root = source->xml(dataPart);
    const auto *list = root ? root->child(kDgm, u"ptLst") : nullptr;
    if (!list) return {};
    QStringList texts, nodes;
    for (const auto *point : list->elements(kDgm, u"pt")) {
        const QString type = point->attr(u"type", QStringLiteral("node"));
        if (type != u"node" && type != u"asst") continue;
        nodes.append(point->attr(u"modelId")); texts.append(diagramText(*point));
        if (nodes.size() > 256) return {}; // editing remains bounded
    }
    if (nodes.isEmpty()) return {};
    QString drawingPart;
    if (const auto *ext = root->descendant(kDsp, u"dataModelExt")) {
        drawingPart = related(source, dataPart, ext->attr(u"relId"));
        if (drawingPart.isEmpty()) drawingPart = related(source, story, ext->attr(u"relId"));
    }
    if (drawingPart.isEmpty()) for (const auto &rel : source->relationships(dataPart))
        if (rel.type.endsWith(u"/diagramDrawing")) drawingPart = source->targetPart(dataPart, rel);
    const auto drawing = source->xml(drawingPart);
    QList<Show::Shape> shapes;
    if (drawing) shapes = Show::DrawingIO::diagram(*drawing, source, drawingPart, theme);
    const auto aliases = pointAliases(*root);
    for (auto &shape : shapes) {
        const QString id = shape.extra.value(QStringLiteral("smartArtModelId")).toString();
        shape.extra.insert(QStringLiteral("smartArtNode"), aliases.value(id, id));
    }
    QVariantMap value{{QStringLiteral("texts"), texts}, {QStringLiteral("ids"), nodes},
                      {QStringLiteral("dataXml"), Ooxml::serializeFragment(*root)},
                      {QStringLiteral("dataPart"), dataPart}, {QStringLiteral("drawingPart"), drawingPart},
                      {QStringLiteral("drawingXml"), drawing ? Ooxml::serializeFragment(*drawing) : QString()},
                      {QStringLiteral("render"), Show::shapesToJson(shapes)}};
    return value;
}
bool editDiagram(QVariantMap &data, const QStringList &texts, QString *error)
{
    const QStringList ids = data.value(QStringLiteral("ids")).toStringList();
    if (texts.size() != ids.size()) {
        if (error) *error = QStringLiteral("Keep one line for each existing SmartArt node. This editor changes text, not diagram structure.");
        return false;
    }
    auto root = Ooxml::parseFragment(data.value(QStringLiteral("dataXml")).toString());
    if (!root) return false;
    const auto aliases = pointAliases(*root);
    auto *list = root->child(kDgm, u"ptLst");
    const auto previous = data.value(QStringLiteral("texts")).toStringList();
    for (auto &child : list->children) if (child.element && child.element->is(kDgm, u"pt")) {
        auto &point = *child.element;
        const QString id = point.attr(u"modelId");
        const qsizetype index = ids.indexOf(aliases.value(id, id));
        if (index < 0 || texts.at(index) == previous.value(index)) continue;
        auto *body = point.child(kDgm, u"t");
        if (!body) {
            auto t = std::make_shared<XmlElement>(kDgm, QStringLiteral("t")); body = t.get();
            t->appendElement(kA, QStringLiteral("bodyPr")); t->appendElement(kA, QStringLiteral("lstStyle"));
            point.insertElementBefore(t, {u"extLst"});
        }
        replaceBody(*body, texts.at(index));
    }
    auto drawing = Ooxml::parseFragment(data.value(QStringLiteral("drawingXml")).toString());
    if (drawing) {
        std::function<void(XmlElement &)> patch = [&](XmlElement &element) {
            const QString id = element.attr(u"modelId");
            const qsizetype index = id.isEmpty() ? -1 : ids.indexOf(aliases.value(id, id));
            if (index >= 0 && texts.at(index) != previous.value(index))
                if (auto *body = element.child(kDsp, u"txBody")) replaceBody(*body, texts.at(index));
            for (auto &child : element.children) if (child.element) patch(*child.element);
        };
        patch(*drawing);
        data.insert(QStringLiteral("drawingXml"), Ooxml::serializeFragment(*drawing));
    }
    auto shapes = Show::shapesFromJson(data.value(QStringLiteral("render")).toByteArray());
    for (auto &shape : shapes) {
        const qsizetype i = ids.indexOf(shape.extra.value(QStringLiteral("smartArtNode")).toString());
        if (i >= 0 && texts.at(i) != previous.value(i)) replaceText(shape, texts.at(i));
    }
    data.insert(QStringLiteral("render"), Show::shapesToJson(shapes));
    data.insert(QStringLiteral("dataXml"), Ooxml::serializeFragment(*root));
    data.insert(QStringLiteral("texts"), texts);
    return true;
}
void writeDiagram(XmlElement &run, const QVariantMap &data, const std::shared_ptr<const Ooxml::Package> &source,
                  Ooxml::PackageWriter &writer, const QString &story, const QString &sourceStory)
{
    auto root = Ooxml::parseFragment(data.value(QStringLiteral("dataXml")).toString());
    if (!root) return;
    const QString dataPart = writer.uniquePartName(QStringLiteral("word/diagrams/data"), QStringLiteral(".xml"));
    // Reserve both destinations before copying the remaining graph. The
    // copier is local: editing one of two references must not edit the other.
    writer.addPart(dataPart, {}, QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.diagramData+xml"));
    const QString drawingPart = writer.uniquePartName(QStringLiteral("word/diagrams/drawing"), QStringLiteral(".xml"));
    auto drawing = Ooxml::parseFragment(data.value(QStringLiteral("drawingXml")).toString());
    if (drawing) writer.addPart(drawingPart, {}, QStringLiteral("application/vnd.ms-office.drawingml.diagramDrawing+xml"));
    Ooxml::RelationshipCopier copier(source, &writer);
    const QString srcData = data.value(QStringLiteral("dataPart")).toString();
    const QString srcDrawing = data.value(QStringLiteral("drawingPart")).toString();
    if (source) {
        copier.mapPart(srcData, dataPart);
        if (drawing) copier.mapPart(srcDrawing, drawingPart);
        if (drawing) copier.rewrite(*drawing, srcDrawing, drawingPart);
        copier.rewrite(*root, srcData, dataPart);
        copier.rewrite(run, sourceStory, story);
    }
    if (drawing) {
        // Word resolves dataModelExt/@relId against the owning story;
        // other consumers resolve it against the data part. Use one id
        // valid in both relationship sets (the attribute is unqualified).
        QString id; int serial = 1;
        const auto storyRels = writer.relationshipsOf(story);
        const auto dataRels = writer.relationshipsOf(dataPart);
        const auto used = [&](const QString &candidate) {
            for (const auto &rel : storyRels + dataRels) if (rel.id == candidate) return true;
            return false;
        };
        do { id = QStringLiteral("rIdQindaDiagram%1").arg(serial++); } while (used(id));
        const QString type = QStringLiteral("http://schemas.microsoft.com/office/2007/relationships/diagramDrawing");
        writer.addRelationshipWithId(dataPart, id, type, drawingPart);
        writer.addRelationshipWithId(story, id, type, drawingPart);
        if (auto *ext = descendant(*root, kDsp, u"dataModelExt")) ext->setAttribute({}, QStringLiteral("relId"), id);
        writer.addPart(drawingPart, Ooxml::serializeDocument(*drawing), QStringLiteral("application/vnd.ms-office.drawingml.diagramDrawing+xml"));
    }
    writer.addPart(dataPart, Ooxml::serializeDocument(*root), QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.diagramData+xml"));
    auto *ids = descendant(run, kDgm, u"relIds");
    if (!ids) return;
    ids->setAttribute(kR, QStringLiteral("dm"), writer.addRelationship(story, QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/diagramData"), dataPart));
    for (const auto &kind : {QStringLiteral("lo"), QStringLiteral("qs"), QStringLiteral("cs")}) {
        const QString xml = data.value(kind + QStringLiteral("Xml")).toString();
        if (xml.isEmpty()) continue; // the source copier already carried these
        const QString type = kind == u"lo" ? QStringLiteral("diagramLayout") : kind == u"qs" ? QStringLiteral("diagramStyle") : QStringLiteral("diagramColors");
        const QString part = writer.uniquePartName(QStringLiteral("word/diagrams/") + type, QStringLiteral(".xml"));
        writer.addPart(part, xml.toUtf8(), QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.") + type + QStringLiteral("+xml"));
        ids->setAttribute(kR, kind, writer.addRelationship(story, QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/") + type, part));
    }
}
} // namespace QindaOffice::Docx::Graphics
