// SPDX-License-Identifier: GPL-3.0-or-later
// One sheet's cells as model values: placement, fill, line, arrows, text
// block, shape data, connection points (visio_read_p.h).
#include "visio_read_p.h"

#include <QLineF>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::Visio {

namespace {

constexpr double kTiny = 1e-6;

int intOf(const Cell *cell, int fallback)
{
    if (cell == nullptr || cell->themed()) {
        return fallback;
    }
    return int(std::lround(cell->number().value_or(fallback)));
}

QColor withTransparency(QColor color, const Cell *trans)
{
    if (color.isValid() && trans != nullptr && !trans->themed()) {
        color.setAlphaF(float(std::clamp(1.0 - trans->number().value_or(0), 0.0, 1.0)));
    }
    return color;
}

LinePattern patternOf(int visio)
{
    // Visio has 23 dash patterns; the model draws four.
    switch (visio) {
    case 1:
        return LinePattern::Solid;
    case 3:
    case 10:
    case 11:
    case 16:
    case 23:
        return LinePattern::Dot;
    case 4:
    case 5:
    case 8:
    case 14:
    case 15:
    case 19:
    case 21:
        return LinePattern::DashDot;
    default:
        return visio > 1 ? LinePattern::Dash : LinePattern::Solid;
    }
}

} // namespace

QTransform localTransform(const Lookup &l)
{
    const double w = l.number(u"Width", 0);
    const double h = l.number(u"Height", 0);
    QTransform t;
    t.translate(l.number(u"PinX", 0), l.number(u"PinY", 0));
    t.rotateRadians(l.number(u"Angle", 0));
    t.scale(l.number(u"FlipX", 0) != 0 ? -1 : 1, l.number(u"FlipY", 0) != 0 ? -1 : 1);
    t.translate(-l.number(u"LocPinX", w / 2), -l.number(u"LocPinY", h / 2));
    return t;
}

Frame frameOf(const QTransform &toPage, const QRectF &box)
{
    // AGENT-NOTE: the model draws local (0,0)-(w,h), y down, through
    // translate(centre) · rotate · flip · translate(-w/2,-h/2). Visio's local
    // space is y up, so the model's frame is toPage after the mirror
    // diag(1/k, -1/k); its linear part is a rotation, or a rotation after a
    // mirror (one flip — two flips are a half turn).
    const double k = std::hypot(toPage.m11(), toPage.m12());
    Frame f;
    const QPointF centre = toPage.map(box.center());
    const double w = std::max(1.0, box.width() * k);
    const double h = std::max(1.0, box.height() * k);
    f.rect = QRectF(centre - QPointF(w / 2, h / 2), QSizeF(w, h));
    if (k < kTiny) {
        return f;
    }
    double m11 = toPage.m11() / k;
    double m12 = toPage.m12() / k;
    const double m21 = -toPage.m21() / k;
    const double m22 = -toPage.m22() / k;
    f.flipX = m11 * m22 - m12 * m21 < 0;
    if (f.flipX) {
        m11 = -m11;
        m12 = -m12;
    }
    double degrees = std::atan2(m12, m11) * 180.0 / 3.14159265358979323846;
    if (std::abs(degrees) < 1e-7) {
        degrees = 0;
    }
    f.rotation = degrees;
    return f;
}

QTransform normalizer(const QRectF &box)
{
    const double w = std::max(box.width(), kTiny);
    const double h = std::max(box.height(), kTiny);
    // x' = (x - left) / w ; y' = (top - y) / h with top = bottom + h.
    return QTransform(1 / w, 0, 0, -1 / h, -box.left() / w, (box.top() + h) / h);
}

QRectF localBox(const Lookup &lookup, const QList<Figure> &figures, double pxPerInch)
{
    const double w = lookup.number(u"Width", 0);
    const double h = lookup.number(u"Height", 0);
    double x0 = std::min(0.0, w);
    double x1 = std::max(0.0, w);
    double y0 = std::min(0.0, h);
    double y1 = std::max(0.0, h);
    QRectF geometry;
    for (const Figure &f : figures) {
        geometry = geometry.isNull() ? f.path.boundingRect() : geometry | f.path.boundingRect();
    }
    const double half = 0.5 / std::max(pxPerInch, 1.0); // half a page unit
    if (x1 - x0 < kTiny) {
        x0 = std::min(x0, geometry.left());
        x1 = std::max(x1, geometry.right());
        if (x1 - x0 < kTiny) {
            x0 -= half;
            x1 += half;
        }
    }
    if (y1 - y0 < kTiny) {
        y0 = std::min(y0, geometry.top());
        y1 = std::max(y1, geometry.bottom());
        if (y1 - y0 < kTiny) {
            y0 -= half;
            y1 += half;
        }
    }
    return QRectF(x0, y0, x1 - x0, y1 - y0);
}

void readLine(const Lookup &lookup, QColor &color, double &width, LinePattern &pattern)
{
    const int visio = intOf(lookup.cell(u"LinePattern"), 1);
    pattern = patternOf(visio);
    width = std::max(0.0, lookup.lineWeight() * kPx);
    if (visio == 0) {
        color = QColor(Qt::transparent);
        return;
    }
    color = lookup.color(u"LineColor", ColorRole::Line);
    if (!color.isValid()) {
        color = QColor(Qt::black);
    }
    color = withTransparency(color, lookup.cell(u"LineColorTrans"));
}

void readShapeStyle(const Lookup &lookup, ShapeStyle &style)
{
    readLine(lookup, style.line, style.lineWidth, style.pattern);
    style.cornerRadius = 0;
    style.opacity = 1;
    const int pattern = intOf(lookup.cell(u"FillPattern"), 1);
    const bool gradient = intOf(lookup.cell(u"FillGradientEnabled"), 0) != 0;
    QColor fg = withTransparency(lookup.color(u"FillForegnd", ColorRole::Fill), lookup.cell(u"FillForegndTrans"));
    QColor bg = withTransparency(lookup.color(u"FillBkgnd", ColorRole::Fill), lookup.cell(u"FillBkgndTrans"));
    if (!fg.isValid()) {
        fg = QColor(Qt::white);
    }
    if (!bg.isValid()) {
        bg = QColor(Qt::white);
    }
    style.gradient = QColor();
    if (gradient) {
        // Visio 2013 gradient stops: the first and last colours, drawn
        // top to bottom (the model's one gradient direction).
        QList<QColor> stops;
        if (const Section *s = lookup.sheet().section(u"FillGradient")) {
            for (const Row *row : s->orderedRows()) {
                const QColor c = lookup.colorOf(row->cell(u"GradientStopColor"), ColorRole::Fill);
                if (c.isValid()) {
                    stops.append(withTransparency(c, row->cell(u"GradientStopColorTrans")));
                }
            }
        }
        style.fill = stops.isEmpty() ? fg : stops.first();
        style.gradient = stops.size() > 1 ? stops.last() : QColor();
    } else if (pattern == 0) {
        style.fill = QColor(Qt::transparent);
    } else if (pattern >= 25 && pattern <= 40) {
        // Legacy gradient patterns: 30 runs background (top) to foreground,
        // the others are shown foreground (top) to background.
        style.fill = pattern == 30 ? bg : fg;
        style.gradient = pattern == 30 ? fg : bg;
    } else {
        style.fill = fg; // patterns 2–24 (hatches) are shown solid
    }
    style.shadow = intOf(lookup.cell(u"ShdwPattern"), 0) > 0;
}

ArrowStyle arrowOf(int visio)
{
    switch (visio) {
    case 0:
        return ArrowStyle::None;
    case 1:
    case 3:
    case 7:
    case 9:
    case 12:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 40:
    case 43:
    case 44:
        return ArrowStyle::Open;
    case 10:
    case 20:
    case 29:
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
    case 38:
    case 41:
    case 42:
        return ArrowStyle::Circle;
    case 22:
        return ArrowStyle::Diamond;
    default:
        return ArrowStyle::Filled;
    }
}

QRectF textBoxOf(const Lookup &l, const QRectF &box)
{
    const double w = l.number(u"Width", 0);
    const double h = l.number(u"Height", 0);
    const double tw = std::abs(l.number(u"TxtWidth", w));
    const double th = std::abs(l.number(u"TxtHeight", h));
    const double left = l.number(u"TxtPinX", w / 2) - l.number(u"TxtLocPinX", tw / 2);
    const double bottom = l.number(u"TxtPinY", h / 2) - l.number(u"TxtLocPinY", th / 2);
    QRectF area(left + l.number(u"LeftMargin", 0), bottom + l.number(u"BottomMargin", 0),
                tw - l.number(u"LeftMargin", 0) - l.number(u"RightMargin", 0),
                th - l.number(u"TopMargin", 0) - l.number(u"BottomMargin", 0));
    if (area.width() < kTiny || area.height() < kTiny) {
        area = QRectF(left, bottom, std::max(tw, kTiny), std::max(th, kTiny));
    }
    return normalizer(box).mapRect(area);
}

QList<DataField> shapeData(const Sheet &sheet)
{
    QList<DataField> out;
    const Section *section = sheet.section(u"Property");
    if (section == nullptr) {
        return out;
    }
    for (const auto &[name, row] : section->namedRows()) {
        if (row->number(u"Invisible", 0) != 0) {
            continue; // kept in the file; not shown, as in Visio
        }
        const Cell *label = row->cell(u"Label");
        QString key = label != nullptr && !label->value.trimmed().isEmpty() ? label->value.trimmed() : name;
        const Cell *value = row->cell(u"Value");
        const QString text = value == nullptr || value->formula == QLatin1String("No Formula") ? QString() : value->value;
        if (std::any_of(out.cbegin(), out.cend(), [&](const DataField &f) { return f.key.compare(key, Qt::CaseInsensitive) == 0; })) {
            key = QStringLiteral("%1 (%2)").arg(key, name);
        }
        out.append({key, text});
    }
    return out;
}

QList<QPointF> connectionPoints(const Sheet &sheet, const QRectF &box)
{
    QList<QPointF> out;
    const Section *section = sheet.section(u"Connection");
    if (section == nullptr) {
        return out;
    }
    const QTransform n = normalizer(box);
    for (const Row *row : section->orderedRows()) {
        out.append(n.map(QPointF(row->number(u"X"), row->number(u"Y"))));
    }
    return out;
}

QColor readableOn(const QColor &fill)
{
    if (!fill.isValid() || fill.alpha() < 64) {
        return QColor(Qt::black);
    }
    const double luminance = 0.2126 * fill.redF() + 0.7152 * fill.greenF() + 0.0722 * fill.blueF();
    return luminance < 0.5 ? QColor(Qt::white) : QColor(Qt::black);
}

} // namespace QindaOffice::Diagram::Visio
