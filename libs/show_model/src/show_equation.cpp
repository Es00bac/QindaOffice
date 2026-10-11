// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_equation.h"
#include "math_omml.h"
#include <QPainter>
namespace QindaOffice::Show {
Math::EquationLayout equationLayout(const Shape &shape)
{
    const auto equation = Math::readOmmlFragment(shape.equationXml);
    Math::LayoutOptions options;
    options.dpi = 72; options.fontSizePt = shape.equationSize; options.placeholders = false;
    return equation ? Math::EquationLayout(*equation, options) : Math::EquationLayout();
}
QSizeF equationSize(const Shape &shape)
{
    const auto layout = equationLayout(shape);
    return {qMax<qreal>(24, layout.width()), qMax<qreal>(24, layout.ascent() + layout.descent())};
}
void paintEquation(QPainter *painter, const Shape &shape)
{
    const auto layout = equationLayout(shape);
    const QRectF rect = shape.rect.normalized();
    painter->save();
    if (!layout.isValid()) {
        painter->setPen(Qt::black);
        painter->drawText(rect, Qt::AlignCenter, QStringLiteral("Equation unavailable"));
    } else {
        const qreal scale = qMin(rect.width() / qMax<qreal>(1, layout.width()),
                                 rect.height() / qMax<qreal>(1, layout.ascent() + layout.descent()));
        painter->translate(rect.center()); painter->scale(scale, scale);
        layout.paint(*painter, QPointF(-layout.width() / 2, (layout.ascent() - layout.descent()) / 2));
    }
    painter->restore();
}
}
