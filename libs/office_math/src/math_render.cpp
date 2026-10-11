// SPDX-License-Identifier: GPL-3.0-or-later
// EquationLayout (math_layout.h): the public face of the Layouter, caret
// geometry and hit testing, and the high-DPI image a host can embed.
#include "math_layout.h"
#include "math_layout_p.h"

#include <QPainter>

#include <cmath>

namespace QindaOffice::Math {

EquationLayout::EquationLayout(const Equation &equation, const LayoutOptions &options)
{
    const QString requested = options.mathFont.isEmpty() ? options.settings.mathFont : options.mathFont;
    MathFont font = MathFont::open(requested);
    if (!font.isValid()) {
        return; // no OpenType math font installed: nothing to draw
    }
    m_family = font.family();
    Layouter layouter(options, font);
    m_box = layouter.layout(equation);
    m_slots = layouter.slotGeometry(m_box);
    m_valid = true;
}

void EquationLayout::paint(QPainter &painter, const QPointF &origin) const
{
    if (m_valid) {
        m_box.paint(painter, origin);
    }
}

QRectF EquationLayout::caretRect(const MathPosition &position) const
{
    for (const SlotGeometry &g : m_slots) {
        if (!g.slot.sameSlot(position) || g.stops.isEmpty()) {
            continue;
        }
        const int offset = qBound(0, position.offset, int(g.stops.size()) - 1);
        return QRectF(g.stops.at(offset), g.baseline - g.ascent, 0, g.ascent + g.descent);
    }
    return {};
}

MathPosition EquationLayout::positionAt(const QPointF &point) const
{
    // The innermost slot under the point (smallest box), else the nearest.
    const SlotGeometry *best = nullptr;
    qreal bestArea = 0;
    qreal bestDistance = 0;
    for (const SlotGeometry &g : m_slots) {
        const QRectF r = g.rect.adjusted(-1, -1, 1, 1);
        const qreal dx = qMax(qMax(r.left() - point.x(), 0.0), point.x() - r.right());
        const qreal dy = qMax(qMax(r.top() - point.y(), 0.0), point.y() - r.bottom());
        const qreal distance = std::hypot(dx, dy);
        const qreal area = r.width() * r.height();
        if (best == nullptr || distance < bestDistance || (distance == bestDistance && area < bestArea)) {
            best = &g;
            bestDistance = distance;
            bestArea = area;
        }
    }
    MathPosition out;
    if (best == nullptr) {
        return out;
    }
    out = best->slot;
    int nearest = 0;
    for (int i = 1; i < best->stops.size(); ++i) {
        if (std::abs(best->stops.at(i) - point.x()) < std::abs(best->stops.at(nearest) - point.x())) {
            nearest = i;
        }
    }
    out.offset = nearest;
    return out;
}

RenderedEquation renderEquation(const Equation &equation, const LayoutOptions &options, qreal devicePixelRatio)
{
    RenderedEquation out;
    const EquationLayout layout(equation, options);
    if (!layout.isValid()) {
        return out;
    }
    const qreal dpr = devicePixelRatio > 0 ? devicePixelRatio : 1;
    QRectF area = layout.box().bounds();
    if (const QRectF ink = layout.inkBounds(); !ink.isNull()) {
        area = area.united(ink);
    }
    area.adjust(-1, -1, 1, 1); // antialiasing fringe
    const QSize size(qMax(1, int(std::ceil(area.width() * dpr))), qMax(1, int(std::ceil(area.height() * dpr))));
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    image.setDevicePixelRatio(dpr);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        layout.paint(painter, -area.topLeft());
    }
    out.image = image;
    out.origin = -area.topLeft();
    out.width = layout.width();
    out.ascent = layout.ascent();
    out.descent = layout.descent();
    return out;
}

} // namespace QindaOffice::Math
