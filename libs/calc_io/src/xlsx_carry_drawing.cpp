// SPDX-License-Identifier: GPL-3.0-or-later
// Carry phase 3, the drawings of one sheet (xlsx_carry_phases.h, M66,
// D-075): the drawing objects the engine does not model — shapes, text
// boxes, slicer and timeline frames, charts only newer Excel draws, the
// DrawingML side of form controls — and the legacy-drawing shapes of the
// sheet's controls and OLE objects.
//
// AGENT-NOTE: a form control is three things that name one shape id: its
// <control shapeId="1025"> in the sheet, its VML shape _x0000_s1025 in the
// legacy drawing, and its DrawingML anchor's a14:compatExt spid. The VML
// shapes move into a fresh block of ids (the written notes take the blocks
// they were given, and two drawings may not share a block); the mapping
// renumbers all three. VML is carried as text, never re-serialized: Excel
// reads it with a legacy parser that expects its own prefixes and layout.

#include "xlsx_carry_phases.h"

#include "xlsx_xml.h"

#include "ooxml_namespaces.h"

#include <QObject>
#include <QRegularExpression>

#include <algorithm>

namespace QindaOffice::Calc::IO::Carry {

namespace {

const QString kVmlType = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/vmlDrawing");
const QString kDrawingType = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing");
const QString kXdr = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing");

// The written part a relationship of this kind of a written owner leads to.
QString writtenTarget(const Context &ctx, const QString &owner, const QString &typeSuffix)
{
    static const QRegularExpression relationship(QStringLiteral("<Relationship [^>]*>"));
    auto it = relationship.globalMatch(QString::fromUtf8(ctx.part(relsPathFor(owner))));
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        const QRegularExpressionMatch type = QRegularExpression(QStringLiteral("Type=\"([^\"]*)\"")).match(tag);
        const QRegularExpressionMatch target = QRegularExpression(QStringLiteral("Target=\"([^\"]*)\"")).match(tag);
        if (type.captured(1).endsWith(typeSuffix)) {
            return Ooxml::Package::resolveTarget(owner, target.captured(1));
        }
    }
    return {};
}

// A new part's relationship and its element in the sheet, at the element's
// schema position.
bool attachToSheet(Context &ctx, const DestSheet &dest, const QString &type, const QString &part, const QString &element,
                   const QStringList &before, QString *error)
{
    const QString id = ctx.addRelationship(dest.part, type, part);
    QByteArray sheet = ctx.part(dest.part);
    if (!insertBeforeAny(&sheet, before + QStringList{QStringLiteral("/worksheet")},
                         '<' + element.toUtf8() + QByteArrayLiteral(" r:id=\"") + id.toUtf8() + QByteArrayLiteral("\"/>"))) {
        if (error != nullptr) {
            *error = QStringLiteral("carry: no %1 anchor in %2").arg(element, dest.part);
        }
        return false;
    }
    ctx.setPart(dest.part, sheet);
    return true;
}

bool carryVmlShapes(Context &ctx, const DestSheet &dest, const QString &sourceVml, QHash<QString, QString> *shapeIds,
                    QString *error)
{
    const QString text = QString::fromUtf8(ctx.source().data(sourceVml));
    static const QRegularExpression shapePattern(QStringLiteral("<v:shape\\s[\\s\\S]*?</v:shape>"));
    static const QRegularExpression objectType(QStringLiteral("ObjectType=\"([^\"]*)\""));
    QStringList shapes;
    auto it = shapePattern.globalMatch(text);
    while (it.hasNext()) {
        const QString shape = it.next().captured(0);
        if (objectType.match(shape).captured(1) != QLatin1String("Note")) {
            shapes.append(shape); // a control or an OLE object; notes are written anew
        }
    }
    if (shapes.isEmpty()) {
        return true;
    }
    // The legacy drawing they go into: the sheet's notes drawing, or a new one.
    QString destVml = dest.patch->vmlNumber > 0 && !dest.patch->commentCells.isEmpty()
        ? QStringLiteral("xl/drawings/vmlDrawing%1.vml").arg(dest.patch->vmlNumber)
        : QString();
    const int block = ctx.allocateVmlBlocks(int(shapes.size() / 1024) + 1);
    bool created = false;
    QString target;
    if (destVml.isEmpty() || !ctx.hasPart(destVml)) {
        destVml = ctx.addNewPart(QStringLiteral("xl/drawings/vmlDrawing"), QStringLiteral(".vml"), QByteArray(),
                                 QStringLiteral("application/vnd.openxmlformats-officedocument.vmlDrawing"));
        target = QStringLiteral("<xml xmlns:v=\"urn:schemas-microsoft-com:vml\" xmlns:o=\"urn:schemas-microsoft-com:office:office\""
                                " xmlns:x=\"urn:schemas-microsoft-com:office:excel\"><o:shapelayout v:ext=\"edit\">"
                                "<o:idmap v:ext=\"edit\" data=\"%1\"/></o:shapelayout></xml>")
                     .arg(block);
        created = true;
        if (!attachToSheet(ctx, dest, kVmlType, destVml, QStringLiteral("legacyDrawing"),
                           {QStringLiteral("legacyDrawingHF"), QStringLiteral("drawingHF"), QStringLiteral("picture"),
                            QStringLiteral("oleObjects"), QStringLiteral("controls"), QStringLiteral("webPublishItems"),
                            QStringLiteral("tableParts"), QStringLiteral("extLst")},
                           error)) {
            return false;
        }
    } else {
        target = QString::fromUtf8(ctx.part(destVml));
        static const QRegularExpression idmap(QStringLiteral("(<o:idmap[^>]*data=\")([^\"]*)\""));
        const QRegularExpressionMatch match = idmap.match(target);
        if (!match.hasMatch()) {
            ctx.warn(QObject::tr("Controls on “%1” were not kept: its notes drawing has no shape map.").arg(dest.name));
            return true;
        }
        QStringList blocks = match.captured(2).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (int b = 0; b <= int(shapes.size() / 1024); ++b) {
            blocks.append(QString::number(block + b));
        }
        target.replace(match.capturedStart(2), match.capturedLength(2), blocks.join(QLatin1Char(',')));
    }
    // New ids, the relationship ids of their pictures, and the shape types
    // they use that the target does not define yet.
    static const QRegularExpression idPattern(QStringLiteral("_x0000_s(\\d+)"));
    static const QRegularExpression relId(QStringLiteral("o:relid=\"([^\"]*)\""));
    static const QRegularExpression typeRef(QStringLiteral("type=\"#([^\"]*)\""));
    QString inserted;
    QSet<QString> types;
    for (int k = 0; k < shapes.size(); ++k) {
        QString shape = shapes.at(k);
        const QString oldId = idPattern.match(shape).captured(1);
        const QString newId = QString::number(block * 1024 + k + 1);
        if (!oldId.isEmpty()) {
            shapeIds->insert(oldId, newId);
            shape.replace(QStringLiteral("_x0000_s") + oldId, QStringLiteral("_x0000_s") + newId);
        }
        for (auto rel = relId.globalMatch(shapes.at(k)); rel.hasNext();) {
            const QString id = rel.next().captured(1);
            shape.replace(QStringLiteral("o:relid=\"%1\"").arg(id),
                          QStringLiteral("o:relid=\"%1\"").arg(ctx.copyRelationship(sourceVml, id, destVml)));
        }
        types.insert(typeRef.match(shape).captured(1));
        inserted += shape;
    }
    QString shapeTypes;
    for (const QString &type : std::as_const(types)) {
        if (type.isEmpty() || target.contains(QStringLiteral("id=\"%1\"").arg(type))) {
            continue;
        }
        const QRegularExpression definition(QStringLiteral("<v:shapetype\\s[^>]*id=\"%1\"[\\s\\S]*?</v:shapetype>")
                                                .arg(QRegularExpression::escape(type)));
        shapeTypes += definition.match(text).captured(0);
    }
    const qsizetype close = target.lastIndexOf(QLatin1String("</xml>"));
    if (close < 0) {
        ctx.warn(QObject::tr("Controls on “%1” were not kept: its notes drawing is malformed.").arg(dest.name));
        return true;
    }
    target.insert(close, shapeTypes + inserted);
    if (created) {
        ctx.replaceCopied(destVml, target.toUtf8());
    } else {
        ctx.setPart(destVml, target.toUtf8());
    }
    return true;
}

// Moves every anchor (from/to) inside `element` through the journal.
bool moveAnchors(Ooxml::XmlElement &element, const QList<SourceStep> &steps)
{
    if (element.ns == kXdr && (element.name == u"twoCellAnchor" || element.name == u"oneCellAnchor")) {
        Ooxml::XmlElement *from = element.child(kXdr, u"from");
        Ooxml::XmlElement *to = element.child(kXdr, u"to");
        const auto cell = [](Ooxml::XmlElement *corner, QStringView name) -> Ooxml::XmlElement * {
            return corner != nullptr ? corner->child(kXdr, name) : nullptr;
        };
        if (from == nullptr || cell(from, u"row") == nullptr || cell(from, u"col") == nullptr) {
            return true;
        }
        CellRange range{{cell(from, u"row")->text().toInt(), cell(from, u"col")->text().toInt()}, {}};
        range.bottomRight = to != nullptr && cell(to, u"row") != nullptr
            ? CellAddr{cell(to, u"row")->text().toInt(), cell(to, u"col")->text().toInt()}
            : range.topLeft;
        const auto moved = mapSourceRange(steps, range);
        if (!moved) {
            return false;
        }
        const auto set = [](Ooxml::XmlElement *e, qint32 value) {
            e->children.clear();
            e->appendText(QString::number(value));
        };
        set(cell(from, u"row"), moved->topLeft.row);
        set(cell(from, u"col"), moved->topLeft.col);
        if (to != nullptr && cell(to, u"row") != nullptr) {
            set(cell(to, u"row"), moved->bottomRight.row);
            set(cell(to, u"col"), moved->bottomRight.col);
        }
        return true;
    }
    for (Ooxml::XmlChild &child : element.children) {
        if (child.element && !moveAnchors(*child.element, steps)) {
            return false;
        }
    }
    return true;
}

// Control anchors name their VML shape (a14:compatExt spid); the drawing's
// own shape ids must be unique in the target. False when a named VML shape
// was not carried.
bool renumberShapes(Ooxml::XmlElement &element, const QHash<QString, QString> &shapeIds, int *nextId)
{
    QString spid;
    std::function<bool(Ooxml::XmlElement &)> findSpid = [&](Ooxml::XmlElement &e) {
        if (e.name == u"compatExt") {
            const QString old = e.attr(u"spid").mid(8); // _x0000_s1025 → 1025
            spid = shapeIds.value(old);
            if (spid.isEmpty()) {
                return false;
            }
            e.setAttribute(QString(), QStringLiteral("spid"), QStringLiteral("_x0000_s") + spid);
        }
        for (Ooxml::XmlChild &child : e.children) {
            if (child.element && !findSpid(*child.element)) {
                return false;
            }
        }
        return true;
    };
    if (!findSpid(element)) {
        return false;
    }
    std::function<void(Ooxml::XmlElement &)> number = [&](Ooxml::XmlElement &e) {
        if (e.ns == kXdr && e.name == u"cNvPr") {
            e.setAttribute(QString(), QStringLiteral("id"), spid.isEmpty() ? QString::number((*nextId)++) : spid);
        }
        for (Ooxml::XmlChild &child : e.children) {
            if (child.element) {
                number(*child.element);
            }
        }
    };
    number(element);
    return true;
}

bool carryAnchors(Context &ctx, const DestSheet &dest, const QString &sourceDrawing,
                  const QHash<QString, QString> &shapeIds, QString *error)
{
    const Ooxml::XmlElementPtr root = parse(ctx.source().data(sourceDrawing));
    if (!root) {
        return true;
    }
    QList<Ooxml::XmlElementPtr> carried;
    qint32 index = -1;
    int dropped = 0;
    for (const Ooxml::XmlChild &child : root->children) {
        if (!child.element) {
            continue;
        }
        ++index;
        if (dest.patch->source.modelledObjects.contains(index)) {
            continue; // a chart or a picture: written from the model
        }
        Ooxml::XmlElementPtr copy = child.element->clone();
        if (!moveAnchors(*copy, dest.patch->source.steps)) {
            ++dropped;
            continue;
        }
        carried.append(copy);
    }
    if (dropped > 0) {
        ctx.warn(QObject::tr("%n drawing object(s) on “%1” were not kept: the cells they stood on were deleted.", nullptr,
                             dropped)
                     .arg(dest.name));
    }
    if (carried.isEmpty()) {
        return true;
    }
    QString destDrawing = writtenTarget(ctx, dest.part, QStringLiteral("/drawing"));
    bool created = false;
    if (destDrawing.isEmpty()) {
        destDrawing = ctx.addNewPart(QStringLiteral("xl/drawings/drawing"), QStringLiteral(".xml"), QByteArray(),
                                     QStringLiteral("application/vnd.openxmlformats-officedocument.drawing+xml"));
        created = true;
        if (!attachToSheet(ctx, dest, kDrawingType, destDrawing, QStringLiteral("drawing"),
                           {QStringLiteral("legacyDrawing"), QStringLiteral("legacyDrawingHF"), QStringLiteral("drawingHF"),
                            QStringLiteral("picture"), QStringLiteral("oleObjects"), QStringLiteral("controls"),
                            QStringLiteral("webPublishItems"), QStringLiteral("tableParts"), QStringLiteral("extLst")},
                           error)) {
            return false;
        }
    }
    QString target = created
        ? QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                         "<xdr:wsDr xmlns:xdr=\"%1\" xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\"></xdr:wsDr>")
              .arg(kXdr)
        : QString::fromUtf8(ctx.part(destDrawing));
    int nextId = 1;
    static const QRegularExpression ids(QStringLiteral("\\sid=\"(\\d+)\""));
    for (auto it = ids.globalMatch(target); it.hasNext();) {
        nextId = qMax(nextId, it.next().captured(1).toInt() + 1);
    }
    QString inserted;
    for (const Ooxml::XmlElementPtr &anchor : std::as_const(carried)) {
        if (!renumberShapes(*anchor, shapeIds, &nextId)) {
            continue; // a control whose legacy shape could not be carried
        }
        ctx.rewrite(*anchor, sourceDrawing, destDrawing);
        inserted += Ooxml::serializeFragment(*anchor);
    }
    const qsizetype close = target.lastIndexOf(QLatin1String("</xdr:wsDr>"));
    if (close < 0) {
        if (error != nullptr) {
            *error = QStringLiteral("carry: malformed drawing %1").arg(destDrawing);
        }
        return false;
    }
    target.insert(close, inserted);
    if (created) {
        ctx.replaceCopied(destDrawing, target.toUtf8());
    } else {
        ctx.setPart(destDrawing, target.toUtf8());
    }
    return true;
}

} // namespace

bool carrySheetDrawings(Context &ctx, const DestSheet &dest, int *drawingPartCount, QHash<QString, QString> *shapeIds,
                        QString *error)
{
    Q_UNUSED(drawingPartCount); // new drawings take the first free name
    const QString sourceSheet = dest.patch->source.part;
    const QString sourceVml = sourceTarget(ctx.source(), sourceSheet, QStringLiteral("vmlDrawing"));
    if (!sourceVml.isEmpty() && !carryVmlShapes(ctx, dest, sourceVml, shapeIds, error)) {
        return false;
    }
    const QString sourceDrawing = sourceTarget(ctx.source(), sourceSheet, QStringLiteral("drawing"));
    return sourceDrawing.isEmpty() || carryAnchors(ctx, dest, sourceDrawing, *shapeIds, error);
}

} // namespace QindaOffice::Calc::IO::Carry
