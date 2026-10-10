// SPDX-License-Identifier: GPL-3.0-or-later
// One written 2-D shape, fresh or merged into its source <Shape>
// (visio_write_p.h).
#include "visio_write_cells.h"

#include "geometry.h"
#include "shape_path.h"
#include "stencils.h"
#include "visio_data.h"
#include "visio_geometry.h"
#include "visio_write_m87.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>

#include <algorithm>

namespace QindaOffice::Diagram::Visio {

namespace {

struct Outline {
    QPainterPath path; // normalized, y down
    bool noFill = false;
    bool noLine = false;
};

QString baseName(const Shape &shape)
{
    if (shape.type == QLatin1String("path")) {
        return QStringLiteral("Shape");
    }
    QString name = shape.type;
    if (!name.isEmpty()) {
        name[0] = name.at(0).toUpper();
    }
    return name;
}

// The model shape's look as Geometry sections: its outline (filled and
// stroked as the stencil paints it) and the strokes its glyph adds.
QList<Outline> outlinesOf(const Shape &shape)
{
    QList<Outline> out;
    if (!shape.paths.isEmpty()) {
        for (const ShapePath &p : shape.paths) {
            if (p.fill || p.stroke) {
                out.append({p.path, !p.fill, !p.stroke});
            }
        }
        return out;
    }
    const double w = std::max(1.0, shape.rect.width());
    const double h = std::max(1.0, shape.rect.height());
    const QTransform normalize = QTransform::fromScale(1 / w, 1 / h);
    if (shape.type == QLatin1String("picture")) {
        QPainterPath frame;
        frame.addRect(0, 0, 1, 1);
        out.append({frame, true, true});
        return out;
    }
    const bool paints = paintsOutline(shape);
    out.append({normalize.map(outline(shape)), !paints, !paints});
    QPainterPath strokes;
    for (const QPainterPath &glyph : glyphStrokes(shape)) {
        strokes.addPath(normalize.map(glyph));
    }
    if (!strokes.isEmpty()) {
        out.append({strokes, true, false});
    }
    return out;
}

void writeGeometry(XmlElement &element, const Shape &shape, int firstIx)
{
    int ix = firstIx;
    for (const Outline &o : outlinesOf(shape)) {
        insertChild(element, outlineSection(o.path, ix++, o.noFill, o.noLine));
    }
}

QRectF normalizedTextBox(const Shape &shape)
{
    const double w = std::max(1.0, shape.rect.width());
    const double h = std::max(1.0, shape.rect.height());
    const QRectF r = textRect(shape);
    return {r.x() / w, r.y() / h, r.width() / w, r.height() / h};
}

// A media part for picture bytes and the page's relationship to it.
QString addPicture(WriteContext &ctx, const QString &part, const QByteArray &bytes, QString *compression)
{
    QByteArray data = bytes;
    QBuffer buffer(&data);
    QImageReader reader(&buffer);
    QString format = QString::fromLatin1(reader.format()).toLower();
    if (format == QLatin1String("jpg")) {
        format = QStringLiteral("jpeg");
    }
    static const QStringList kept{QStringLiteral("png"), QStringLiteral("jpeg"), QStringLiteral("gif"),
                                  QStringLiteral("bmp"), QStringLiteral("tiff")};
    if (!kept.contains(format)) {
        // Anything else (webp, svg …) as PNG, which every Visio reads.
        QByteArray png;
        QBuffer out(&png);
        out.open(QIODevice::WriteOnly);
        QImage::fromData(bytes).save(&out, "PNG");
        data = png;
        format = QStringLiteral("png");
    }
    *compression = format.toUpper();
    const QString extension = format == QLatin1String("jpeg") ? QStringLiteral("jpg") : format;
    const QString media = ctx.writer->uniquePartName(QStringLiteral("visio/media/image"), QLatin1Char('.') + extension);
    ctx.writer->addPart(media, data, Ooxml::ContentType::forImageExtension(extension));
    return ctx.writer->addRelationship(part, QString::fromUtf16(Ooxml::RelType::Image), media);
}

XmlElementPtr foreignData(const QString &relId, const QString &compression)
{
    XmlElementPtr f = makeElement(QStringLiteral("ForeignData"));
    f->setAttribute({}, QStringLiteral("ForeignType"), QStringLiteral("Bitmap"));
    f->setAttribute({}, QStringLiteral("CompressionType"), compression);
    appendElement(*f, QStringLiteral("Rel")).setAttribute(QString::fromUtf16(Ooxml::Ns::R), QStringLiteral("id"), relId);
    return f;
}

void writePictureCells(XmlElement &element, const Placement &p)
{
    setCell(element, QStringLiteral("ImgOffsetX"), QStringLiteral("0"));
    setCell(element, QStringLiteral("ImgOffsetY"), QStringLiteral("0"));
    setCell(element, QStringLiteral("ImgWidth"), numberText(p.width), QStringLiteral("Width*1"));
    setCell(element, QStringLiteral("ImgHeight"), numberText(p.height), QStringLiteral("Height*1"));
}

QString familyOf(const WriteContext &ctx, const TextStyle &style)
{
    return style.family.isEmpty() ? ctx.defaultFont : style.family;
}

bool lineChanged(const ShapeStyle &a, const ShapeStyle &b)
{
    return a.line != b.line || a.lineWidth != b.lineWidth || a.pattern != b.pattern || a.opacity != b.opacity;
}

bool fillChanged(const ShapeStyle &a, const ShapeStyle &b)
{
    return a.fill != b.fill || a.gradient != b.gradient || a.shadow != b.shadow || a.opacity != b.opacity;
}

} // namespace

void setShapeNames(XmlElement &element, const QString &base, int id)
{
    const QString name = base + QLatin1Char('.') + QString::number(id);
    element.setAttribute({}, QStringLiteral("NameU"), name);
    element.setAttribute({}, QStringLiteral("Name"), name);
    element.removeAttribute({}, u"IsCustomNameU");
    element.removeAttribute({}, u"IsCustomName");
    element.removeAttribute({}, u"UniqueID"); // a copy is a different shape
}

XmlElementPtr freshShape(WriteContext &ctx, const Shape &shape, int id, const QTransform &toParent,
                         const QString &part, bool withPorts)
{
    Q_UNUSED(withPorts) // a fresh shape always lists its connection points
    const bool picture = shape.type == QLatin1String("picture") && !shape.image.isEmpty();
    XmlElementPtr e = makeElement(QStringLiteral("Shape"));
    e->setAttribute({}, QStringLiteral("ID"), QString::number(id));
    setShapeNames(*e, baseName(shape), id);
    e->setAttribute({}, QStringLiteral("Type"), picture ? QStringLiteral("Foreign") : QStringLiteral("Shape"));
    for (const char *style : {"LineStyle", "FillStyle", "TextStyle"}) {
        e->setAttribute({}, QString::fromLatin1(style), QString::number(ctx.shapeStyle));
    }
    const Placement p = placementOf(shape, toParent);
    writePlacement(*e, p);
    if (picture) {
        writePictureCells(*e, p);
        writeLine(*e, QColor(), 0, LinePattern::Solid);
        ShapeStyle none;
        none.fill = QColor(Qt::transparent);
        writeFill(*e, none);
    } else {
        writeLine(*e, shape.style.line, shape.style.lineWidth, shape.style.pattern, shape.style.opacity);
        ShapeStyle fill = shape.style;
        if (!paintsOutline(shape) && shape.paths.isEmpty()) {
            fill.fill = QColor(Qt::transparent); // text, actor: the model paints no outline
        }
        writeFill(*e, fill);
    }
    setCell(*e, QStringLiteral("ObjType"), QStringLiteral("1"));
    if (shape.aspectLocked) {
        setCell(*e, QStringLiteral("LockAspect"), QStringLiteral("1"));
    }
    writeTextBlock(*e, normalizedTextBox(shape), p, shape.textStyle);
    if (!shape.text.isEmpty()) {
        writeText(*e, shape.text, shape.textStyle, familyOf(ctx, shape.textStyle));
    }
    if (!shape.data.isEmpty()) {
        insertChild(*e, dataSection(shape.data, linkedColumns(*ctx.drawing, shape)));
    }
    const QVector<QPointF> ports = portsOf(shape);
    if (!ports.isEmpty()) {
        insertChild(*e, connectionSection(QList<QPointF>(ports.cbegin(), ports.cend()), p.width, p.height));
    }
    writeGeometry(*e, shape, 0);
    // M87 (D-095): layers, hyperlinks, Visio's container cells.
    if (!shape.layers.isEmpty()) {
        writeLayerMember(*e, shape.layers);
    }
    if (!shape.links.isEmpty()) {
        writeHyperlinks(*e, shape.links);
    }
    writeContainerCells(*e, shape, p.pxPerInch);
    if (picture) {
        QString compression;
        const QString rel = addPicture(ctx, part, shape.image, &compression);
        e->appendElement(foreignData(rel, compression));
    }
    return e;
}

void updateShape(WriteContext &ctx, XmlElement &element, const Shape &shape, const Shape &snapshot,
                 const QTransform &toParent, FrameChange frame, const QString &part, bool withPorts,
                 const XmlElement *master)
{
    // The source's own view (with its master) before anything changes.
    const Sheet masterSheet = master != nullptr ? Sheet::merge(*master, nullptr) : Sheet();
    const Sheet merged = Sheet::merge(element, master != nullptr ? &masterSheet : nullptr);
    const bool sizeChanged = shape.rect.size() != snapshot.rect.size();
    const bool reshaped = frame.changed || sizeChanged || shape.rotation != snapshot.rotation
                       || shape.flipX != snapshot.flipX || shape.flipY != snapshot.flipY;
    const QPointF moved = toParent.map(shape.rect.center()) - toParent.map(snapshot.rect.center()) + frame.shift;
    const Placement p = placementOf(shape, toParent);
    if (reshaped) {
        writePlacement(element, p);
        if (merged.foreignData() != nullptr) {
            writePictureCells(element, p);
        }
    } else if (!moved.isNull()) {
        // Only the pin moves, by as much as the shape did: the pin may sit
        // anywhere in the shape (LocPinX), and every other placement
        // formula is kept.
        const Cell *x = merged.cell(u"PinX");
        const Cell *y = merged.cell(u"PinY");
        setCell(element, QStringLiteral("PinX"), numberText((x != nullptr ? x->number().value_or(0) : 0) + moved.x()));
        setCell(element, QStringLiteral("PinY"), numberText((y != nullptr ? y->number().value_or(0) : 0) + moved.y()));
    }
    if (lineChanged(shape.style, snapshot.style)) {
        writeLine(element, shape.style.line, shape.style.lineWidth, shape.style.pattern, shape.style.opacity);
    }
    if (fillChanged(shape.style, snapshot.style)) {
        writeFill(element, shape.style);
    }
    if (shape.text != snapshot.text || shape.textStyle != snapshot.textStyle) {
        writeText(element, shape.text, shape.textStyle, familyOf(ctx, shape.textStyle));
    }
    if (!shape.text.isEmpty() && (shape.textBox != snapshot.textBox || sizeChanged || shape.textStyle.align != snapshot.textStyle.align)) {
        writeTextBlock(element, normalizedTextBox(shape), p, shape.textStyle);
    }
    if (shape.data != snapshot.data || shape.dataLink != snapshot.dataLink) {
        updateData(element, snapshot.data, shape.data, merged, master != nullptr ? &masterSheet : nullptr,
                   linkedColumns(*ctx.drawing, shape));
    }
    if (merged.foreignData() == nullptr && (shape.paths != snapshot.paths || sizeChanged)) {
        writeGeometry(element, shape, clearGeometry(element, master));
    }
    if (merged.foreignData() != nullptr && shape.image != snapshot.image && !shape.image.isEmpty()) {
        element.removeChildren(kNs, u"ForeignData");
        QString compression;
        const QString rel = addPicture(ctx, part, shape.image, &compression);
        insertChild(element, foreignData(rel, compression));
    }
    if (withPorts && merged.section(u"Connection") == nullptr) {
        const QVector<QPointF> ports = portsOf(shape);
        insertChild(element, connectionSection(QList<QPointF>(ports.cbegin(), ports.cend()), p.width, p.height));
    }
    if (shape.aspectLocked != snapshot.aspectLocked) {
        setCell(element, QStringLiteral("LockAspect"), shape.aspectLocked ? QStringLiteral("1") : QStringLiteral("0"));
    }
    // M87 (D-095): what changed of the layers, links and container cells.
    if (shape.layers != snapshot.layers) {
        writeLayerMember(element, shape.layers);
    }
    if (shape.links != snapshot.links) {
        writeHyperlinks(element, shape.links, master != nullptr ? &masterSheet : nullptr);
    }
    if (shape.structure != snapshot.structure || shape.containerResize != snapshot.containerResize
        || shape.containerMargin != snapshot.containerMargin || shape.type != snapshot.type) {
        writeContainerCells(element, shape, p.pxPerInch);
    }
}

} // namespace QindaOffice::Diagram::Visio
