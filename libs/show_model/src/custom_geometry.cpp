// SPDX-License-Identifier: GPL-3.0-or-later
// a:custGeom for the renderer: the guide formulas (ECMA-376 Part 1
// §20.1.9.11: "*/", "+-", "?:", "at2", "pin", …), the built-in guide names
// (w, h, ss, wd2, cd4, …) and the path commands (moveTo, lnTo, arcTo,
// quadBezTo, cubicBezTo, close), each path in its own w×h space scaled to
// the shape.

#include "ooxml_namespaces.h"
#include "ooxml_xml.h"
#include "preset_geometry.h"

#include <QHash>
#include <QtMath>

namespace QindaOffice::Show::Geometry {

namespace {

namespace Ooxml = QindaOffice::Ooxml;

const QString kA = QString::fromUtf16(Ooxml::Ns::A);

// Angles in guide formulas are 1/60000 degree.
constexpr qreal kAngleUnit = 60000.0;

qreal toRadians(qreal angle)
{
    return qDegreesToRadians(angle / kAngleUnit);
}

class Guides {
public:
    Guides(qreal w, qreal h)
    {
        const qreal ss = qMin(w, h);
        const qreal ls = qMax(w, h);
        m_values = {
            {QStringLiteral("w"), w},       {QStringLiteral("h"), h},         {QStringLiteral("l"), 0},
            {QStringLiteral("t"), 0},       {QStringLiteral("r"), w},         {QStringLiteral("b"), h},
            {QStringLiteral("hc"), w / 2},  {QStringLiteral("vc"), h / 2},    {QStringLiteral("ss"), ss},
            {QStringLiteral("ls"), ls},     {QStringLiteral("cd2"), 10800000}, {QStringLiteral("cd4"), 5400000},
            {QStringLiteral("cd8"), 2700000}, {QStringLiteral("3cd4"), 16200000}, {QStringLiteral("3cd8"), 8100000},
            {QStringLiteral("5cd8"), 13500000}, {QStringLiteral("7cd8"), 18900000},
        };
        for (int d : {2, 3, 4, 5, 6, 8, 10, 12, 16, 32}) {
            m_values.insert(QStringLiteral("wd%1").arg(d), w / d);
            m_values.insert(QStringLiteral("hd%1").arg(d), h / d);
            m_values.insert(QStringLiteral("ssd%1").arg(d), ss / d);
        }
    }

    [[nodiscard]] qreal value(const QString &token) const
    {
        const auto found = m_values.constFind(token);
        if (found != m_values.constEnd()) {
            return found.value();
        }
        return token.toDouble();
    }

    void define(const QString &name, const QString &formula)
    {
        const QStringList parts = formula.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            return;
        }
        auto arg = [&](int i) { return i < parts.size() ? value(parts.at(i)) : 0.0; };
        const QString op = parts.first();
        const qreal x = arg(1);
        const qreal y = arg(2);
        const qreal z = arg(3);
        qreal result = 0;
        if (op == u"val") {
            result = x;
        } else if (op == u"*/") {
            result = z != 0 ? x * y / z : 0;
        } else if (op == u"+-") {
            result = x + y - z;
        } else if (op == u"+/") {
            result = z != 0 ? (x + y) / z : 0;
        } else if (op == u"?:") {
            result = x > 0 ? y : z;
        } else if (op == u"abs") {
            result = std::abs(x);
        } else if (op == u"at2") {
            result = qRadiansToDegrees(std::atan2(y, x)) * kAngleUnit;
        } else if (op == u"cat2") {
            result = x * std::cos(std::atan2(z, y));
        } else if (op == u"sat2") {
            result = x * std::sin(std::atan2(z, y));
        } else if (op == u"cos") {
            result = x * std::cos(toRadians(y));
        } else if (op == u"sin") {
            result = x * std::sin(toRadians(y));
        } else if (op == u"tan") {
            result = x * std::tan(toRadians(y));
        } else if (op == u"max") {
            result = qMax(x, y);
        } else if (op == u"min") {
            result = qMin(x, y);
        } else if (op == u"mod") {
            result = std::sqrt(x * x + y * y + z * z);
        } else if (op == u"pin") {
            result = y < x ? x : (y > z ? z : y);
        } else if (op == u"sqrt") {
            result = std::sqrt(qMax(0.0, x));
        }
        m_values.insert(name, result);
    }

private:
    QHash<QString, qreal> m_values;
};

// The point on an ellipse of radii (rx, ry) at the VISUAL angle `angle`
// (DrawingML measures arc angles as seen, not parametrically).
qreal parametric(qreal rx, qreal ry, qreal angleRadians)
{
    return std::atan2(rx * std::sin(angleRadians), ry * std::cos(angleRadians));
}

} // namespace

Outline customOutline(const QString &fragment, const QRectF &rect)
{
    const Ooxml::XmlElementPtr root = Ooxml::parseFragment(fragment);
    if (!root) {
        return {};
    }
    const QRectF box = rect.normalized();
    Guides shapeGuides(box.width(), box.height());
    for (const char16_t *list : {u"avLst", u"gdLst"}) {
        if (const Ooxml::XmlElement *guides = root->child(kA, list)) {
            for (const Ooxml::XmlElement *gd : guides->elements(kA, u"gd")) {
                shapeGuides.define(gd->attr(u"name"), gd->attr(u"fmla"));
            }
        }
    }
    Outline outline;
    const Ooxml::XmlElement *pathList = root->child(kA, u"pathLst");
    if (pathList == nullptr) {
        return outline;
    }
    for (const Ooxml::XmlElement *pathElement : pathList->elements(kA, u"path")) {
        // A path's own coordinate space; 0 = the shape's.
        const qreal pw = pathElement->attr(u"w").toDouble();
        const qreal ph = pathElement->attr(u"h").toDouble();
        const qreal sx = pw > 0 ? box.width() / pw : 1.0;
        const qreal sy = ph > 0 ? box.height() / ph : 1.0;
        const Guides &g = shapeGuides;
        const auto point = [&](const Ooxml::XmlElement *pt) {
            return QPointF(box.left() + g.value(pt->attr(u"x")) * sx, box.top() + g.value(pt->attr(u"y")) * sy);
        };
        QPainterPath path;
        QPointF current = box.topLeft();
        for (const Ooxml::XmlElement *command : pathElement->elements()) {
            const QList<const Ooxml::XmlElement *> pts = command->elements(kA, u"pt");
            if (command->name == u"moveTo" && !pts.isEmpty()) {
                current = point(pts.first());
                path.moveTo(current);
            } else if (command->name == u"lnTo" && !pts.isEmpty()) {
                current = point(pts.first());
                path.lineTo(current);
            } else if (command->name == u"quadBezTo" && pts.size() >= 2) {
                current = point(pts.at(1));
                path.quadTo(point(pts.at(0)), current);
            } else if (command->name == u"cubicBezTo" && pts.size() >= 3) {
                current = point(pts.at(2));
                path.cubicTo(point(pts.at(0)), point(pts.at(1)), current);
            } else if (command->name == u"arcTo") {
                const qreal rx = g.value(command->attr(u"wR")) * sx;
                const qreal ry = g.value(command->attr(u"hR")) * sy;
                const qreal start = toRadians(g.value(command->attr(u"stAng")));
                const qreal sweep = toRadians(g.value(command->attr(u"swAng")));
                if (rx <= 0 || ry <= 0) {
                    continue;
                }
                const qreal from = parametric(rx, ry, start);
                const qreal to = parametric(rx, ry, start + sweep);
                qreal span = to - from;
                // Keep the sweep's direction and size beyond ±180°.
                while (sweep > 0 && span < 0) {
                    span += 2 * M_PI;
                }
                while (sweep < 0 && span > 0) {
                    span -= 2 * M_PI;
                }
                if (std::abs(sweep) >= 2 * M_PI - 1e-9) {
                    span = sweep > 0 ? 2 * M_PI : -2 * M_PI;
                }
                const QPointF centre = current - QPointF(rx * std::cos(from), ry * std::sin(from));
                const QRectF ellipse(centre.x() - rx, centre.y() - ry, 2 * rx, 2 * ry);
                path.arcTo(ellipse, -qRadiansToDegrees(from), -qRadiansToDegrees(span));
                current = path.currentPosition();
            } else if (command->name == u"close") {
                path.closeSubpath();
            }
        }
        const bool filledPath = pathElement->attr(u"fill") != u"none";
        const bool stroked = pathElement->attr(u"stroke") != u"0" && pathElement->attr(u"stroke") != u"false";
        if (filledPath) {
            outline.fill.addPath(path);
        }
        if (stroked) {
            outline.stroke.addPath(path);
        }
    }
    return outline;
}

} // namespace QindaOffice::Show::Geometry
