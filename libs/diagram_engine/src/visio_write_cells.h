// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the Visio writer (M65, D-073): cells shared by shapes and
// connectors — placement, line, fill, text and shape data — written as the
// model's values, with formulas only where they keep a shape behaving in
// Visio as it does here (the pin stays centred, the text block and the
// connection points scale with the shape).

#include "visio_sheet.h"
#include "visio_write_p.h"

namespace QindaOffice::Diagram::Visio {

// A 2-D shape's placement in its parent (inches, radians).
struct Placement {
    double pinX = 0;
    double pinY = 0;
    double width = 0;
    double height = 0;
    double angle = 0;
    bool flipX = false;
    double pxPerInch = kPx;
};

[[nodiscard]] double pxPerInch(const QTransform &toParent);
[[nodiscard]] Placement placementOf(const Shape &shape, const QTransform &toParent);
void writePlacement(XmlElement &shape, const Placement &p);

void writeLine(XmlElement &shape, const QColor &color, double widthPx, LinePattern pattern, double opacity = 1.0);
void writeFill(XmlElement &shape, const ShapeStyle &style);
[[nodiscard]] int visioArrow(ArrowStyle style);

// The text block (normalized `box`, y down) and VerticalAlign of `style`.
void writeTextBlock(XmlElement &shape, const QRectF &box, const Placement &p, const TextStyle &style);
// <Text>, Character and Paragraph replacing the shape's own.
void writeText(XmlElement &shape, const QString &text, const TextStyle &style, const QString &family);

// A Property section for `data` (fresh shapes); the fields named in
// `linked` are external data (M87: rows "_VisDM_…" with DataLinked=1).
[[nodiscard]] XmlElementPtr dataSection(const QList<DataField> &data, const QStringList &linked = {});
// Applies the difference between `before` and `after` to the shape's
// Property rows; `merged` is the source sheet with its master, `master`
// the master's sheet alone (rows it has are deleted with Del="1").
void updateData(XmlElement &shape, const QList<DataField> &before, const QList<DataField> &after, const Sheet &merged,
                const Sheet *master, const QStringList &linked = {});
// A Connection section for normalized points (y down) on a shape of
// `width` × `height` inches.
[[nodiscard]] XmlElementPtr connectionSection(const QList<QPointF> &points, double width, double height);

// Hides every Geometry section `master` gives the shape and drops the
// shape's own, returning the first free section index.
int clearGeometry(XmlElement &shape, const XmlElement *master);

// Inserts `element` among `shape`'s children before its text, foreign data
// and nested shapes (the order Visio writes).
void insertChild(XmlElement &shape, XmlElementPtr element);

} // namespace QindaOffice::Diagram::Visio
