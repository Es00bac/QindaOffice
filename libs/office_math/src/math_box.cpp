// SPDX-License-Identifier: GPL-3.0-or-later
#include "math_box.h"

#include <QPainter>
#include <QPen>

namespace QindaOffice::Math {

namespace {

QRectF unite(const QRectF &a, const QRectF &b)
{
    if (a.isNull()) {
        return b;
    }
    if (b.isNull()) {
        return a;
    }
    return a.united(b);
}

} // namespace

QRectF Box::inkBounds() const
{
    QRectF out;
    if (hidden) {
        return out;
    }
    switch (type) {
    case Type::Glyphs:
        out = path.boundingRect();
        break;
    case Type::Rule:
    case Type::Placeholder:
        out = rect;
        break;
    case Type::Stroke:
        out = path.boundingRect().adjusted(-pen / 2, -pen / 2, pen / 2, pen / 2);
        break;
    case Type::Group:
        break;
    }
    for (const Box &child : children) {
        const QRectF r = child.inkBounds();
        if (!r.isNull()) {
            out = unite(out, r.translated(child.pos));
        }
    }
    return out;
}

void Box::paint(QPainter &painter, const QPointF &origin) const
{
    if (hidden) {
        return;
    }
    switch (type) {
    case Type::Glyphs:
        // Frozen outlines survive arbitrary scene-graph transforms and PDF
        // paint engines without sharing Qt's thread-affine font engines.
        painter.fillPath(path.translated(origin), color);
        break;
    case Type::Rule:
        painter.fillRect(rect.translated(origin), color);
        break;
    case Type::Stroke: {
        painter.save();
        QPen stroke(color, pen);
        stroke.setCapStyle(Qt::FlatCap);
        stroke.setJoinStyle(Qt::MiterJoin);
        painter.setPen(stroke);
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path.translated(origin));
        painter.restore();
        break;
    }
    case Type::Placeholder: {
        painter.save();
        QPen dotted(color, pen, Qt::DotLine);
        painter.setPen(dotted);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(rect.translated(origin));
        painter.restore();
        break;
    }
    case Type::Group:
        break;
    }
    for (const Box &child : children) {
        child.paint(painter, origin + child.pos);
    }
}

Box Box::group()
{
    return Box();
}

Box Box::rule(const QRectF &rect, const QColor &color)
{
    Box b;
    b.type = Type::Rule;
    b.rect = rect;
    b.color = color;
    b.width = qMax<qreal>(0, rect.right());
    b.ascent = qMax<qreal>(0, -rect.top());
    b.descent = qMax<qreal>(0, rect.bottom());
    return b;
}

void Box::add(Box child, const QPointF &at)
{
    child.pos = at;
    children.push_back(std::move(child));
}

void Box::fitChildren()
{
    for (const Box &c : children) {
        width = qMax(width, c.pos.x() + c.width);
        ascent = qMax(ascent, c.ascent - c.pos.y());
        descent = qMax(descent, c.descent + c.pos.y());
    }
}

void Box::absorbItalic()
{
    width += italic;
    italic = 0;
}

} // namespace QindaOffice::Math
