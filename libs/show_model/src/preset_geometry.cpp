// SPDX-License-Identifier: GPL-3.0-or-later
// Preset outlines, following the formulas of ECMA-376 Part 1 Annex D
// (presetShapeDefinitions.xml) for the shapes decks use most; regular
// polygons and stars are fitted to the box.

#include "preset_geometry.h"
#include "preset_geometry_parts.h"

namespace QindaOffice::Show::Geometry {

namespace {

using namespace Presets;

const QHash<QString, Builder> &builders()
{
    static const QHash<QString, Builder> table = [] {
        QHash<QString, Builder> t;
        const auto rect = [](const Box &x, const Adj &) { return filled(polygon({{x.l, x.t}, {x.r, x.t}, {x.r, x.b}, {x.l, x.b}})); };
        for (const char *name : {"rect", "flowChartProcess", "actionButtonBlank"}) {
            t.insert(QLatin1String(name), rect);
        }
        t.insert(QStringLiteral("ellipse"), [](const Box &x, const Adj &) { return filled(ellipsePath(QRectF(x.l, x.t, x.w, x.h))); });
        t.insert(QStringLiteral("flowChartConnector"), t.value(QStringLiteral("ellipse")));
        t.insert(QStringLiteral("roundRect"), [](const Box &x, const Adj &a) {
            const qreal r = x.ss * a("adj", 16667);
            return filled(roundedRect(x, r, r, r, r));
        });
        t.insert(QStringLiteral("flowChartAlternateProcess"), [](const Box &x, const Adj &) {
            const qreal r = x.ss / 6.0;
            return filled(roundedRect(x, r, r, r, r));
        });
        t.insert(QStringLiteral("round1Rect"), [](const Box &x, const Adj &a) {
            return filled(roundedRect(x, 0, x.ss * a("adj", 16667), 0, 0));
        });
        t.insert(QStringLiteral("round2SameRect"), [](const Box &x, const Adj &a) {
            const qreal top = x.ss * a("adj1", 16667);
            const qreal bottom = x.ss * a("adj2", 0);
            return filled(roundedRect(x, top, top, bottom, bottom));
        });
        t.insert(QStringLiteral("round2DiagRect"), [](const Box &x, const Adj &a) {
            const qreal one = x.ss * a("adj1", 16667);
            const qreal two = x.ss * a("adj2", 0);
            return filled(roundedRect(x, one, two, one, two));
        });
        t.insert(QStringLiteral("snip1Rect"), [](const Box &x, const Adj &a) {
            return filled(snippedRect(x, 0, x.ss * a("adj", 16667), 0, 0));
        });
        t.insert(QStringLiteral("snip2SameRect"), [](const Box &x, const Adj &a) {
            const qreal top = x.ss * a("adj1", 16667);
            const qreal bottom = x.ss * a("adj2", 0);
            return filled(snippedRect(x, top, top, bottom, bottom));
        });
        t.insert(QStringLiteral("snip2DiagRect"), [](const Box &x, const Adj &a) {
            const qreal one = x.ss * a("adj1", 0);
            const qreal two = x.ss * a("adj2", 16667);
            return filled(snippedRect(x, one, two, one, two));
        });
        t.insert(QStringLiteral("triangle"), [](const Box &x, const Adj &a) {
            return filled(polygon({{x.l + x.w * a("adj", 50000), x.t}, {x.r, x.b}, {x.l, x.b}}));
        });
        t.insert(QStringLiteral("flowChartExtract"), [](const Box &x, const Adj &) {
            return filled(polygon({{x.hc, x.t}, {x.r, x.b}, {x.l, x.b}}));
        });
        t.insert(QStringLiteral("flowChartMerge"), [](const Box &x, const Adj &) {
            return filled(polygon({{x.l, x.t}, {x.r, x.t}, {x.hc, x.b}}));
        });
        t.insert(QStringLiteral("rtTriangle"), [](const Box &x, const Adj &) {
            return filled(polygon({{x.l, x.b}, {x.l, x.t}, {x.r, x.b}}));
        });
        const auto diamond = [](const Box &x, const Adj &) {
            return filled(polygon({{x.hc, x.t}, {x.r, x.vc}, {x.hc, x.b}, {x.l, x.vc}}));
        };
        t.insert(QStringLiteral("diamond"), diamond);
        t.insert(QStringLiteral("flowChartDecision"), diamond);
        t.insert(QStringLiteral("parallelogram"), [](const Box &x, const Adj &a) {
            const qreal dx = x.ss * a("adj", 25000);
            return filled(polygon({{x.l + dx, x.t}, {x.r, x.t}, {x.r - dx, x.b}, {x.l, x.b}}));
        });
        t.insert(QStringLiteral("flowChartInputOutput"), [](const Box &x, const Adj &) {
            const qreal dx = x.w / 5.0;
            return filled(polygon({{x.l + dx, x.t}, {x.r, x.t}, {x.r - dx, x.b}, {x.l, x.b}}));
        });
        t.insert(QStringLiteral("trapezoid"), [](const Box &x, const Adj &a) {
            const qreal dx = x.ss * a("adj", 25000);
            return filled(polygon({{x.l, x.b}, {x.l + dx, x.t}, {x.r - dx, x.t}, {x.r, x.b}}));
        });
        t.insert(QStringLiteral("flowChartManualOperation"), [](const Box &x, const Adj &) {
            return filled(polygon({{x.l, x.t}, {x.r, x.t}, {x.r - x.w / 5, x.b}, {x.l + x.w / 5, x.b}}));
        });
        t.insert(QStringLiteral("flowChartManualInput"), [](const Box &x, const Adj &) {
            return filled(polygon({{x.l, x.t + x.h / 5}, {x.r, x.t}, {x.r, x.b}, {x.l, x.b}}));
        });
        t.insert(QStringLiteral("hexagon"), [](const Box &x, const Adj &a) {
            const qreal dx = x.ss * a("adj", 25000);
            return filled(polygon({{x.l, x.vc}, {x.l + dx, x.t}, {x.r - dx, x.t}, {x.r, x.vc}, {x.r - dx, x.b}, {x.l + dx, x.b}}));
        });
        t.insert(QStringLiteral("flowChartPreparation"), [](const Box &x, const Adj &) {
            const qreal dx = x.w / 5;
            return filled(polygon({{x.l, x.vc}, {x.l + dx, x.t}, {x.r - dx, x.t}, {x.r, x.vc}, {x.r - dx, x.b}, {x.l + dx, x.b}}));
        });
        t.insert(QStringLiteral("octagon"), [](const Box &x, const Adj &a) {
            const qreal d = x.ss * a("adj", 29289);
            return filled(snippedRect(x, d, d, d, d));
        });
        t.insert(QStringLiteral("pentagon"), [](const Box &x, const Adj &) { return regular(x, 5, 0); });
        t.insert(QStringLiteral("heptagon"), [](const Box &x, const Adj &) { return regular(x, 7, 0); });
        t.insert(QStringLiteral("decagon"), [](const Box &x, const Adj &) { return regular(x, 10, 0); });
        t.insert(QStringLiteral("dodecagon"), [](const Box &x, const Adj &) { return regular(x, 12, 0); });
        const std::pair<const char *, std::pair<int, qreal>> stars[] = {
            {"star4", {4, 12500}}, {"star5", {5, 19098}}, {"star6", {6, 28868}}, {"star7", {7, 34601}},
            {"star8", {8, 37500}}, {"star10", {10, 42533}}, {"star12", {12, 37500}}, {"star16", {16, 37500}},
            {"star24", {24, 37500}}, {"star32", {32, 37500}}};
        for (const auto &[name, spec] : stars) {
            const auto [points, inner] = spec;
            t.insert(QLatin1String(name), [points, inner](const Box &x, const Adj &a) {
                return regular(x, points, a("adj", inner) * 2.0);
            });
        }
        t.insert(QStringLiteral("rightArrow"), [](const Box &x, const Adj &a) { return filled(horizontalArrow(x, a, false, true)); });
        t.insert(QStringLiteral("leftArrow"), [](const Box &x, const Adj &a) { return filled(horizontalArrow(x, a, true, false)); });
        t.insert(QStringLiteral("leftRightArrow"), [](const Box &x, const Adj &a) { return filled(horizontalArrow(x, a, true, true)); });
        t.insert(QStringLiteral("downArrow"), [](const Box &x, const Adj &a) { return filled(verticalArrow(x, a, false, true)); });
        t.insert(QStringLiteral("upArrow"), [](const Box &x, const Adj &a) { return filled(verticalArrow(x, a, true, false)); });
        t.insert(QStringLiteral("upDownArrow"), [](const Box &x, const Adj &a) { return filled(verticalArrow(x, a, true, true)); });
        t.insert(QStringLiteral("notchedRightArrow"), [](const Box &x, const Adj &a) {
            const qreal half = x.h * a("adj1", 50000) / 2.0;
            const qreal head = x.ss * a("adj2", 50000);
            const qreal notch = half * head / (x.h / 2.0);
            return filled(polygon({{x.l, x.vc - half}, {x.r - head, x.vc - half}, {x.r - head, x.t}, {x.r, x.vc},
                                   {x.r - head, x.b}, {x.r - head, x.vc + half}, {x.l, x.vc + half}, {x.l + notch, x.vc}}));
        });
        t.insert(QStringLiteral("chevron"), [](const Box &x, const Adj &a) {
            const qreal d = x.ss * a("adj", 50000);
            return filled(polygon({{x.l, x.t}, {x.r - d, x.t}, {x.r, x.vc}, {x.r - d, x.b}, {x.l, x.b}, {x.l + d, x.vc}}));
        });
        t.insert(QStringLiteral("homePlate"), [](const Box &x, const Adj &a) {
            const qreal d = x.ss * a("adj", 50000);
            return filled(polygon({{x.l, x.t}, {x.r - d, x.t}, {x.r, x.vc}, {x.r - d, x.b}, {x.l, x.b}}));
        });
        t.insert(QStringLiteral("flowChartOffpageConnector"), [](const Box &x, const Adj &) {
            return filled(polygon({{x.l, x.t}, {x.r, x.t}, {x.r, x.t + x.h * 0.8}, {x.hc, x.b}, {x.l, x.t + x.h * 0.8}}));
        });
        t.insert(QStringLiteral("plus"), [](const Box &x, const Adj &a) {
            const qreal d = x.ss * a("adj", 25000);
            return filled(polygon({{x.l, x.t + d}, {x.l + d, x.t + d}, {x.l + d, x.t}, {x.r - d, x.t}, {x.r - d, x.t + d},
                                   {x.r, x.t + d}, {x.r, x.b - d}, {x.r - d, x.b - d}, {x.r - d, x.b}, {x.l + d, x.b},
                                   {x.l + d, x.b - d}, {x.l, x.b - d}}));
        });
        t.insert(QStringLiteral("mathPlus"), [](const Box &x, const Adj &a) {
            const qreal half = x.ss * a("adj1", 23520) / 2;
            const qreal arm = x.ss * 0.36;
            return filled(polygon({{x.hc - arm, x.vc - half}, {x.hc - half, x.vc - half}, {x.hc - half, x.vc - arm},
                                   {x.hc + half, x.vc - arm}, {x.hc + half, x.vc - half}, {x.hc + arm, x.vc - half},
                                   {x.hc + arm, x.vc + half}, {x.hc + half, x.vc + half}, {x.hc + half, x.vc + arm},
                                   {x.hc - half, x.vc + arm}, {x.hc - half, x.vc + half}, {x.hc - arm, x.vc + half}}));
        });
        t.insert(QStringLiteral("mathMinus"), [](const Box &x, const Adj &a) {
            const qreal half = x.h * a("adj1", 23520) / 2;
            return filled(polygon({{x.l + x.w * 0.12, x.vc - half}, {x.r - x.w * 0.12, x.vc - half},
                                   {x.r - x.w * 0.12, x.vc + half}, {x.l + x.w * 0.12, x.vc + half}}));
        });
        t.insert(QStringLiteral("can"), [](const Box &x, const Adj &a) {
            const qreal cap = x.ss * a("adj", 25000);
            QPainterPath body;
            body.moveTo(x.l, x.t + cap / 2);
            body.lineTo(x.l, x.b - cap / 2);
            body.arcTo(QRectF(x.l, x.b - cap, x.w, cap), 180, 180);
            body.lineTo(x.r, x.t + cap / 2);
            body.arcTo(QRectF(x.l, x.t, x.w, cap), 0, 180);
            body.closeSubpath();
            QPainterPath stroke = body;
            stroke.addEllipse(QRectF(x.l, x.t, x.w, cap));
            return Outline{body, stroke};
        });
        t.insert(QStringLiteral("flowChartMagneticDisk"), t.value(QStringLiteral("can")));
        t.insert(QStringLiteral("donut"), [](const Box &x, const Adj &a) {
            const qreal d = x.ss * a("adj", 25000);
            QPainterPath path = ellipsePath(QRectF(x.l, x.t, x.w, x.h));
            path.addEllipse(QRectF(x.l + d, x.t + d, x.w - 2 * d, x.h - 2 * d));
            path.setFillRule(Qt::OddEvenFill);
            return filled(path);
        });
        t.insert(QStringLiteral("frame"), [](const Box &x, const Adj &a) {
            const qreal d = x.ss * a("adj1", 12500);
            QPainterPath path = polygon({{x.l, x.t}, {x.r, x.t}, {x.r, x.b}, {x.l, x.b}});
            path.addPolygon(QPolygonF({QPointF(x.l + d, x.t + d), QPointF(x.r - d, x.t + d), QPointF(x.r - d, x.b - d),
                                       QPointF(x.l + d, x.b - d)}));
            path.closeSubpath();
            path.setFillRule(Qt::OddEvenFill);
            return filled(path);
        });
        t.insert(QStringLiteral("corner"), [](const Box &x, const Adj &a) {
            const qreal dy = x.ss * a("adj1", 50000);
            const qreal dx = x.ss * a("adj2", 50000);
            return filled(polygon({{x.l, x.t}, {x.l + dx, x.t}, {x.l + dx, x.b - dy}, {x.r, x.b - dy}, {x.r, x.b}, {x.l, x.b}}));
        });
        t.insert(QStringLiteral("foldedCorner"), [](const Box &x, const Adj &a) {
            const qreal d = x.ss * a("adj", 16667);
            QPainterPath body = polygon({{x.l, x.t}, {x.r, x.t}, {x.r, x.b - d}, {x.r - d, x.b}, {x.l, x.b}});
            QPainterPath stroke = body;
            stroke.moveTo(x.r, x.b - d);
            stroke.lineTo(x.r - d * 0.8, x.b - d * 0.8);
            stroke.lineTo(x.r - d, x.b);
            return Outline{body, stroke};
        });
        t.insert(QStringLiteral("flowChartTerminator"), [](const Box &x, const Adj &) {
            const qreal r = qMin(x.h / 2, x.w / 2);
            return filled(roundedRect(x, r, r, r, r));
        });
        t.insert(QStringLiteral("flowChartPredefinedProcess"), [](const Box &x, const Adj &) {
            QPainterPath body = polygon({{x.l, x.t}, {x.r, x.t}, {x.r, x.b}, {x.l, x.b}});
            QPainterPath stroke = body;
            stroke.moveTo(x.l + x.w / 8, x.t);
            stroke.lineTo(x.l + x.w / 8, x.b);
            stroke.moveTo(x.r - x.w / 8, x.t);
            stroke.lineTo(x.r - x.w / 8, x.b);
            return Outline{body, stroke};
        });
        t.insert(QStringLiteral("flowChartDocument"), [](const Box &x, const Adj &) {
            QPainterPath path;
            path.moveTo(x.l, x.t);
            path.lineTo(x.r, x.t);
            path.lineTo(x.r, x.t + x.h * 0.8);
            path.cubicTo(x.r - x.w * 0.25, x.t + x.h * 0.65, x.l + x.w * 0.5, x.t + x.h * 1.08, x.l, x.t + x.h * 0.9);
            path.closeSubpath();
            return filled(path);
        });
        t.insert(QStringLiteral("flowChartDelay"), [](const Box &x, const Adj &) {
            QPainterPath path;
            path.moveTo(x.l, x.t);
            path.lineTo(x.hc, x.t);
            path.arcTo(QRectF(x.l, x.t, x.w, x.h), 90, -180);
            path.lineTo(x.l, x.b);
            path.closeSubpath();
            return filled(path);
        });
        t.insert(QStringLiteral("heart"), [](const Box &x, const Adj &) {
            QPainterPath path;
            path.moveTo(x.hc, x.t + x.h / 4);
            path.cubicTo(x.hc + x.w * 0.25, x.t - x.h / 6, x.r + x.w * 0.2, x.t + x.h / 3, x.hc, x.b);
            path.cubicTo(x.l - x.w * 0.2, x.t + x.h / 3, x.hc - x.w * 0.25, x.t - x.h / 6, x.hc, x.t + x.h / 4);
            path.closeSubpath();
            return filled(path);
        });
        t.insert(QStringLiteral("lightningBolt"), [](const Box &x, const Adj &) {
            static const qreal pts[][2] = {{8472, 0},     {12860, 6080},  {11050, 6797}, {16577, 12007},
                                           {14767, 12877}, {21600, 21600}, {10012, 14915}, {12222, 13987},
                                           {5022, 9705},  {7602, 8382},   {0, 3890}};
            QPolygonF poly;
            for (const auto &p : pts) {
                poly << QPointF(x.l + x.w * p[0] / 21600, x.t + x.h * p[1] / 21600);
            }
            QPainterPath path;
            path.addPolygon(poly);
            path.closeSubpath();
            return filled(path);
        });
        t.insert(QStringLiteral("pie"), [](const Box &x, const Adj &a) {
            const qreal start = a.angle("adj1", 0);
            qreal end = a.angle("adj2", 16200000);
            const qreal sweep = std::fmod(end - start + 360.0, 360.0);
            return filled(arcPath(QRectF(x.l, x.t, x.w, x.h), start, sweep, true));
        });
        addLinePresets(t);
        return t;
    }();
    return table;
}

} // namespace

Outline presetOutline(const QString &name, const QRectF &rect, const Adjustments &adjust)
{
    if (rect.isEmpty() && !isOpenPreset(name)) {
        return {};
    }
    const auto found = builders().constFind(name);
    if (found == builders().constEnd()) {
        return {};
    }
    const Box box(rect.normalized());
    return found.value()(box, Adj(adjust));
}

QRectF textRect(const QString &name, const QRectF &rect, const Adjustments &adjust)
{
    const QRectF r = rect.normalized();
    if (name == u"ellipse" || name == u"flowChartConnector" || name == u"wedgeEllipseCallout") {
        // The inscribed rectangle: corners at 45° on the ellipse.
        const qreal dx = r.width() / 2.0 * (1.0 - M_SQRT1_2);
        const qreal dy = r.height() / 2.0 * (1.0 - M_SQRT1_2);
        return r.adjusted(dx, dy, -dx, -dy);
    }
    if (name == u"roundRect") {
        const qreal radius = qMin(r.width(), r.height()) * Adj(adjust)("adj", 16667) * 0.29289;
        return r.adjusted(radius, radius, -radius, -radius);
    }
    if (name == u"diamond" || name == u"flowChartDecision") {
        return r.adjusted(r.width() / 4, r.height() / 4, -r.width() / 4, -r.height() / 4);
    }
    if (name == u"triangle") {
        return QRectF(r.left() + r.width() / 4, r.top() + r.height() / 2, r.width() / 2, r.height() / 2);
    }
    return r;
}

bool isOpenPreset(const QString &name)
{
    return name == u"line" || name == u"arc" || name.startsWith(QLatin1String("straightConnector"))
        || name.startsWith(QLatin1String("bentConnector")) || name.startsWith(QLatin1String("curvedConnector"))
        || name == u"leftBracket" || name == u"rightBracket";
}

} // namespace QindaOffice::Show::Geometry
