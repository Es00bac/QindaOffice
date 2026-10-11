// SPDX-License-Identifier: GPL-3.0-or-later
// QindaShow's own shapes spelled as PresentationML (pptx_write_shapes.h):
// p:sp, p:cxnSp, p:pic, p:grpSp and table graphic frames (the a:tbl itself
// is pptx_write_table.cpp), and the picture a chart or preserved element
// becomes when its source part is gone.

#include "pptx_write_shapes.h"
#include "pptx_equations.h"

#include "render_parts.h"

#include <QBuffer>
#include <QImage>
#include <QPainter>

#include <cmath>

namespace QindaOffice::Show::Pptx {

namespace {

using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;

XmlElementPtr make(const QString &ns, const QString &name)
{
    return std::make_shared<XmlElement>(ns, name);
}

void set(XmlElement &e, const QString &name, const QString &value)
{
    e.setAttribute(QString(), name, value);
}

XmlElement &nonVisual(PartWriter &out, XmlElement &shape, const char *wrapper, const char *specific,
                      const Shape &model, quint32 id)
{
    XmlElement &nv = shape.appendElement(nsP(), QLatin1String(wrapper));
    XmlElement &cNvPr = nv.appendElement(nsP(), QStringLiteral("cNvPr"));
    set(cNvPr, QStringLiteral("id"), QString::number(id));
    set(cNvPr, QStringLiteral("name"), defaultName(model, id));
    if (!model.description.isEmpty()) {
        set(cNvPr, QStringLiteral("descr"), model.description);
    }
    if (model.hidden) {
        set(cNvPr, QStringLiteral("hidden"), QStringLiteral("1"));
    }
    // M91 (D-099): action settings on click and on mouse-over.
    if (XmlElementPtr link = out.action(model.click, QStringLiteral("hlinkClick"))) {
        cNvPr.appendElement(link);
    }
    if (XmlElementPtr link = out.action(model.hover, QStringLiteral("hlinkHover"))) {
        cNvPr.appendElement(link);
    }
    XmlElement &specificProps = nv.appendElement(nsP(), QLatin1String(specific));
    XmlElement &nvPr = nv.appendElement(nsP(), QStringLiteral("nvPr"));
    // M75: a placeholder QindaShow writes itself (a new layout's, one a
    // layout duplicate brought along) is a placeholder in the file too.
    if (model.placeholder.isPlaceholder()) {
        if (QLatin1String(specific) == QLatin1String("cNvSpPr")) {
            set(specificProps.appendElement(nsA(), QStringLiteral("spLocks")), QStringLiteral("noGrp"), QStringLiteral("1"));
        }
        XmlElement &ph = nvPr.appendElement(nsP(), QStringLiteral("ph"));
        if (model.placeholder.type != u"obj") {
            set(ph, QStringLiteral("type"), model.placeholder.type);
        }
        if (!model.placeholder.orient.isEmpty()) {
            set(ph, QStringLiteral("orient"), model.placeholder.orient);
        }
        if (!model.placeholder.size.isEmpty()) {
            set(ph, QStringLiteral("sz"), model.placeholder.size);
        }
        if (model.placeholder.index >= 0) {
            set(ph, QStringLiteral("idx"), QString::number(model.placeholder.index));
        }
    }
    return specificProps;
}

// M75 (D-084): a video or sound as PowerPoint embeds one — the media part
// related twice from the slide (.../video or .../audio, and Office 2010's
// .../media named by p14:media), clicking the picture plays it.
void mediaReferences(PartWriter &out, XmlElement &pic, const MediaInfo &media)
{
    XmlElement *nv = pic.child(nsP(), u"nvPicPr");
    XmlElement *cNvPr = nv != nullptr ? nv->child(nsP(), u"cNvPr") : nullptr;
    XmlElement *nvPr = nv != nullptr ? nv->child(nsP(), u"nvPr") : nullptr;
    if (cNvPr == nullptr || nvPr == nullptr) {
        return;
    }
    if (cNvPr->child(nsA(), u"hlinkClick") == nullptr) {
        XmlElement &link = cNvPr->insertElementBefore(make(nsA(), QStringLiteral("hlinkClick")), {u"hlinkHover", u"extLst"});
        link.setAttribute(nsR(), QStringLiteral("id"), QString());
        set(link, QStringLiteral("action"), QStringLiteral("ppaction://media"));
    }
    const bool audio = media.kind == MediaKind::Audio;
    QString fileRel;
    QString mediaRel;
    const QString kindType = relType(audio ? "audio" : "video");
    const QString mediaType = QStringLiteral("http://schemas.microsoft.com/office/2007/relationships/media");
    if (media.data.isEmpty()) {
        fileRel = out.relate(kindType, media.link, true);
        mediaRel = out.relate(mediaType, media.link, true);
    } else {
        const QString part = out.session.mediaFile(media.data, media.mime, media.name);
        fileRel = out.relate(kindType, part);
        mediaRel = out.relate(mediaType, part);
    }
    XmlElement &file = nvPr->insertElementBefore(make(nsA(), audio ? QStringLiteral("audioFile") : QStringLiteral("videoFile")),
                                                 {u"custDataLst", u"extLst"});
    file.setAttribute(nsR(), QStringLiteral("link"), fileRel);
    XmlElement &ext = nvPr->appendElement(nsP(), QStringLiteral("extLst")).appendElement(nsP(), QStringLiteral("ext"));
    set(ext, QStringLiteral("uri"), QStringLiteral("{DAA4B4D4-6D71-4841-9C94-3DA2E9CE8D0B}"));
    XmlElement &p14 = ext.appendElement(QStringLiteral("http://schemas.microsoft.com/office/powerpoint/2010/main"),
                                         QStringLiteral("media"));
    p14.setAttribute(nsR(), media.data.isEmpty() ? QStringLiteral("link") : QStringLiteral("embed"), mediaRel);
    writeMediaEdits(p14, media); // M91: trim, fades, bookmarks
    writeNarration(*nvPr, media.narration);
}

XmlElementPtr geometryElement(const Shape &shape)
{
    if (!shape.customGeometry.isEmpty()) {
        if (XmlElementPtr custom = Ooxml::parseFragment(shape.customGeometry)) {
            return custom;
        }
    }
    QString preset = shape.geometry;
    if (preset.isEmpty()) {
        preset = shape.kind == ShapeKind::Ellipse ? QStringLiteral("ellipse")
            : shape.kind == ShapeKind::Line || shape.kind == ShapeKind::Arrow
            ? (shape.connector ? QStringLiteral("straightConnector1") : QStringLiteral("line"))
            : QStringLiteral("rect");
    }
    XmlElementPtr geometry = make(nsA(), QStringLiteral("prstGeom"));
    set(*geometry, QStringLiteral("prst"), preset);
    XmlElement &av = geometry->appendElement(nsA(), QStringLiteral("avLst"));
    for (const auto &[name, value] : shape.adjust) {
        XmlElement &gd = av.appendElement(nsA(), QStringLiteral("gd"));
        set(gd, QStringLiteral("name"), name);
        set(gd, QStringLiteral("fmla"), QStringLiteral("val %1").arg(value));
    }
    return geometry;
}

LineStyle outlineFor(const Shape &shape)
{
    LineStyle line = shape.line;
    if (shape.kind == ShapeKind::Arrow && line.head.isNone() && line.tail.isNone()) {
        line.tail.type = QStringLiteral("triangle"); // the pre-M62 Arrow's own head
    }
    return line;
}

XmlElementPtr spShape(PartWriter &out, const Shape &shape, quint32 id)
{
    const bool lineKind = shape.kind == ShapeKind::Line || shape.kind == ShapeKind::Arrow;
    XmlElementPtr sp = make(nsP(), lineKind && shape.connector ? QStringLiteral("cxnSp") : QStringLiteral("sp"));
    if (sp->name == u"cxnSp") {
        XmlElement &connection = nonVisual(out, *sp, "nvCxnSpPr", "cNvCxnSpPr", shape, id);
        for (const auto &[end, name] : {std::pair{&shape.start, "stCxn"}, std::pair{&shape.end, "endCxn"}}) {
            if (end->shapeId != 0 && end->shapeId != id) {
                XmlElement &cx = connection.appendElement(nsA(), QLatin1String(name));
                set(cx, QStringLiteral("id"), QString::number(end->shapeId));
                set(cx, QStringLiteral("idx"), QString::number(end->site));
            }
        }
    } else {
        XmlElement &props = nonVisual(out, *sp, "nvSpPr", "cNvSpPr", shape, id);
        if (shape.kind == ShapeKind::TextBox) {
            set(props, QStringLiteral("txBox"), QStringLiteral("1"));
        }
    }
    XmlElement &spPr = sp->appendElement(nsP(), QStringLiteral("spPr"));
    spPr.appendElement(transformElement(nsA(), shape, false));
    spPr.appendElement(geometryElement(shape));
    if (lineKind) {
        spPr.appendElement(lineElement(shape.stroke, shape.strokeWidth > 0 ? shape.strokeWidth : 1.5, outlineFor(shape), true));
        return sp;
    }
    spPr.appendElement(fillElement(out, shape.fill, shape.fillStyle));
    if (shape.kind != ShapeKind::TextBox || shape.framed) {
        spPr.appendElement(lineElement(shape.stroke, shape.strokeWidth, outlineFor(shape), shape.strokeWidth > 0));
    }
    if (!shape.paragraphs.isEmpty()) {
        sp->appendElement(textBodyElement(out, nsP(), shape.paragraphs, shape.textBody));
    }
    return sp;
}

XmlElementPtr picture(PartWriter &out, const Shape &shape, quint32 id, const QByteArray &bytes, const QString &mime)
{
    XmlElementPtr pic = make(nsP(), QStringLiteral("pic"));
    XmlElement &locks = nonVisual(out, *pic, "nvPicPr", "cNvPicPr", shape, id);
    set(locks.appendElement(nsA(), QStringLiteral("picLocks")), QStringLiteral("noChangeAspect"), QStringLiteral("1"));
    XmlElement &blipFill = pic->appendElement(nsP(), QStringLiteral("blipFill"));
    blipFill.appendElement(nsA(), QStringLiteral("blip"))
        .setAttribute(nsR(), QStringLiteral("embed"), out.relate(relType("image"), out.session.media(bytes, mime)));
    if (shape.image.isCropped()) {
        XmlElement &src = blipFill.appendElement(nsA(), QStringLiteral("srcRect"));
        const auto edge = [&src](const char *name, qreal value) {
            if (value != 0) {
                set(src, QLatin1String(name), QString::number(qint64(std::llround(value * 100000))));
            }
        };
        edge("l", shape.image.cropLeft);
        edge("t", shape.image.cropTop);
        edge("r", shape.image.cropRight);
        edge("b", shape.image.cropBottom);
    }
    blipFill.appendElement(nsA(), QStringLiteral("stretch")).appendElement(nsA(), QStringLiteral("fillRect"));
    XmlElement &spPr = pic->appendElement(nsP(), QStringLiteral("spPr"));
    spPr.appendElement(transformElement(nsA(), shape, false));
    spPr.appendElement(geometryElement(shape));
    if (shape.framed) {
        spPr.appendElement(lineElement(shape.stroke, shape.strokeWidth, shape.line, shape.strokeWidth > 0));
    }
    if (shape.media.isMedia()) {
        mediaReferences(out, *pic, shape.media);
    }
    return pic;
}

XmlElementPtr group(PartWriter &out, const Shape &shape, quint32 id)
{
    XmlElementPtr grpSp = make(nsP(), QStringLiteral("grpSp"));
    nonVisual(out, *grpSp, "nvGrpSpPr", "cNvGrpSpPr", shape, id);
    grpSp->appendElement(nsP(), QStringLiteral("grpSpPr")).appendElement(transformElement(nsA(), shape, true));
    for (const Shape &child : shape.children) {
        if (XmlElementPtr written = writeShape(out, child, out.claim(child.nvId))) {
            grpSp->appendElement(written);
        }
    }
    return grpSp;
}

} // namespace

QString defaultName(const Shape &shape, quint32 id)
{
    if (!shape.name.isEmpty()) {
        return shape.name;
    }
    static const char *const kNames[] = {"TextBox", "Rectangle", "Oval",  "Straight Connector", "Straight Arrow Connector",
                                         "Picture", "Shape",     "Table", "Chart",              "Group",
                                         "Object", "Equation"};
    return QStringLiteral("%1 %2").arg(QLatin1String(kNames[int(shape.kind)])).arg(id > 1 ? id - 1 : id);
}

void lineGeometry(const Shape &shape, QRectF *rect, bool *flipH, bool *flipV)
{
    *flipH = shape.flipH != (shape.rect.width() < 0);
    *flipV = shape.flipV != (shape.rect.height() < 0);
    *rect = shape.rect.normalized();
}

XmlElementPtr transformElement(const QString &ns, const Shape &shape, bool group)
{
    XmlElementPtr xfrm = make(ns, QStringLiteral("xfrm"));
    QRectF rect = shape.rect.normalized();
    bool flipH = shape.flipH;
    bool flipV = shape.flipV;
    if (shape.kind == ShapeKind::Line || shape.kind == ShapeKind::Arrow) {
        lineGeometry(shape, &rect, &flipH, &flipV);
    }
    const qint64 rot = qint64(std::llround(std::fmod(std::fmod(shape.rotation, 360.0) + 360.0, 360.0) * 60000)) % 21600000;
    if (rot != 0) {
        set(*xfrm, QStringLiteral("rot"), QString::number(rot));
    }
    if (flipH) {
        set(*xfrm, QStringLiteral("flipH"), QStringLiteral("1"));
    }
    if (flipV) {
        set(*xfrm, QStringLiteral("flipV"), QStringLiteral("1"));
    }
    XmlElement &off = xfrm->appendElement(nsA(), QStringLiteral("off"));
    set(off, QStringLiteral("x"), emu(rect.x()));
    set(off, QStringLiteral("y"), emu(rect.y()));
    XmlElement &ext = xfrm->appendElement(nsA(), QStringLiteral("ext"));
    set(ext, QStringLiteral("cx"), emu(qMax<qreal>(0.0, rect.width())));
    set(ext, QStringLiteral("cy"), emu(qMax<qreal>(0.0, rect.height())));
    if (group) {
        const QRectF space = shape.childRect.isValid() ? shape.childRect : rect;
        XmlElement &chOff = xfrm->appendElement(nsA(), QStringLiteral("chOff"));
        set(chOff, QStringLiteral("x"), emu(space.x()));
        set(chOff, QStringLiteral("y"), emu(space.y()));
        XmlElement &chExt = xfrm->appendElement(nsA(), QStringLiteral("chExt"));
        set(chExt, QStringLiteral("cx"), emu(qMax<qreal>(0.0, space.width())));
        set(chExt, QStringLiteral("cy"), emu(qMax<qreal>(0.0, space.height())));
    }
    return xfrm;
}

XmlElementPtr pictureFallback(PartWriter &out, const Shape &shape, quint32 id)
{
    // What QindaShow draws for it, at 2x, as a picture in its place.
    const QRectF rect = shape.rect.normalized();
    if (rect.isEmpty()) {
        return nullptr;
    }
    const qreal scale = qMin<qreal>(2.0, 4000.0 / qMax(rect.width(), rect.height()));
    QImage image(QSize(qMax(1, qRound(rect.width() * scale)), qMax(1, qRound(rect.height() * scale))),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(scale, scale);
        painter.translate(-rect.topLeft());
        Shape flat = shape;
        flat.rotation = 0;
        flat.flipH = false;
        flat.flipV = false;
        Render::paintShape(&painter, flat);
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    Shape placed = shape;
    placed.kind = ShapeKind::Image;
    placed.image = ImageInfo();
    placed.geometry.clear();
    placed.customGeometry.clear();
    placed.framed = false;
    if (placed.description.isEmpty()) {
        placed.description = shape.preserved.label;
    }
    return picture(out, placed, id, png, QStringLiteral("image/png"));
}

XmlElementPtr generateShape(PartWriter &out, const Shape &shape, quint32 id)
{
    switch (shape.kind) {
    case ShapeKind::Equation:
        return generateEquation(out, shape, id);
    case ShapeKind::TextBox:
    case ShapeKind::Rectangle:
    case ShapeKind::Ellipse:
    case ShapeKind::AutoShape:
    case ShapeKind::Line:
    case ShapeKind::Arrow:
        return spShape(out, shape, id);
    case ShapeKind::Image:
        if (shape.imagePng.isEmpty() && shape.media.isMedia()) {
            // A video or sound without a poster gets QindaShow's (M75).
            return picture(out, shape, id, mediaPoster(shape.media.kind, shape.media.name, QSize(640, 360)),
                           QStringLiteral("image/png"));
        }
        return shape.imagePng.isEmpty() ? nullptr : picture(out, shape, id, shape.imagePng, shape.image.mime);
    case ShapeKind::Group:
        return group(out, shape, id);
    case ShapeKind::Table: {
        XmlElementPtr frame = make(nsP(), QStringLiteral("graphicFrame"));
        XmlElement &locks = nonVisual(out, *frame, "nvGraphicFramePr", "cNvGraphicFramePr", shape, id);
        set(locks.appendElement(nsA(), QStringLiteral("graphicFrameLocks")), QStringLiteral("noGrp"), QStringLiteral("1"));
        frame->appendElement(transformElement(nsP(), shape, false));
        XmlElement &data = frame->appendElement(nsA(), QStringLiteral("graphic")).appendElement(nsA(), QStringLiteral("graphicData"));
        set(data, QStringLiteral("uri"), QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/table"));
        data.appendElement(tableElement(out, shape.table, nullptr, nullptr, QString()));
        return frame;
    }
    case ShapeKind::Chart:
    case ShapeKind::Preserved:
        return pictureFallback(out, shape, id);
    }
    return nullptr;
}

} // namespace QindaOffice::Show::Pptx
