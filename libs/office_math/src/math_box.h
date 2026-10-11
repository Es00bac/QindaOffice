// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The laid-out equation: a tree of boxes in TeX's sense — each has a width,
// an ascent above its baseline and a descent below it, and sits at an
// offset from its parent's origin (the left end of the parent's baseline;
// y grows DOWN, as everywhere in Qt). Leaves draw glyphs, filled rules,
// stroked paths or an empty-argument placeholder.
//
// The host places the root on its own baseline: an inline equation's
// ascent/descent are what a text line makes room for.

#include <QColor>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>

#include <vector>

class QPainter;

namespace QindaOffice::Math {

class Box {
public:
    enum class Type : quint8 { Group, Glyphs, Rule, Stroke, Placeholder };

    Type type = Type::Group;
    qreal width = 0;
    qreal ascent = 0;
    qreal descent = 0;
    qreal italic = 0;   // italic correction after the box (scripts attach after it)
    qreal accentX = -1; // top accent attachment x of a one-glyph box (< 0: none)
    QPointF pos;        // origin relative to the parent's origin
    bool hidden = false; // laid out but not drawn (m:phant without m:show)
    int slot = -1;       // index of the argument slot this box is (layout bookkeeping)
    std::vector<Box> children;

    // Glyph ids/positions are diagnostic; path holds frozen vector outlines.
    // No QRawFont survives layout: Qt raw fonts must stay on their thread.
    QList<quint32> glyphs;
    QList<QPointF> positions; // glyph origins relative to the box origin
    QColor color;
    // Glyphs: filled path; Rule: rect; Stroke: path stroked `pen` wide; Placeholder: rect
    QRectF rect;
    QPainterPath path;
    qreal pen = 0;

    [[nodiscard]] qreal height() const { return ascent + descent; }
    [[nodiscard]] QRectF bounds() const { return QRectF(0, -ascent, width, ascent + descent); }
    // Ink extents of the subtree relative to this box's origin.
    [[nodiscard]] QRectF inkBounds() const;
    void paint(QPainter &painter, const QPointF &origin) const;

    // Builders.
    [[nodiscard]] static Box group();
    [[nodiscard]] static Box rule(const QRectF &rect, const QColor &color);
    void add(Box child, const QPointF &at);
    // Grows width/ascent/descent to cover every child.
    void fitChildren();
    // Width += italic correction; italic = 0 (an argument that no script follows).
    void absorbItalic();
};

} // namespace QindaOffice::Math
