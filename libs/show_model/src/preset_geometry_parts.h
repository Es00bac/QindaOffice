// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Building blocks of the preset outlines (internal to show_model):
// the shape's box with the formula names of presetShapeDefinitions.xml
// (l, t, r, b, w, h, hc, vc, ss), adjust values, and the path helpers the
// preset builders in preset_geometry.cpp and preset_geometry_lines.cpp
// share.

#include "preset_geometry.h"

#include <QHash>
#include <QPolygonF>
#include <QtMath>

#include <functional>

namespace QindaOffice::Show::Geometry::Presets {

struct Box {
    qreal l, t, r, b, w, h, hc, vc, ss;
    explicit Box(const QRectF &rect)
        : l(rect.left())
        , t(rect.top())
        , r(rect.right())
        , b(rect.bottom())
        , w(rect.width())
        , h(rect.height())
        , hc(rect.center().x())
        , vc(rect.center().y())
        , ss(qMin(rect.width(), rect.height()))
    {
    }
};

class Adj {
public:
    explicit Adj(const Adjustments &list)
        : m_list(list)
    {
    }
    // The adjust value `name` as a fraction (DrawingML stores 1/100000).
    [[nodiscard]] qreal operator()(const char *name, qreal fallback) const
    {
        for (const auto &[key, value] : m_list) {
            if (key == QLatin1String(name)) {
                return value / 100000.0;
            }
        }
        return fallback / 100000.0;
    }
    // An angle adjust value in degrees (stored in 1/60000 degree).
    [[nodiscard]] qreal angle(const char *name, qreal fallback) const
    {
        for (const auto &[key, value] : m_list) {
            if (key == QLatin1String(name)) {
                return value / 60000.0;
            }
        }
        return fallback / 60000.0;
    }

private:
    const Adjustments &m_list;
};

using Builder = std::function<Outline(const Box &, const Adj &)>;
[[nodiscard]] Outline actionButtonOutline(const QString &name, const QRectF &rect);

inline QPainterPath polygon(std::initializer_list<QPointF> points)
{
    QPainterPath path;
    path.addPolygon(QPolygonF(QList<QPointF>(points)));
    path.closeSubpath();
    return path;
}

inline Outline filled(const QPainterPath &path)
{
    return {path, path};
}

// A regular polygon / star fitted into the box.
inline Outline regular(const Box &x, int points, qreal innerRatio)
{
    QPolygonF poly;
    const int vertices = innerRatio > 0 ? points * 2 : points;
    for (int i = 0; i < vertices; ++i) {
        const qreal radius = (innerRatio > 0 && i % 2 == 1) ? innerRatio : 1.0;
        const qreal angle = -M_PI / 2 + 2 * M_PI * i / vertices;
        poly.append(QPointF(radius * std::cos(angle), radius * std::sin(angle)));
    }
    const QRectF bounds = poly.boundingRect();
    for (QPointF &p : poly) {
        p = QPointF(x.l + (p.x() - bounds.left()) / bounds.width() * x.w,
                    x.t + (p.y() - bounds.top()) / bounds.height() * x.h);
    }
    QPainterPath path;
    path.addPolygon(poly);
    path.closeSubpath();
    return filled(path);
}

inline QPainterPath roundedRect(const Box &x, qreal tl, qreal tr, qreal br, qreal bl)
{
    QPainterPath path;
    path.moveTo(x.l + tl, x.t);
    path.lineTo(x.r - tr, x.t);
    if (tr > 0) {
        path.arcTo(QRectF(x.r - 2 * tr, x.t, 2 * tr, 2 * tr), 90, -90);
    }
    path.lineTo(x.r, x.b - br);
    if (br > 0) {
        path.arcTo(QRectF(x.r - 2 * br, x.b - 2 * br, 2 * br, 2 * br), 0, -90);
    }
    path.lineTo(x.l + bl, x.b);
    if (bl > 0) {
        path.arcTo(QRectF(x.l, x.b - 2 * bl, 2 * bl, 2 * bl), -90, -90);
    }
    path.lineTo(x.l, x.t + tl);
    if (tl > 0) {
        path.arcTo(QRectF(x.l, x.t, 2 * tl, 2 * tl), 180, -90);
    }
    path.closeSubpath();
    return path;
}

inline QPainterPath snippedRect(const Box &x, qreal tl, qreal tr, qreal br, qreal bl)
{
    return polygon({{x.l + tl, x.t}, {x.r - tr, x.t}, {x.r, x.t + tr}, {x.r, x.b - br}, {x.r - br, x.b},
                    {x.l + bl, x.b}, {x.l, x.b - bl}, {x.l, x.t + tl}});
}

// A horizontal block arrow pointing right (mirrored for left).
inline QPainterPath horizontalArrow(const Box &x, const Adj &a, bool left, bool right)
{
    const qreal half = x.h * a("adj1", 50000) / 2.0;
    const qreal head = x.ss * a("adj2", 50000);
    const qreal y1 = x.vc - half;
    const qreal y2 = x.vc + half;
    const qreal xl = left ? x.l + head : x.l;
    const qreal xr = right ? x.r - head : x.r;
    QPolygonF p;
    if (left) {
        p << QPointF(x.l, x.vc) << QPointF(xl, x.t) << QPointF(xl, y1);
    } else {
        p << QPointF(x.l, y1);
    }
    if (right) {
        p << QPointF(xr, y1) << QPointF(xr, x.t) << QPointF(x.r, x.vc) << QPointF(xr, x.b) << QPointF(xr, y2);
    } else {
        p << QPointF(x.r, y1) << QPointF(x.r, y2);
    }
    if (left) {
        p << QPointF(xl, y2) << QPointF(xl, x.b);
    } else {
        p << QPointF(x.l, y2);
    }
    QPainterPath path;
    path.addPolygon(p);
    path.closeSubpath();
    return path;
}

inline QPainterPath verticalArrow(const Box &x, const Adj &a, bool up, bool down)
{
    // The same arrow turned a quarter: swap the axes.
    const QTransform swap(0, 1, 1, 0, 0, 0);
    const Box turned(QRectF(x.t, x.l, x.h, x.w));
    return swap.map(horizontalArrow(turned, a, up, down));
}

inline QPainterPath ellipsePath(const QRectF &rect)
{
    QPainterPath path;
    path.addEllipse(rect);
    return path;
}

inline QPainterPath arcPath(const QRectF &rect, qreal startDeg, qreal sweepDeg, bool wedge)
{
    // DrawingML angles turn clockwise from 3 o'clock; Qt's counter-clockwise.
    QPainterPath path;
    if (wedge) {
        path.moveTo(rect.center());
    } else {
        path.arcMoveTo(rect, -startDeg);
    }
    path.arcTo(rect, -startDeg, -sweepDeg);
    if (wedge) {
        path.closeSubpath();
    }
    return path;
}

// Registers the line-like presets (lines, connectors, arcs, brackets) and
// the callouts; preset_geometry_lines.cpp.
void addLinePresets(QHash<QString, Builder> &table);

} // namespace QindaOffice::Show::Geometry::Presets
