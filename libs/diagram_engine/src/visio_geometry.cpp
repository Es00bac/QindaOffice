// SPDX-License-Identifier: GPL-3.0-or-later
// Geometry sections ⇄ paths (visio_geometry.h).
#include "visio_geometry.h"

#include <QLineF>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Visio {

namespace {

constexpr double kPi = 3.14159265358979323846;

// The numbers inside a POLYLINE(…) / NURBS(…) value, in order.
QVector<double> functionArguments(const Cell *cell)
{
    QVector<double> out;
    if (cell == nullptr) {
        return out;
    }
    QString text = cell->value.isEmpty() || !cell->value.contains(QLatin1Char('(')) ? cell->formula : cell->value;
    const qsizetype open = text.indexOf(QLatin1Char('('));
    const qsizetype close = text.lastIndexOf(QLatin1Char(')'));
    if (open < 0 || close <= open) {
        return out;
    }
    static const QRegularExpression number(QStringLiteral("[-+]?(\\d+\\.?\\d*|\\.\\d+)([eE][-+]?\\d+)?"));
    for (const QString &part : text.mid(open + 1, close - open - 1).split(QLatin1Char(','))) {
        const QRegularExpressionMatch m = number.match(part);
        out.append(m.hasMatch() ? m.captured(0).toDouble() : 0.0);
    }
    return out;
}

double basis(int i, int p, double u, const QVector<double> &k)
{
    if (p == 0) {
        const bool last = u >= k.last() && k.at(i + 1) >= k.last() && k.at(i) < k.at(i + 1);
        return (k.at(i) <= u && u < k.at(i + 1)) || last ? 1.0 : 0.0;
    }
    double value = 0;
    const double a = k.at(i + p) - k.at(i);
    if (a > 1e-12) {
        value += (u - k.at(i)) / a * basis(i, p - 1, u, k);
    }
    const double b = k.at(i + p + 1) - k.at(i + 1);
    if (b > 1e-12) {
        value += (k.at(i + p + 1) - u) / b * basis(i + 1, p - 1, u, k);
    }
    return value;
}

// A (rational) B-spline sampled as line segments from its first point.
void appendNurbs(QPainterPath &path, const QVector<QPointF> &ctrl, QVector<double> knots, QVector<double> weights,
                 int degree)
{
    const int n = int(ctrl.size());
    degree = std::clamp(degree, 1, std::min(9, n - 1));
    if (n < 2) {
        return;
    }
    for (int i = 1; i < knots.size(); ++i) {
        knots[i] = std::max(knots[i], knots[i - 1]); // non-decreasing, as libvisio does
    }
    while (knots.size() < n + degree + 1) {
        knots.append(knots.isEmpty() ? 1.0 : knots.last());
    }
    knots.resize(n + degree + 1);
    weights.resize(n);
    for (double &w : weights) {
        w = w > 0 ? w : 1.0;
    }
    const double u0 = knots.at(degree);
    const double u1 = knots.at(n);
    if (!(u1 > u0)) {
        path.lineTo(ctrl.last());
        return;
    }
    const int samples = std::clamp(n * 12, 24, 400);
    for (int s = 1; s <= samples; ++s) {
        const double u = u0 + (u1 - u0) * double(s) / samples;
        QPointF sum;
        double total = 0;
        for (int i = 0; i < n; ++i) {
            const double b = basis(i, degree, u, knots) * weights.at(i);
            sum += ctrl.at(i) * b;
            total += b;
        }
        if (total > 1e-12) {
            path.lineTo(sum / total);
        }
    }
    path.lineTo(ctrl.last());
}

void appendEllipse(QPainterPath &path, QPointF c, QPointF a, QPointF b)
{
    // c + u·cos t + v·sin t: the unit circle's four Bézier quarters mapped.
    const QPointF u = a - c;
    const QPointF v = b - c;
    const double k = 0.5522847498307936;
    const auto at = [&](double t) { return c + u * std::cos(t) + v * std::sin(t); };
    const auto tangent = [&](double t) { return -u * std::sin(t) + v * std::cos(t); };
    path.moveTo(at(0));
    for (int q = 0; q < 4; ++q) {
        const double t0 = q * kPi / 2;
        const double t1 = (q + 1) * kPi / 2;
        path.cubicTo(at(t0) + tangent(t0) * k, at(t1) - tangent(t1) * k, at(t1));
    }
    path.closeSubpath();
}

struct Spline {
    bool active = false;
    QVector<QPointF> ctrl;
    QVector<double> knots;
    double lastKnot = 0;
    int degree = 3;
    QPointF pending;
};

void flushSpline(QPainterPath &path, Spline &spline)
{
    if (!spline.active) {
        return;
    }
    spline.ctrl.append(spline.pending);
    spline.knots.append(spline.lastKnot);
    appendNurbs(path, spline.ctrl, spline.knots, QVector<double>(spline.ctrl.size(), 1.0), spline.degree);
    spline = Spline();
}

bool closedFigures(const QPainterPath &path)
{
    QPointF start;
    QPointF last;
    bool any = false;
    for (int i = 0; i < path.elementCount(); ++i) {
        const QPainterPath::Element e = path.elementAt(i);
        if (e.isMoveTo()) {
            if (any && QLineF(start, last).length() > 1e-6) {
                return false;
            }
            start = QPointF(e.x, e.y);
            any = true;
        }
        last = QPointF(e.x, e.y);
    }
    return any && QLineF(start, last).length() <= 1e-6;
}

Figure readSection(const Section &section, double w, double h)
{
    Figure figure;
    QPainterPath &path = figure.path;
    QPointF current;
    Spline spline;
    for (const Row *row : section.orderedRows()) {
        const QString &t = row->type;
        if (t != QLatin1String("SplineKnot")) {
            flushSpline(path, spline);
        }
        const QPointF xy(row->number(u"X"), row->number(u"Y"));
        const QPointF rel(xy.x() * w, xy.y() * h);
        if (t == QLatin1String("MoveTo") || t == QLatin1String("RelMoveTo")) {
            current = t == QLatin1String("MoveTo") ? xy : rel;
            path.moveTo(current);
            continue;
        }
        if (path.elementCount() == 0 && t != QLatin1String("Ellipse") && t != QLatin1String("InfiniteLine")) {
            path.moveTo(current); // a figure without a MoveTo starts at the origin
        }
        if (t == QLatin1String("LineTo") || t == QLatin1String("RelLineTo")) {
            current = t == QLatin1String("LineTo") ? xy : rel;
            path.lineTo(current);
        } else if (t == QLatin1String("ArcTo")) {
            const double bow = row->number(u"A");
            const QLineF chord(current, xy);
            if (std::abs(bow) < 1e-9 || chord.length() < 1e-9) {
                path.lineTo(xy);
            } else {
                // A positive bow turns counter-clockwise: the arc's middle
                // lies to the right of the chord's direction (libvisio's
                // sweep rule, which matches Visio).
                const QPointF d = (xy - current) / chord.length();
                const QPointF mid = (current + xy) / 2 + QPointF(d.y(), -d.x()) * bow;
                appendArcThrough(path, current, mid, xy);
            }
            current = xy;
        } else if (t == QLatin1String("EllipticalArcTo") || t == QLatin1String("RelEllipticalArcTo")) {
            const bool relative = t == QLatin1String("RelEllipticalArcTo");
            const QPointF end = relative ? rel : xy;
            const QPointF control = relative ? QPointF(row->number(u"A") * w, row->number(u"B") * h)
                                             : QPointF(row->number(u"A"), row->number(u"B"));
            appendArcThrough(path, current, control, end, row->number(u"C"), row->number(u"D", 1));
            current = end;
        } else if (t == QLatin1String("Ellipse")) {
            appendEllipse(path, xy, QPointF(row->number(u"A"), row->number(u"B")),
                          QPointF(row->number(u"C"), row->number(u"D")));
        } else if (t == QLatin1String("RelCubBezTo")) {
            path.cubicTo(QPointF(row->number(u"A") * w, row->number(u"B") * h),
                         QPointF(row->number(u"C") * w, row->number(u"D") * h), rel);
            current = rel;
        } else if (t == QLatin1String("RelQuadBezTo")) {
            path.quadTo(QPointF(row->number(u"A") * w, row->number(u"B") * h), rel);
            current = rel;
        } else if (t == QLatin1String("PolylineTo")) {
            const QVector<double> a = functionArguments(row->cell(u"A"));
            const bool relX = a.value(0) == 0;
            const bool relY = a.value(1) == 0;
            for (qsizetype i = 2; i + 1 < a.size(); i += 2) {
                path.lineTo(a.at(i) * (relX ? w : 1), a.at(i + 1) * (relY ? h : 1));
            }
            path.lineTo(xy);
            current = xy;
        } else if (t == QLatin1String("NURBSTo")) {
            // NURBS(lastKnot, degree, xType, yType, x1, y1, knot1, weight1, …)
            const QVector<double> e = functionArguments(row->cell(u"E"));
            QVector<QPointF> ctrl{current};
            QVector<double> knots{row->number(u"C")};
            QVector<double> weights{row->number(u"D", 1)};
            const bool relX = e.value(2) == 0;
            const bool relY = e.value(3) == 0;
            for (qsizetype i = 4; i + 3 < e.size(); i += 4) {
                ctrl.append(QPointF(e.at(i) * (relX ? w : 1), e.at(i + 1) * (relY ? h : 1)));
                knots.append(e.at(i + 2));
                weights.append(e.at(i + 3));
            }
            ctrl.append(xy);
            knots << row->number(u"A") << e.value(0, row->number(u"A"));
            weights.append(row->number(u"B", 1));
            appendNurbs(path, ctrl, knots, weights, e.size() >= 2 ? int(e.at(1)) : 3);
            current = xy;
        } else if (t == QLatin1String("SplineStart")) {
            spline.active = true;
            spline.ctrl = {current};
            spline.knots = {row->number(u"B"), row->number(u"A")};
            spline.lastKnot = row->number(u"C");
            spline.degree = int(row->number(u"D", 3));
            spline.pending = xy;
            current = xy;
        } else if (t == QLatin1String("SplineKnot") && spline.active) {
            spline.ctrl.append(spline.pending);
            spline.knots.append(row->number(u"A"));
            spline.pending = xy;
            current = xy;
        }
        // InfiniteLine (and anything unknown) draws nothing here.
    }
    flushSpline(path, spline);
    figure.fill = !section.flag(u"NoFill") && closedFigures(path);
    figure.stroke = !section.flag(u"NoLine");
    return figure;
}

XmlElement &row(XmlElement &section, const QString &type, int ix)
{
    XmlElement &r = appendElement(section, QStringLiteral("Row"));
    r.setAttribute({}, QStringLiteral("T"), type);
    r.setAttribute({}, QStringLiteral("IX"), QString::number(ix));
    return r;
}

XmlElementPtr newSection(int ix, bool noFill, bool noLine)
{
    XmlElementPtr s = makeElement(QStringLiteral("Section"));
    s->setAttribute({}, QStringLiteral("N"), QStringLiteral("Geometry"));
    s->setAttribute({}, QStringLiteral("IX"), QString::number(ix));
    appendCell(*s, QStringLiteral("NoFill"), noFill ? QStringLiteral("1") : QStringLiteral("0"));
    appendCell(*s, QStringLiteral("NoLine"), noLine ? QStringLiteral("1") : QStringLiteral("0"));
    appendCell(*s, QStringLiteral("NoShow"), QStringLiteral("0"));
    appendCell(*s, QStringLiteral("NoSnap"), QStringLiteral("0"));
    appendCell(*s, QStringLiteral("NoQuickDrag"), QStringLiteral("0"));
    return s;
}

} // namespace

void appendArcThrough(QPainterPath &path, QPointF p0, QPointF p1, QPointF p2, double angle, double ratio)
{
    if (std::abs(ratio) < 1e-9) {
        ratio = 1;
    }
    const double c = std::cos(angle);
    const double s = std::sin(angle);
    // Into the space where the ellipse is a circle, and back.
    const auto to = [&](QPointF p) { return QPointF(p.x() * c + p.y() * s, (p.y() * c - p.x() * s) * ratio); };
    const auto from = [&](QPointF q) {
        const double y = q.y() / ratio;
        return QPointF(q.x() * c - y * s, q.x() * s + y * c);
    };
    const QPointF a = to(p0);
    const QPointF b = to(p1);
    const QPointF e = to(p2);
    const double d = 2 * (a.x() * (b.y() - e.y()) + b.x() * (e.y() - a.y()) + e.x() * (a.y() - b.y()));
    const double scale = std::max({QLineF(a, b).length(), QLineF(b, e).length(), QLineF(a, e).length(), 1e-9});
    if (std::abs(d) < 1e-9 * scale * scale) {
        path.lineTo(p2);
        return;
    }
    const double a2 = a.x() * a.x() + a.y() * a.y();
    const double b2 = b.x() * b.x() + b.y() * b.y();
    const double e2 = e.x() * e.x() + e.y() * e.y();
    const QPointF centre((a2 * (b.y() - e.y()) + b2 * (e.y() - a.y()) + e2 * (a.y() - b.y())) / d,
                         (a2 * (e.x() - b.x()) + b2 * (a.x() - e.x()) + e2 * (b.x() - a.x())) / d);
    const double r = QLineF(centre, a).length();
    const auto angleOf = [&](QPointF p) { return std::atan2(p.y() - centre.y(), p.x() - centre.x()); };
    const auto norm = [](double v) {
        v = std::fmod(v, 2 * kPi);
        return v < 0 ? v + 2 * kPi : v;
    };
    const double t0 = angleOf(a);
    const double ccw = norm(angleOf(e) - t0);
    const double mid = norm(angleOf(b) - t0);
    const double sweep = mid <= ccw ? ccw : ccw - 2 * kPi; // through p1
    const int segments = std::max(1, int(std::ceil(std::abs(sweep) / (kPi / 2) - 1e-9)));
    const double delta = sweep / segments;
    const double k = 4.0 / 3.0 * std::tan(delta / 4);
    for (int i = 0; i < segments; ++i) {
        const double s0 = t0 + delta * i;
        const double s1 = s0 + delta;
        const QPointF q0 = centre + QPointF(std::cos(s0), std::sin(s0)) * r;
        const QPointF q3 = centre + QPointF(std::cos(s1), std::sin(s1)) * r;
        const QPointF q1 = q0 + QPointF(-std::sin(s0), std::cos(s0)) * (k * r);
        const QPointF q2 = q3 - QPointF(-std::sin(s1), std::cos(s1)) * (k * r);
        path.cubicTo(from(q1), from(q2), i + 1 == segments ? p2 : from(q3));
    }
}

QList<Figure> readGeometry(const Sheet &sheet, double width, double height)
{
    QList<Figure> out;
    for (const Section *section : sheet.sections(u"Geometry")) {
        if (section->flag(u"NoShow")) {
            continue;
        }
        Figure figure = readSection(*section, width, height);
        if (!figure.path.isEmpty() && figure.path.elementCount() > 1) {
            out.append(figure);
        }
    }
    return out;
}

XmlElementPtr outlineSection(const QPainterPath &normalized, int ix, bool noFill, bool noLine)
{
    XmlElementPtr s = newSection(ix, noFill, noLine);
    const auto x = [](double v) { return numberText(v); };
    const auto y = [](double v) { return numberText(1.0 - v); }; // y down → Visio's y up
    int index = 1;
    for (int i = 0; i < normalized.elementCount(); ++i) {
        const QPainterPath::Element e = normalized.elementAt(i);
        if (e.isMoveTo() || e.isLineTo()) {
            XmlElement &r = row(*s, e.isMoveTo() ? QStringLiteral("RelMoveTo") : QStringLiteral("RelLineTo"), index++);
            appendCell(r, QStringLiteral("X"), x(e.x));
            appendCell(r, QStringLiteral("Y"), y(e.y));
        } else if (e.isCurveTo() && i + 2 < normalized.elementCount()) {
            const QPainterPath::Element c2 = normalized.elementAt(i + 1);
            const QPainterPath::Element end = normalized.elementAt(i + 2);
            XmlElement &r = row(*s, QStringLiteral("RelCubBezTo"), index++);
            appendCell(r, QStringLiteral("X"), x(end.x));
            appendCell(r, QStringLiteral("Y"), y(end.y));
            appendCell(r, QStringLiteral("A"), x(e.x));
            appendCell(r, QStringLiteral("B"), y(e.y));
            appendCell(r, QStringLiteral("C"), x(c2.x));
            appendCell(r, QStringLiteral("D"), y(c2.y));
            i += 2;
        }
    }
    return s;
}

XmlElementPtr routeSection(const QVector<QPointF> &points, bool curved, double width, double height, int ix)
{
    XmlElementPtr s = newSection(ix, true, false);
    if (points.isEmpty()) {
        return s;
    }
    XmlElement &move = row(*s, QStringLiteral("MoveTo"), 1);
    appendCell(move, QStringLiteral("X"), numberText(points.first().x()));
    appendCell(move, QStringLiteral("Y"), numberText(points.first().y()));
    if (curved && points.size() == 4) {
        const double w = std::abs(width) > 1e-9 ? width : 1;
        const double h = std::abs(height) > 1e-9 ? height : 1;
        XmlElement &r = row(*s, QStringLiteral("RelCubBezTo"), 2);
        appendCell(r, QStringLiteral("X"), numberText(points.at(3).x() / w));
        appendCell(r, QStringLiteral("Y"), numberText(points.at(3).y() / h));
        appendCell(r, QStringLiteral("A"), numberText(points.at(1).x() / w));
        appendCell(r, QStringLiteral("B"), numberText(points.at(1).y() / h));
        appendCell(r, QStringLiteral("C"), numberText(points.at(2).x() / w));
        appendCell(r, QStringLiteral("D"), numberText(points.at(2).y() / h));
        return s;
    }
    for (qsizetype i = 1; i < points.size(); ++i) {
        XmlElement &r = row(*s, QStringLiteral("LineTo"), int(i) + 1);
        appendCell(r, QStringLiteral("X"), numberText(points.at(i).x()));
        appendCell(r, QStringLiteral("Y"), numberText(points.at(i).y()));
    }
    return s;
}

} // namespace QindaOffice::Diagram::Visio
