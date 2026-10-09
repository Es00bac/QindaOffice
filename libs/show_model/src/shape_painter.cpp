// SPDX-License-Identifier: GPL-3.0-or-later
// Shapes of every kind for the renderer: geometry (presets, custom paths),
// fills (plain, gradient, picture, pattern), outlines (dash, cap, join),
// arrowheads, pictures with their crop, groups in their child space, and
// the fallbacks for preserved Office content.

#include "preset_geometry.h"
#include "render_parts.h"
#include "text_painter.h"

#include <QImage>
#include <QPainter>
#include <QtMath>

namespace QindaOffice::Show::Render {

namespace {

QTransform mirror(const Shape &shape, const QRectF &rect)
{
    QTransform t;
    if (shape.flipH || shape.flipV) {
        t.translate(rect.center().x(), rect.center().y());
        t.scale(shape.flipH ? -1 : 1, shape.flipV ? -1 : 1);
        t.translate(-rect.center().x(), -rect.center().y());
    }
    return t;
}

QString geometryName(const Shape &shape)
{
    if (!shape.geometry.isEmpty()) {
        return shape.geometry;
    }
    return shape.kind == ShapeKind::Ellipse ? QStringLiteral("ellipse") : QStringLiteral("rect");
}

Geometry::Outline outlineOf(const Shape &shape, const QRectF &rect)
{
    Geometry::Outline outline;
    if (!shape.customGeometry.isEmpty()) {
        outline = Geometry::customOutline(shape.customGeometry, rect);
    } else {
        outline = Geometry::presetOutline(geometryName(shape), rect, shape.adjust);
    }
    if (outline.isEmpty()) {
        QPainterPath box;
        box.addRect(rect);
        outline = {box, box};
    }
    return outline;
}

QBrush gradientBrush(const FillStyle &style, const QRectF &bounds)
{
    QGradient gradient;
    if (style.path.isEmpty()) {
        const qreal radians = qDegreesToRadians(style.angle);
        const QPointF direction(std::cos(radians), std::sin(radians));
        const qreal half = (std::abs(bounds.width() * direction.x()) + std::abs(bounds.height() * direction.y())) / 2.0;
        gradient = QLinearGradient(bounds.center() - direction * half, bounds.center() + direction * half);
    } else {
        gradient = QRadialGradient(bounds.center(), std::hypot(bounds.width(), bounds.height()) / 2.0);
    }
    for (const GradientStop &stop : style.stops) {
        gradient.setColorAt(qBound(0.0, stop.position, 1.0), stop.color);
    }
    return QBrush(gradient);
}

Qt::BrushStyle patternStyle(const QString &name)
{
    if (name.contains(QLatin1String("Horz")) || name == u"horz") {
        return Qt::HorPattern;
    }
    if (name.contains(QLatin1String("Vert")) || name == u"vert") {
        return Qt::VerPattern;
    }
    if (name.contains(QLatin1String("DnDiag")) || name == u"dnDiag") {
        return Qt::FDiagPattern;
    }
    if (name.contains(QLatin1String("UpDiag")) || name == u"upDiag") {
        return Qt::BDiagPattern;
    }
    if (name.contains(QLatin1String("Grid")) || name == u"cross") {
        return Qt::CrossPattern;
    }
    if (name.contains(QLatin1String("diagCross")) || name.contains(QLatin1String("Check"))) {
        return Qt::DiagCrossPattern;
    }
    if (name.startsWith(QLatin1String("pct"))) {
        const int pct = name.mid(3).toInt();
        return pct >= 75 ? Qt::Dense2Pattern : pct >= 50 ? Qt::Dense4Pattern : pct >= 25 ? Qt::Dense5Pattern : Qt::Dense6Pattern;
    }
    return Qt::Dense4Pattern;
}

QList<qreal> dashPattern(const QString &dash)
{
    if (dash == u"dash") {
        return {4, 3};
    }
    if (dash == u"dashDot") {
        return {4, 3, 1, 3};
    }
    if (dash == u"dot") {
        return {1, 3};
    }
    if (dash == u"lgDash") {
        return {8, 3};
    }
    if (dash == u"lgDashDot") {
        return {8, 3, 1, 3};
    }
    if (dash == u"lgDashDotDot") {
        return {8, 3, 1, 3, 1, 3};
    }
    if (dash == u"sysDash") {
        return {3, 1};
    }
    if (dash == u"sysDashDot") {
        return {3, 1, 1, 1};
    }
    if (dash == u"sysDashDotDot") {
        return {3, 1, 1, 1, 1, 1};
    }
    if (dash == u"sysDot") {
        return {1, 1};
    }
    return {};
}

// An arrowhead at `tip`, arriving from `from`.
void paintLineEnd(QPainter *painter, const LineEnd &end, const QPointF &tip, const QPointF &from, const QPen &pen,
                  bool legacy)
{
    const QLineF axis(from, tip);
    if (axis.length() < 0.01 || end.isNone() || pen.style() == Qt::NoPen) {
        return;
    }
    const qreal unit = qMax<qreal>(pen.widthF(), 1.5);
    const auto factor = [](const QString &size) { return size == u"sm" ? 2.0 : size == u"lg" ? 5.0 : 3.0; };
    // The pre-M62 Arrow kind drew fixed 12 pt barbs; keep its look.
    const qreal length = legacy ? 10.4 : unit * factor(end.length);
    const qreal halfWidth = legacy ? 6.0 : unit * factor(end.width) / 2.0;
    const QLineF unitBack = QLineF(tip, from).unitVector();
    const QPointF back(unitBack.dx(), unitBack.dy());
    const QPointF normal(-back.y(), back.x());
    const QPointF base = tip + back * length;
    painter->save();
    QPen outline = pen;
    outline.setStyle(Qt::SolidLine);
    outline.setJoinStyle(Qt::MiterJoin);
    painter->setPen(outline);
    painter->setBrush(pen.color());
    if (end.type == u"arrow") {
        painter->setBrush(Qt::NoBrush);
        painter->drawPolyline(QPolygonF({base + normal * halfWidth, tip, base - normal * halfWidth}));
    } else if (end.type == u"oval") {
        painter->drawEllipse(tip, halfWidth, halfWidth);
    } else if (end.type == u"diamond") {
        const QPointF mid = tip + back * (length / 2.0);
        painter->drawPolygon(QPolygonF({tip, mid + normal * halfWidth, base, mid - normal * halfWidth}));
    } else if (end.type == u"stealth") {
        const QPointF notch = tip + back * (length * 0.6);
        painter->drawPolygon(QPolygonF({tip, base + normal * halfWidth, notch, base - normal * halfWidth}));
    } else {
        painter->drawPolygon(QPolygonF({tip, base + normal * halfWidth, base - normal * halfWidth}));
    }
    painter->restore();
}

void paintLine(QPainter *painter, const Shape &shape)
{
    QPen pen = outlinePen(shape.stroke, shape.strokeWidth > 0 ? shape.strokeWidth : 1.5, shape.line);
    if (pen.style() == Qt::NoPen) {
        return;
    }
    const QString name = shape.geometry;
    const bool bent = name.startsWith(QLatin1String("bent")) || name.startsWith(QLatin1String("curved"));
    QPainterPath path;
    if (bent) {
        const QRectF box = shape.rect.normalized();
        path = mirror(shape, box).map(Geometry::presetOutline(name, box, shape.adjust).stroke);
    } else {
        // The pre-M62 spelling: the rect's own corners (its extents carry
        // the direction); flips swap the ends.
        QPointF from = shape.rect.topLeft();
        QPointF to = shape.rect.bottomRight();
        if (shape.flipH) {
            std::swap(from.rx(), to.rx());
        }
        if (shape.flipV) {
            std::swap(from.ry(), to.ry());
        }
        path.moveTo(from);
        path.lineTo(to);
    }
    if (path.isEmpty()) {
        return;
    }
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
    LineEnd tail = shape.line.tail;
    const bool legacy = shape.kind == ShapeKind::Arrow && tail.isNone() && shape.line.head.isNone();
    if (legacy) {
        tail.type = QStringLiteral("triangle");
    }
    const QPointF start = path.pointAtPercent(0);
    const QPointF finish = path.pointAtPercent(1);
    const QPointF nearStart = path.elementCount() > 1 ? QPointF(path.elementAt(1)) : finish;
    const QPointF nearEnd = path.elementCount() > 1 ? QPointF(path.elementAt(path.elementCount() - 2)) : start;
    paintLineEnd(painter, shape.line.head, start, nearStart, pen, false);
    paintLineEnd(painter, tail, finish, nearEnd, pen, legacy);
}

void paintImage(QPainter *painter, const Shape &shape, const QPainterPath &clip)
{
    const QRectF target = shape.rect.normalized();
    QImage image;
    image.loadFromData(shape.imagePng);
    if (image.isNull() && shape.media.isMedia()) {
        // M75: a video or sound without a poster frame.
        paintPlaceholderBox(painter, target, shape.media.kind == MediaKind::Audio ? QStringLiteral("Audio") : QStringLiteral("Video"));
        return;
    }
    if (image.isNull()) {
        // Formats Qt cannot decode (EMF/WMF) keep their bytes; show where.
        painter->save();
        QPen dashed(QColor(120, 120, 120));
        dashed.setStyle(Qt::DashLine);
        dashed.setCosmetic(true);
        painter->setPen(dashed);
        painter->setBrush(Qt::NoBrush);
        painter->drawRect(target);
        painter->restore();
        return;
    }
    const ImageInfo &info = shape.image;
    const qreal visibleW = 1.0 - info.cropLeft - info.cropRight;
    const qreal visibleH = 1.0 - info.cropTop - info.cropBottom;
    QRectF placed = target;
    if (visibleW > 0.001 && visibleH > 0.001) {
        placed = QRectF(target.left() - info.cropLeft / visibleW * target.width(),
                        target.top() - info.cropTop / visibleH * target.height(), target.width() / visibleW,
                        target.height() / visibleH);
    }
    painter->save();
    painter->setClipPath(clip, Qt::IntersectClip);
    painter->setTransform(mirror(shape, target), true);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->drawImage(placed, image);
    painter->restore();
}

void paintChildren(QPainter *painter, const Shape &shape)
{
    const QRectF rect = shape.rect.normalized();
    painter->save();
    painter->setTransform(mirror(shape, rect), true);
    const QRectF space = shape.childRect.isValid() && shape.childRect.width() > 0 && shape.childRect.height() > 0
        ? shape.childRect
        : QRectF(QPointF(0, 0), rect.size());
    painter->translate(rect.topLeft());
    painter->scale(rect.width() / space.width(), rect.height() / space.height());
    painter->translate(-space.topLeft());
    for (const Shape &child : shape.children) {
        paintShape(painter, child);
    }
    painter->restore();
}

} // namespace

QPen outlinePen(const QColor &color, qreal width, const LineStyle &line)
{
    if (width <= 0 || !color.isValid() || color.alpha() == 0) {
        return Qt::NoPen;
    }
    QPen pen(color, width);
    pen.setCapStyle(line.cap == u"rnd" ? Qt::RoundCap : line.cap == u"sq" ? Qt::SquareCap : Qt::FlatCap);
    pen.setJoinStyle(line.join == u"bevel" ? Qt::BevelJoin : line.join == u"miter" ? Qt::MiterJoin : Qt::RoundJoin);
    const QList<qreal> pattern = dashPattern(line.dash);
    if (!pattern.isEmpty()) {
        pen.setDashPattern(pattern);
    }
    return pen;
}

void paintFill(QPainter *painter, const QPainterPath &path, const QColor &plain, const FillStyle &style)
{
    if (path.isEmpty()) {
        return;
    }
    switch (style.kind) {
    case FillKind::Plain:
        if (plain.isValid() && plain.alpha() > 0) {
            painter->fillPath(path, plain);
        }
        return;
    case FillKind::Gradient:
        painter->fillPath(path, gradientBrush(style, path.boundingRect()));
        return;
    case FillKind::Picture: {
        QImage image;
        image.loadFromData(style.image);
        if (image.isNull()) {
            painter->fillPath(path, QColor(230, 230, 230));
            return;
        }
        painter->save();
        painter->setClipPath(path, Qt::IntersectClip);
        painter->setOpacity(painter->opacity() * qBound(0.0, style.imageAlpha, 1.0));
        if (style.tile) {
            painter->fillPath(path, QBrush(image));
        } else {
            painter->setRenderHint(QPainter::SmoothPixmapTransform);
            painter->drawImage(path.boundingRect(), image);
        }
        painter->restore();
        return;
    }
    case FillKind::Pattern:
        if (style.background.isValid()) {
            painter->fillPath(path, style.background);
        }
        painter->fillPath(path, QBrush(style.foreground.isValid() ? style.foreground : QColor(Qt::black),
                                       patternStyle(style.pattern)));
        return;
    }
}

void paintPlaceholderBox(QPainter *painter, const QRectF &rect, const QString &label)
{
    painter->save();
    painter->fillRect(rect, QColor(242, 242, 242));
    QPen dashed(QColor(150, 150, 150), 1.0);
    dashed.setStyle(Qt::DashLine);
    painter->setPen(dashed);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(rect);
    TextRun run;
    run.text = label;
    run.color = QColor(110, 110, 110);
    run.pointSize = qBound<qreal>(6.0, rect.height() / 5.0, 14.0);
    TextParagraph paragraph;
    paragraph.alignment = int(Qt::AlignHCenter);
    paragraph.runs = {run};
    TextBodyStyle style;
    style.anchor = TextAnchor::Middle;
    paintText(painter, rect, {paragraph}, style);
    painter->restore();
}

void paintShape(QPainter *painter, const Shape &shape)
{
    if (shape.hidden) {
        return;
    }
    painter->save();
    const QRectF rect = shape.rect.normalized();
    if (shape.rotation != 0) {
        painter->translate(rect.center());
        painter->rotate(shape.rotation);
        painter->translate(-rect.center());
    }
    switch (shape.kind) {
    case ShapeKind::Line:
    case ShapeKind::Arrow:
        paintLine(painter, shape);
        break;
    case ShapeKind::Group:
        paintChildren(painter, shape);
        break;
    case ShapeKind::Table:
        paintTable(painter, rect, shape.table);
        break;
    case ShapeKind::Chart:
        if (shape.chart.isEmpty() && !shape.children.empty()) {
            paintChildren(painter, shape);
        } else {
            paintChart(painter, rect, shape.chart);
        }
        break;
    case ShapeKind::Preserved:
        if (!shape.children.empty()) {
            paintChildren(painter, shape);
        } else if (!shape.imagePng.isEmpty()) {
            QPainterPath box;
            box.addRect(rect);
            paintImage(painter, shape, box);
        } else {
            paintPlaceholderBox(painter, rect, shape.preserved.label.isEmpty() ? QStringLiteral("Office content")
                                                                               : shape.preserved.label);
        }
        break;
    case ShapeKind::Image: {
        const Geometry::Outline outline = outlineOf(shape, rect);
        const QPainterPath area = mirror(shape, rect).map(outline.fill.isEmpty() ? outline.stroke : outline.fill);
        paintFill(painter, area, shape.fill, shape.fillStyle);
        paintImage(painter, shape, area);
        if (shape.framed) {
            painter->strokePath(mirror(shape, rect).map(outline.stroke), outlinePen(shape.stroke, shape.strokeWidth, shape.line));
        }
        break;
    }
    case ShapeKind::TextBox:
    case ShapeKind::Rectangle:
    case ShapeKind::Ellipse:
    case ShapeKind::AutoShape: {
        const Geometry::Outline outline = outlineOf(shape, rect);
        const QTransform flip = mirror(shape, rect);
        paintFill(painter, flip.map(outline.fill), shape.fill, shape.fillStyle);
        const bool stroked = shape.kind != ShapeKind::TextBox || shape.framed;
        const QPen pen = outlinePen(shape.stroke, shape.strokeWidth, shape.line);
        if (stroked && pen.style() != Qt::NoPen) {
            painter->strokePath(flip.map(outline.stroke), pen);
        }
        const QRectF textBox = shape.kind == ShapeKind::TextBox ? rect : Geometry::textRect(geometryName(shape), rect, shape.adjust);
        paintText(painter, textBox, shape.paragraphs, shape.textBody);
        break;
    }
    }
    painter->restore();
}

} // namespace QindaOffice::Show::Render
