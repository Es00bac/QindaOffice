// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The stencil catalogue (M50, D-050): every shape type QindaDiagram draws,
// grouped the way the Shapes panel shows them — Basic, Flowchart, Org chart,
// Network, Containers, UML and Arrows & callouts — with its default size,
// text, theme role, connection points and how its text is placed.
//
// Geometry and drawing live here too, in the shape's LOCAL space
// (0,0)-(w,h), before rotation/flip: `outline` is the fill and hit area,
// `textRect` where the label goes, `paintGlyph` the strokes a stencil draws
// on top of its outline (network icons, UML compartments, the org card's
// photo placeholder). painter.cpp composes them; nothing else draws shapes.
//
// AGENT-CONTRACT (with the .qdiagram format): a stencil id is persisted in
// every shape (`type`); never rename one. An unknown id (a newer file) draws
// as a plain rectangle and keeps its id.

#include "drawing.h"

#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

class QPainter;

namespace QindaOffice::Diagram {

// Which of the theme's colour pairs a new shape takes (themes.h).
enum class StyleRole { Primary, Decision, Terminal, Data, Neutral, Accent, Container, Note, Plain };

enum class TextPlacement {
    Inside,   // centred in the outline
    Below,    // under an icon (network, actor)
    Header,   // in a container's or lane's header band
    Card,     // an org chart card: name and title beside the photo
};

struct Stencil {
    QString id;
    QString category;       // category id (categoryIds())
    QString title;          // translated
    QString keywords;       // extra search words, translated
    QSizeF size;            // default size in page units
    QString defaultText;    // translated; may be empty
    StyleRole role = StyleRole::Primary;
    TextPlacement text = TextPlacement::Inside;
    bool container = false; // carries the shapes inside it when moved
    bool aspectLocked = false;
    bool palette = true;    // offered in the Shapes panel
    QVector<QPointF> ports; // connection points, normalized (0..1, 0..1)
};

[[nodiscard]] const QVector<Stencil> &stencils();
[[nodiscard]] const Stencil *stencil(const QString &id);
// Category ids in panel order, and their translated titles.
[[nodiscard]] QStringList categoryIds();
[[nodiscard]] QString categoryTitle(const QString &categoryId);

// Local-space geometry of `shape` (its rect size, type and corner radius).
[[nodiscard]] QPainterPath outline(const Shape &shape);
// Where the text goes, local space. May extend outside the outline (Below).
[[nodiscard]] QRectF textRect(const Shape &shape);
// The header band of a container or lane (local), or an empty rect.
[[nodiscard]] QRectF headerRect(const Shape &shape);
// Whether the stencil paints its outline with fill and line (false for
// text, icons without a tile, the UML actor).
[[nodiscard]] bool paintsOutline(const Shape &shape);
// Strokes drawn over the filled outline, local space, in `ink`.
void paintGlyph(QPainter *painter, const Shape &shape, const QColor &ink);
// Connection points of the shape's stencil (normalized); four side
// midpoints for an unknown type.
[[nodiscard]] QVector<QPointF> portsOf(const Shape &shape);
[[nodiscard]] bool isContainer(const Shape &shape);

} // namespace QindaOffice::Diagram
