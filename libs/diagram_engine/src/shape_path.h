// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Custom outlines (M65, D-073): Shape::paths hold subpaths normalized to the
// shape's box (0..1, y down) so a resize scales them. These helpers are the
// one place that turns them into local geometry, a text form for the
// .qdiagram file, and the glyph strokes a stencil paints over its outline
// (for writers that need the look as geometry, e.g. Visio export).
//
// AGENT-CONTRACT (drawing_io.cpp ⇄ .qdiagram): the text form is
// "M x y L x y C x1 y1 x2 y2 x y …" with plain decimal numbers; a reader
// that meets anything else skips the path rather than guessing.

#include "drawing.h"

#include <QJsonObject>
#include <QList>
#include <QPainterPath>
#include <QSizeF>
#include <QString>

namespace QindaOffice::Diagram {

[[nodiscard]] QString pathToText(const QPainterPath &path);
[[nodiscard]] QPainterPath pathFromText(const QString &text);

// The .qdiagram members "paths", "textBox" and "ports": written only when
// present, so drawings of stencils keep their exact JSON.
void customToJson(const Shape &shape, QJsonObject &object);
void customFromJson(const QJsonObject &object, Shape &shape);

// `normalized` scaled to `size` (the shape's local space).
[[nodiscard]] QPainterPath scaledPath(const QPainterPath &normalized, QSizeF size);
// The union of a shape's custom subpaths in local space (the hit area).
[[nodiscard]] QPainterPath customOutline(const Shape &shape);
// True when at least one custom subpath is filled and closed.
[[nodiscard]] bool customOutlineFills(const Shape &shape);

// What the stencil paints over its filled outline (network icons, UML and
// container rules, the note's flap …) as stroke paths in local space, in
// the order painted. Fills a glyph makes (an accent strip, a header band)
// are not included: they have colours of their own.
[[nodiscard]] QList<QPainterPath> glyphStrokes(const Shape &shape);

} // namespace QindaOffice::Diagram
