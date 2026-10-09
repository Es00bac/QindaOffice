// SPDX-License-Identifier: GPL-3.0-or-later
// Line-like presets (line, connectors, arc, brackets) and the callouts,
// for the renderer (preset_geometry_parts.h).

#include "preset_geometry_parts.h"

namespace QindaOffice::Show::Geometry::Presets {

void addLinePresets(QHash<QString, Builder> &t)
{
    const auto callout = [](const Box &x, const Adj &a, qreal corner, bool oval) {
        const QPointF tip(x.hc + x.w * a("adj1", -20833), x.vc + x.h * a("adj2", 62500));
        QPainterPath body = oval ? ellipsePath(QRectF(x.l, x.t, x.w, x.h)) : roundedRect(x, corner, corner, corner, corner);
        if (!body.contains(tip)) {
            // A wedge from the centre's side towards the tip.
            const QPointF centre(x.hc, x.vc);
            const QLineF axis(centre, tip);
            const QLineF normal = axis.normalVector().unitVector();
            const qreal half = qMin(x.w, x.h) / 8.0;
            const QPointF offset(normal.dx() * half, normal.dy() * half);
            QPainterPath wedge = polygon({centre + offset, tip, centre - offset});
            body = body.united(wedge);
        }
        return filled(body);
    };
    t.insert(QStringLiteral("wedgeRectCallout"), [callout](const Box &x, const Adj &a) { return callout(x, a, 0, false); });
    t.insert(QStringLiteral("wedgeRoundRectCallout"), [callout](const Box &x, const Adj &a) {
        return callout(x, a, x.ss * a("adj3", 16667), false);
    });
    t.insert(QStringLiteral("wedgeEllipseCallout"), [callout](const Box &x, const Adj &a) { return callout(x, a, 0, true); });
    t.insert(QStringLiteral("cloudCallout"), t.value(QStringLiteral("wedgeEllipseCallout")));
    t.insert(QStringLiteral("cloud"), t.value(QStringLiteral("ellipse")));
    t.insert(QStringLiteral("arc"), [](const Box &x, const Adj &a) {
        const qreal start = a.angle("adj1", 16200000);
        const qreal end = a.angle("adj2", 0);
        const qreal sweep = std::fmod(end - start + 360.0, 360.0);
        return Outline{QPainterPath(), arcPath(QRectF(x.l, x.t, x.w, x.h), start, sweep, false)};
    });
    // Lines and connectors: stroked from the top-left to the bottom-right
    // of their box (flips move the ends, see shape painting).
    t.insert(QStringLiteral("line"), [](const Box &x, const Adj &) {
        QPainterPath path(QPointF(x.l, x.t));
        path.lineTo(x.r, x.b);
        return Outline{QPainterPath(), path};
    });
    t.insert(QStringLiteral("straightConnector1"), t.value(QStringLiteral("line")));
    t.insert(QStringLiteral("bentConnector2"), [](const Box &x, const Adj &) {
        QPainterPath path(QPointF(x.l, x.t));
        path.lineTo(x.r, x.t);
        path.lineTo(x.r, x.b);
        return Outline{QPainterPath(), path};
    });
    t.insert(QStringLiteral("bentConnector3"), [](const Box &x, const Adj &a) {
        const qreal x1 = x.l + x.w * a("adj1", 50000);
        QPainterPath path(QPointF(x.l, x.t));
        path.lineTo(x1, x.t);
        path.lineTo(x1, x.b);
        path.lineTo(x.r, x.b);
        return Outline{QPainterPath(), path};
    });
    t.insert(QStringLiteral("bentConnector4"), [](const Box &x, const Adj &a) {
        const qreal x1 = x.l + x.w * a("adj1", 50000);
        const qreal y2 = x.t + x.h * a("adj2", 50000);
        QPainterPath path(QPointF(x.l, x.t));
        path.lineTo(x1, x.t);
        path.lineTo(x1, y2);
        path.lineTo(x.r, y2);
        path.lineTo(x.r, x.b);
        return Outline{QPainterPath(), path};
    });
    t.insert(QStringLiteral("curvedConnector2"), [](const Box &x, const Adj &) {
        QPainterPath path(QPointF(x.l, x.t));
        path.cubicTo(x.hc, x.t, x.r, x.vc, x.r, x.b);
        return Outline{QPainterPath(), path};
    });
    t.insert(QStringLiteral("curvedConnector3"), [](const Box &x, const Adj &a) {
        const qreal x1 = x.l + x.w * a("adj1", 50000);
        QPainterPath path(QPointF(x.l, x.t));
        path.cubicTo((x.l + x1) / 2, x.t, x1, (x.t + x.vc) / 2, x1, x.vc);
        path.cubicTo(x1, (x.vc + x.b) / 2, (x1 + x.r) / 2, x.b, x.r, x.b);
        return Outline{QPainterPath(), path};
    });
    const auto bracket = [](const Box &x, qreal r, bool left, bool right) {
        QPainterPath path;
        if (left) {
            path.moveTo(x.l + r * 2, x.b);
            path.arcTo(QRectF(x.l, x.b - 2 * r, 2 * r, 2 * r), 270, -90);
            path.lineTo(x.l, x.t + r);
            path.arcTo(QRectF(x.l, x.t, 2 * r, 2 * r), 180, -90);
        }
        if (right) {
            path.moveTo(x.r - r * 2, x.t);
            path.arcTo(QRectF(x.r - 2 * r, x.t, 2 * r, 2 * r), 90, -90);
            path.lineTo(x.r, x.b - r);
            path.arcTo(QRectF(x.r - 2 * r, x.b - 2 * r, 2 * r, 2 * r), 0, -90);
        }
        return path;
    };
    t.insert(QStringLiteral("bracketPair"), [bracket](const Box &x, const Adj &a) {
        const qreal r = x.ss * a("adj", 16667);
        return Outline{roundedRect(x, r, r, r, r), bracket(x, r, true, true)};
    });
    t.insert(QStringLiteral("leftBracket"), [bracket](const Box &x, const Adj &a) {
        return Outline{QPainterPath(), bracket(x, qMin(x.w, x.ss * a("adj", 8333)), true, false)};
    });
    t.insert(QStringLiteral("rightBracket"), [bracket](const Box &x, const Adj &a) {
        return Outline{QPainterPath(), bracket(x, qMin(x.w, x.ss * a("adj", 8333)), false, true)};
    });
}

} // namespace QindaOffice::Show::Geometry::Presets
