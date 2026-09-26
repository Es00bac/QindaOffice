// SPDX-License-Identifier: GPL-3.0-or-later
#include "painter.h"

#include "geometry.h"
#include "stencils.h"
#include "text_paint.h"
#include "themes.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram {

namespace {

bool visible(const QColor &color)
{
    return color.isValid() && color.alpha() > 0;
}

QPen linePen(const QColor &color, double width, LinePattern pattern)
{
    QPen pen(color, std::max(0.1, width), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    switch (pattern) {
    case LinePattern::Solid:
        break;
    case LinePattern::Dash:
        pen.setCapStyle(Qt::FlatCap);
        pen.setDashPattern({4, 3});
        break;
    case LinePattern::Dot:
        pen.setDashPattern({0.01, 2.5});
        break;
    case LinePattern::DashDot:
        pen.setCapStyle(Qt::FlatCap);
        pen.setDashPattern({5, 2.5, 1, 2.5});
        break;
    }
    return pen;
}

// The shape's frame without the flip: text must never read mirrored.
QTransform textTransform(const Shape &shape)
{
    QTransform t;
    t.translate(shape.rect.center().x(), shape.rect.center().y());
    t.rotate(shape.rotation);
    t.translate(-shape.rect.width() / 2, -shape.rect.height() / 2);
    return t;
}

void paintShapeText(QPainter *painter, const Shape &shape)
{
    if (shape.text.isEmpty()) {
        return;
    }
    const Stencil *stencil = Diagram::stencil(shape.type);
    painter->save();
    painter->setTransform(textTransform(shape), true);
    const QRectF box = textRect(shape);
    if (shape.type == QLatin1String("umlClass")) {
        TextPaint::drawClass(painter, shape.text, shape.textStyle, shape.rect.width(), shape.style.line,
                             std::max(0.75, shape.style.lineWidth));
    } else if (stencil != nullptr && stencil->text == TextPlacement::Card) {
        TextPaint::drawCard(painter, shape.text, shape.textStyle, box);
    } else if (shape.type == QLatin1String("laneHorizontal")) {
        // The header band reads bottom-to-top, like a spine label.
        painter->translate(box.left(), box.bottom());
        painter->rotate(-90);
        TextPaint::drawBlock(painter, shape.text, shape.textStyle, QRectF(0, 0, box.height(), box.width()));
    } else {
        TextPaint::drawBlock(painter, shape.text, shape.textStyle, box);
    }
    painter->restore();
}

} // namespace

double arrowInset(ArrowStyle style, double lineWidth)
{
    const double length = 7 + lineWidth * 2.5;
    switch (style) {
    case ArrowStyle::None:
        return 0;
    case ArrowStyle::Open:
        return lineWidth * 0.6;
    case ArrowStyle::Filled:
        return length * 0.85;
    case ArrowStyle::Diamond:
        return length * 1.3;
    case ArrowStyle::Circle:
        return length * 0.75;
    }
    return 0;
}

void paintArrow(QPainter *painter, ArrowStyle style, QPointF tip, QPointF direction, const QColor &color,
                double lineWidth)
{
    if (style == ArrowStyle::None || direction.isNull()) {
        return;
    }
    const double length = 7 + lineWidth * 2.5;
    const double half = length * 0.42;
    const QPointF back = tip - direction * length;
    const QPointF normal(-direction.y(), direction.x());
    painter->save();
    painter->setPen(QPen(color, lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    switch (style) {
    case ArrowStyle::Filled: {
        painter->setBrush(color);
        painter->drawPolygon(QPolygonF({tip, back + normal * half, back - normal * half}));
        break;
    }
    case ArrowStyle::Open:
        painter->setBrush(Qt::NoBrush);
        painter->drawPolyline(QPolygonF({back + normal * half, tip, back - normal * half}));
        break;
    case ArrowStyle::Diamond: {
        const QPointF mid = tip - direction * (length * 0.7);
        const QPointF end = tip - direction * (length * 1.4);
        painter->setBrush(color);
        painter->drawPolygon(QPolygonF({tip, mid + normal * half * 0.8, end, mid - normal * half * 0.8}));
        break;
    }
    case ArrowStyle::Circle: {
        const double r = length * 0.36;
        painter->setBrush(color);
        painter->drawEllipse(tip - direction * r, r, r);
        break;
    }
    case ArrowStyle::None:
        break;
    }
    painter->restore();
}

void paintShape(QPainter *painter, const Shape &shape, bool withText)
{
    const ShapeStyle &style = shape.style;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setOpacity(painter->opacity() * std::clamp(style.opacity, 0.0, 1.0));
    painter->setTransform(shapeTransform(shape), true);
    const QPainterPath path = outline(shape);
    const bool drawOutline = paintsOutline(shape);
    if (drawOutline && style.shadow) {
        // A two-step soft drop shadow, below and to the right.
        painter->setPen(Qt::NoPen);
        painter->fillPath(path.translated(1.5, 2.5), QColor(15, 23, 42, 22));
        painter->fillPath(path.translated(3, 5), QColor(15, 23, 42, 16));
    }
    if (drawOutline && visible(style.fill)) {
        if (visible(style.gradient)) {
            QLinearGradient gradient(0, 0, 0, shape.rect.height());
            gradient.setColorAt(0, style.fill);
            gradient.setColorAt(1, style.gradient);
            painter->fillPath(path, gradient);
        } else {
            painter->fillPath(path, style.fill);
        }
    }
    const QColor ink = visible(style.line) ? style.line : shape.textStyle.color;
    paintGlyph(painter, shape, ink);
    if (drawOutline && visible(style.line) && style.lineWidth > 0) {
        painter->strokePath(path, linePen(style.line, style.lineWidth, style.pattern));
    }
    painter->restore();
    if (withText) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setRenderHint(QPainter::TextAntialiasing);
        painter->setOpacity(painter->opacity() * std::clamp(style.opacity, 0.0, 1.0));
        paintShapeText(painter, shape);
        painter->restore();
    }
}

void paintConnector(QPainter *painter, const Connector &connector, const Route &route, const QColor &labelBackground,
                    bool withText)
{
    if (route.points.size() < 2) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const double width = std::max(0.25, connector.lineWidth);
    const QPainterPath path = routePath(route, arrowInset(connector.startArrow, width),
                                        arrowInset(connector.endArrow, width));
    painter->setBrush(Qt::NoBrush);
    painter->setPen(linePen(connector.line, width, connector.pattern));
    painter->drawPath(path);
    paintArrow(painter, connector.startArrow, route.start(), route.startDirection, connector.line, width);
    paintArrow(painter, connector.endArrow, route.end(), route.endDirection, connector.line, width);
    if (withText && !connector.text.isEmpty()) {
        const QSizeF size = TextPaint::measure(connector.text, connector.textStyle);
        const QRectF box(route.labelPoint - QPointF(size.width() / 2, size.height() / 2), size);
        const QRectF plate = box.adjusted(-4, -2, 4, 2);
        painter->setPen(Qt::NoPen);
        painter->setBrush(visible(labelBackground) ? labelBackground : QColor(Qt::white));
        painter->drawRoundedRect(plate, 3, 3);
        TextStyle style = connector.textStyle;
        style.align = Qt::AlignCenter;
        TextPaint::drawBlock(painter, connector.text, style, box.adjusted(-1, 0, 1, 1));
    }
    painter->restore();
}

void paintPage(QPainter *painter, const Page &page, const PaintOptions &options)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    const QRectF paper(QPointF(0, 0), page.size);
    if (options.background) {
        painter->fillRect(paper, visible(page.background) ? page.background : QColor(Qt::white));
    }
    if (options.grid && page.gridSpacing > 1) {
        QPen minor(options.gridColor, 0);
        minor.setCosmetic(true);
        QColor majorColor = options.gridColor.darker(112);
        QPen major(majorColor, 0);
        major.setCosmetic(true);
        int index = 0;
        for (double x = page.gridSpacing; x < paper.width(); x += page.gridSpacing) {
            painter->setPen(++index % 4 == 0 ? major : minor);
            painter->drawLine(QPointF(x, 0), QPointF(x, paper.height()));
        }
        index = 0;
        for (double y = page.gridSpacing; y < paper.height(); y += page.gridSpacing) {
            painter->setPen(++index % 4 == 0 ? major : minor);
            painter->drawLine(QPointF(0, y), QPointF(paper.width(), y));
        }
    }
    const auto included = [&](const QString &id) { return options.only.isEmpty() || options.only.contains(id); };
    for (const Shape &shape : page.shapes) {
        if (included(shape.id)) {
            paintShape(painter, shape, !options.hideTextOf.contains(shape.id));
        }
    }
    QHash<QString, Route> local;
    if (options.routes == nullptr) {
        local = routePage(page);
    }
    const QHash<QString, Route> &routes = options.routes != nullptr ? *options.routes : local;
    const QColor labelBackground = visible(page.background) ? page.background : QColor(Qt::white);
    for (const Connector &connector : page.connectors) {
        if (included(connector.id)) {
            const auto it = routes.constFind(connector.id);
            const Route route = it != routes.constEnd() ? *it : routeConnector(page, connector);
            paintConnector(painter, connector, route, labelBackground, !options.hideTextOf.contains(connector.id));
        }
    }
    painter->restore();
}

QRectF textBox(const Shape &shape)
{
    const QRectF local = textRect(shape);
    const QPointF centre = textTransform(shape).map(local.center());
    return QRectF(centre - QPointF(local.width() / 2, local.height() / 2), local.size());
}

QRectF itemsBounds(const Page &page, const QSet<QString> &ids)
{
    QRectF bounds;
    const auto included = [&](const QString &id) { return ids.isEmpty() || ids.contains(id); };
    for (const Shape &shape : page.shapes) {
        if (!included(shape.id)) {
            continue;
        }
        QRectF r = shapeBounds(shape).adjusted(-6, -6, 6, 6); // shadow and line reach
        const Stencil *stencil = Diagram::stencil(shape.type);
        if (stencil != nullptr && stencil->text == TextPlacement::Below && !shape.text.isEmpty()) {
            const QSizeF text = TextPaint::measure(shape.text, shape.textStyle);
            const QRectF box = textBox(shape);
            const double w = std::min(box.width(), text.width() + 4);
            r |= QRectF(box.center().x() - w / 2, box.top(), w, text.height() + 4);
        }
        bounds = bounds.isNull() ? r : bounds | r;
    }
    for (const Connector &connector : page.connectors) {
        if (!included(connector.id)) {
            continue;
        }
        const Route route = routeConnector(page, connector);
        QRectF r = QPolygonF(routePolyline(route)).boundingRect().adjusted(-8, -8, 8, 8);
        if (!connector.text.isEmpty()) {
            const QSizeF size = TextPaint::measure(connector.text, connector.textStyle);
            r |= QRectF(route.labelPoint - QPointF(size.width() / 2 + 6, size.height() / 2 + 4),
                        size + QSizeF(12, 8));
        }
        bounds = bounds.isNull() ? r : bounds | r;
    }
    return bounds;
}

QImage stencilThumbnail(const QString &stencilId, const QString &themeId, int pixels)
{
    const Stencil *stencil = Diagram::stencil(stencilId);
    const int side = std::max(8, pixels);
    QImage image(side, side, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (stencil == nullptr) {
        return image;
    }
    const Theme &palette = theme(themeId);
    Shape shape;
    shape.type = stencil->id;
    shape.style = shapeStyleFor(palette, stencil);
    shape.textStyle = textStyleFor(palette, stencil);
    shape.style.shadow = false;
    const bool below = stencil->text == TextPlacement::Below;
    // Fit the default size in the square, leaving room for strokes.
    const QSizeF size = stencil->size;
    const double room = side * (below ? 0.78 : 0.9);
    const double scale = room / std::max(size.width(), size.height());
    shape.rect = QRectF(QPointF(0, 0), size);
    if (stencil->id == QLatin1String("text")) {
        shape.text = QStringLiteral("Aa");
        shape.textStyle.size = 22;
        shape.textStyle.bold = true;
    } else if (stencil->container) {
        shape.text = stencil->title;
        shape.textStyle.size = 22;
    } else if (stencil->id == QLatin1String("picture")) {
        return image;
    }
    shape.style.lineWidth = std::max(1.5, 1.6 / scale);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate((side - size.width() * scale) / 2, (side - size.height() * scale) / 2 - (below ? side * 0.06 : 0));
    painter.scale(scale, scale);
    paintShape(&painter, shape, !shape.text.isEmpty() && !below);
    return image;
}

} // namespace QindaOffice::Diagram
