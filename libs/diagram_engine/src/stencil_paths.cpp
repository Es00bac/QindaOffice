// SPDX-License-Identifier: GPL-3.0-or-later
// Stencil geometry in local space: outlines, text boxes and header bands.
#include "stencils.h"

#include "shape_path.h"

#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram {

namespace {

bool is(const Shape &s, const char *id)
{
    return s.type == QLatin1String(id);
}

QPainterPath polygon(std::initializer_list<QPointF> points)
{
    QPainterPath path;
    path.addPolygon(QPolygonF(QVector<QPointF>(points)));
    path.closeSubpath();
    return path;
}

QPainterPath roundedBox(double w, double h, double radius)
{
    QPainterPath path;
    const double r = std::clamp(radius, 0.0, std::min(w, h) / 2);
    if (r <= 0) {
        path.addRect(0, 0, w, h);
    } else {
        path.addRoundedRect(QRectF(0, 0, w, h), r, r);
    }
    return path;
}

// A block arrow pointing right in a w x h box; the others are mirrors.
QPainterPath arrowRight(double w, double h)
{
    const double head = std::min(w * 0.45, h * 0.9);
    const double t = h * 0.26;
    return polygon({{0, t}, {w - head, t}, {w - head, 0}, {w, h / 2}, {w - head, h}, {w - head, h - t}, {0, h - t}});
}

QPainterPath cylinder(double w, double h)
{
    const double e = std::min(h * 0.28, w * 0.35); // cap height
    QPainterPath path;
    path.moveTo(0, e / 2);
    path.arcTo(QRectF(0, 0, w, e), 180, -180);
    path.lineTo(w, h - e / 2);
    path.arcTo(QRectF(0, h - e, w, e), 0, -180);
    path.closeSubpath();
    return path;
}

QPainterPath cloud(double w, double h)
{
    QPainterPath path;
    path.addEllipse(QRectF(w * 0.02, h * 0.38, w * 0.36, h * 0.54));
    path.addEllipse(QRectF(w * 0.18, h * 0.12, w * 0.40, h * 0.62));
    path.addEllipse(QRectF(w * 0.42, h * 0.02, w * 0.38, h * 0.62));
    path.addEllipse(QRectF(w * 0.62, h * 0.30, w * 0.36, h * 0.58));
    path.addRect(QRectF(w * 0.2, h * 0.55, w * 0.62, h * 0.4));
    path.setFillRule(Qt::WindingFill); // a union, not holes
    return path.simplified();
}

QPainterPath documentShape(double w, double h)
{
    const double wave = h * 0.12;
    QPainterPath path;
    path.moveTo(0, 0);
    path.lineTo(w, 0);
    path.lineTo(w, h - wave);
    path.cubicTo(w * 0.75, h - wave * 2.6, w * 0.5, h + wave * 0.6, w * 0.25, h - wave * 0.4);
    path.cubicTo(w * 0.12, h - wave * 1.2, w * 0.05, h - wave * 1.1, 0, h - wave * 0.8);
    path.closeSubpath();
    return path;
}

QPainterPath callout(double w, double h, bool oval)
{
    const double body = h * 0.78;
    QPainterPath path;
    if (oval) {
        path.addEllipse(QRectF(0, 0, w, body));
    } else {
        const double r = std::min(w, body) * 0.12;
        path.addRoundedRect(QRectF(0, 0, w, body), r, r);
    }
    path.addPolygon(QPolygonF(QVector<QPointF>{{w * 0.2, body * 0.8}, {w * 0.14, h}, {w * 0.38, body * 0.9}}));
    path.closeSubpath();
    path.setFillRule(Qt::WindingFill); // a union, not holes
    return path.simplified();
}

} // namespace

QPainterPath outline(const Shape &s)
{
    const double w = std::max(1.0, s.rect.width());
    const double h = std::max(1.0, s.rect.height());
    if (!s.paths.isEmpty()) {
        return customOutline(s); // M65: geometry read from Visio
    }
    if (is(s, "ellipse") || is(s, "circle") || is(s, "umlUseCase") || is(s, "onPage")) {
        QPainterPath path;
        path.addEllipse(QRectF(0, 0, w, h));
        return path;
    }
    if (is(s, "diamond") || is(s, "decision")) {
        return polygon({{w / 2, 0}, {w, h / 2}, {w / 2, h}, {0, h / 2}});
    }
    if (is(s, "triangle")) {
        return polygon({{w / 2, 0}, {w, h}, {0, h}});
    }
    if (is(s, "hexagon") || is(s, "preparation")) {
        const double i = std::min(w * 0.22, h * 0.5);
        return polygon({{i, 0}, {w - i, 0}, {w, h / 2}, {w - i, h}, {i, h}, {0, h / 2}});
    }
    if (is(s, "data")) {
        const double i = std::min(w * 0.18, h * 0.6);
        return polygon({{i, 0}, {w, 0}, {w - i, h}, {0, h}});
    }
    if (is(s, "terminator")) {
        return roundedBox(w, h, h / 2);
    }
    if (is(s, "roundedRectangle") || is(s, "umlClass")) {
        return roundedBox(w, h, s.style.cornerRadius > 0 ? s.style.cornerRadius : std::min(w, h) * 0.14);
    }
    if (is(s, "document")) {
        return documentShape(w, h);
    }
    if (is(s, "manualInput")) {
        return polygon({{0, h * 0.28}, {w, 0}, {w, h}, {0, h}});
    }
    if (is(s, "offPage")) {
        return polygon({{0, 0}, {w, 0}, {w, h * 0.62}, {w / 2, h}, {0, h * 0.62}});
    }
    if (is(s, "database")) {
        return cylinder(w, h);
    }
    if (is(s, "cloud")) {
        return cloud(w, h);
    }
    if (is(s, "note")) {
        const double fold = std::min(w, h) * 0.2;
        return polygon({{0, 0}, {w - fold, 0}, {w, fold}, {w, h}, {0, h}});
    }
    if (is(s, "umlPackage")) {
        QPainterPath path;
        path.addRect(QRectF(0, h * 0.18, w, h * 0.82));
        path.addRect(QRectF(0, 0, w * 0.42, h * 0.18 + 1));
        path.setFillRule(Qt::WindingFill); // a union, not holes
        return path.simplified();
    }
    if (is(s, "arrowRight")) {
        return arrowRight(w, h);
    }
    if (is(s, "arrowLeft")) {
        return QTransform(-1, 0, 0, 1, w, 0).map(arrowRight(w, h));
    }
    if (is(s, "arrowUp") || is(s, "arrowDown")) {
        // A right arrow in an h x w box, turned.
        const QPainterPath right = arrowRight(h, w);
        return is(s, "arrowDown") ? QTransform(0, 1, 1, 0, 0, 0).map(right)
                                  : QTransform(0, -1, 1, 0, 0, h).map(right);
    }
    if (is(s, "arrowDouble")) {
        const double head = std::min(w * 0.3, h * 0.9);
        const double t = h * 0.26;
        return polygon({{0, h / 2}, {head, 0}, {head, t}, {w - head, t}, {w - head, 0}, {w, h / 2},
                        {w - head, h}, {w - head, h - t}, {head, h - t}, {head, h}});
    }
    if (is(s, "chevron")) {
        const double d = std::min(w * 0.3, h * 0.5);
        return polygon({{0, 0}, {w - d, 0}, {w, h / 2}, {w - d, h}, {0, h}, {d, h / 2}});
    }
    if (is(s, "calloutRect") || is(s, "calloutOval")) {
        return callout(w, h, is(s, "calloutOval"));
    }
    const Stencil *stencil = Diagram::stencil(s.type);
    double radius = s.style.cornerRadius;
    if (stencil != nullptr && stencil->text == TextPlacement::Card && radius <= 0) {
        radius = 8;
    } else if (stencil != nullptr && stencil->text == TextPlacement::Below && radius <= 0
               && !is(s, "picture") && !is(s, "umlActor")) {
        radius = std::min(w, h) * 0.22; // the icon tile
    }
    return roundedBox(w, h, radius);
}

bool paintsOutline(const Shape &s)
{
    return !is(s, "text") && !is(s, "umlActor") && !is(s, "picture");
}

QRectF headerRect(const Shape &s)
{
    const double w = s.rect.width();
    const double h = s.rect.height();
    if (is(s, "laneHorizontal")) {
        return {0, 0, std::min(36.0, w * 0.3), h};
    }
    if (is(s, "laneVertical") || is(s, "phase")) {
        return {0, 0, w, std::min(34.0, h * 0.3)};
    }
    if (is(s, "container") || is(s, "orgTeam")) {
        return {0, 0, w, std::min(30.0, h * 0.3)};
    }
    return {};
}

QRectF textRect(const Shape &s)
{
    const double w = s.rect.width();
    const double h = s.rect.height();
    if (!s.textBox.isEmpty()) {
        return {s.textBox.x() * w, s.textBox.y() * h, s.textBox.width() * w, s.textBox.height() * h};
    }
    const Stencil *stencil = Diagram::stencil(s.type);
    const TextPlacement placement = stencil != nullptr ? stencil->text : TextPlacement::Inside;
    constexpr double pad = 6;
    switch (placement) {
    case TextPlacement::Below:
        return {-w * 0.75, h + 4, w * 2.5, std::max(28.0, s.textStyle.size * 3.2)};
    case TextPlacement::Header: {
        const QRectF header = headerRect(s);
        return header.adjusted(10, 2, -8, -2);
    }
    case TextPlacement::Card: {
        const double photo = std::min(h - 16, 44.0);
        return {photo + 22, pad, std::max(10.0, w - photo - 22 - pad * 1.5), h - pad * 2};
    }
    case TextPlacement::Inside:
        break;
    }
    if (is(s, "diamond") || is(s, "decision")) {
        return {w * 0.2, h * 0.18, w * 0.6, h * 0.64};
    }
    if (is(s, "triangle")) {
        return {w * 0.2, h * 0.45, w * 0.6, h * 0.5};
    }
    if (is(s, "data") || is(s, "hexagon") || is(s, "preparation")) {
        return {w * 0.18, pad, w * 0.64, h - pad * 2};
    }
    if (is(s, "document")) {
        return {pad, pad, w - pad * 2, h * 0.8 - pad};
    }
    if (is(s, "database")) {
        const double e = std::min(h * 0.28, w * 0.35);
        return {pad, e, w - pad * 2, h - e * 1.5};
    }
    if (is(s, "offPage")) {
        return {pad, pad, w - pad * 2, h * 0.55};
    }
    if (is(s, "calloutRect") || is(s, "calloutOval")) {
        const double inset = is(s, "calloutOval") ? w * 0.12 : pad;
        return {inset, pad, w - inset * 2, h * 0.78 - pad * 2};
    }
    if (is(s, "cloud") || is(s, "ellipse") || is(s, "circle") || is(s, "umlUseCase")) {
        return {w * 0.14, h * 0.14, w * 0.72, h * 0.72};
    }
    if (is(s, "umlPackage")) {
        return {pad, h * 0.18 + pad, w - pad * 2, h * 0.82 - pad * 2};
    }
    if (is(s, "arrowUp") || is(s, "arrowDown")) {
        const double head = std::min(h * 0.45, w * 0.9);
        return {w * 0.2, is(s, "arrowUp") ? head : pad, w * 0.6, h - head - pad};
    }
    if (is(s, "arrowRight") || is(s, "arrowLeft") || is(s, "arrowDouble") || is(s, "chevron")) {
        return {w * 0.14, h * 0.26, w * 0.66, h * 0.48};
    }
    if (is(s, "note")) {
        return {pad * 1.5, pad * 1.5, w - pad * 3, h - pad * 3};
    }
    return {pad, pad, std::max(1.0, w - pad * 2), std::max(1.0, h - pad * 2)};
}

} // namespace QindaOffice::Diagram
