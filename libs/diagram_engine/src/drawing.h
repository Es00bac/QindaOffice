// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The QindaDiagram document model (M50, D-050): a Drawing of Pages, each
// holding Shapes (stencil instances with geometry, text, style and shape
// data) and Connectors (lines glued to shapes). Plain values — copied
// cheaply through Qt's implicit sharing, so an undo step is a snapshot.
//
// Units: one page unit is 1/96 inch (a CSS pixel), the unit QindaTK's
// canvas and SVG use. Page origin is the top-left corner, y grows down.
// A shape's `rect` is its UNROTATED box; `rotation` (degrees, clockwise)
// turns it about the box centre, `flipX`/`flipY` mirror it locally.
//
// AGENT-CONTRACT (drawing_io.cpp): every struct carries `extra`, the JSON
// members this version does not know. The reader keeps them and the writer
// puts them back, so a file from a newer minor revision survives a save.
//
// Z-order: shapes paint in vector order (last on top), then every connector
// in vector order above all shapes (docs/diagram.md "Z-order").

#include <QByteArray>
#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace QindaOffice::Diagram {

enum class LinePattern { Solid, Dash, Dot, DashDot };
enum class ArrowStyle { None, Filled, Open, Diamond, Circle };
enum class Routing { Orthogonal, Straight, Curved };

// Text formatting for a whole shape or connector label ("basic formatting":
// one run per shape, D-050).
struct TextStyle {
    QString family;         // empty: the drawing theme's font
    double size = 11;       // points
    bool bold = false;
    bool italic = false;
    bool underline = false;
    QColor color = QColor(31, 41, 55);
    Qt::Alignment align = Qt::AlignHCenter | Qt::AlignVCenter;
    friend bool operator==(const TextStyle &, const TextStyle &) = default;
};

struct ShapeStyle {
    QColor fill = Qt::white;       // invalid or alpha 0: no fill
    QColor gradient;               // valid: vertical gradient fill -> gradient
    QColor line = QColor(71, 85, 105); // invalid or alpha 0: no line
    double lineWidth = 1.5;        // page units
    LinePattern pattern = LinePattern::Solid;
    double cornerRadius = 0;       // page units, rectangles only
    bool shadow = false;
    double opacity = 1.0;
    friend bool operator==(const ShapeStyle &, const ShapeStyle &) = default;
};

// One row of shape data (Visio "Shape Data"): ordered, keys unique per item.
struct DataField {
    QString key;
    QString value;
    friend bool operator==(const DataField &, const DataField &) = default;
};

// One subpath of a custom outline (M65, D-073: geometry read from Visio).
// `path` is in the shape's local box NORMALIZED to 0..1 (y down), so it
// scales with the shape; `fill`/`stroke` are per subpath, as Visio's
// NoFill/NoLine are per Geometry section.
struct ShapePath {
    QPainterPath path;
    bool fill = true;
    bool stroke = true;
    friend bool operator==(const ShapePath &, const ShapePath &) = default;
};

struct Shape {
    QString id;
    QString type;            // stencil id (stencils.h)
    QRectF rect;
    double rotation = 0;
    bool flipX = false;
    bool flipY = false;
    QString text;
    TextStyle textStyle;
    ShapeStyle style;
    QList<DataField> data;
    QString groupId;         // one level of grouping; empty = ungrouped
    bool locked = false;
    bool aspectLocked = false;
    QByteArray image;        // encoded picture bytes (picture / photo shapes)
    // M65 (D-073), all empty for the stencils' own look:
    QList<ShapePath> paths;  // a custom outline replacing the stencil's
    QRectF textBox;          // the text area, normalized like `paths`
    QList<QPointF> ports;    // connection points, normalized (0..1, 0..1)
    QJsonObject extra;
    friend bool operator==(const Shape &, const Shape &) = default;
};

// A connector end: glued to a shape (a connection point `port`, or the
// outline when port < 0) or free at `point` (page units).
struct Endpoint {
    QString shapeId;
    int port = -1;
    QPointF point;
    [[nodiscard]] bool glued() const { return !shapeId.isEmpty(); }
    friend bool operator==(const Endpoint &, const Endpoint &) = default;
};

struct Connector {
    QString id;
    Endpoint from;
    Endpoint to;
    Routing routing = Routing::Orthogonal;
    QColor line = QColor(71, 85, 105);
    double lineWidth = 1.5;
    LinePattern pattern = LinePattern::Solid;
    ArrowStyle startArrow = ArrowStyle::None;
    ArrowStyle endArrow = ArrowStyle::Filled;
    QString text;
    TextStyle textStyle;
    double labelPosition = 0.5; // fraction of the route length
    QList<DataField> data;
    QString groupId;
    QJsonObject extra;
    friend bool operator==(const Connector &, const Connector &) = default;
};

struct Page {
    QString id;
    QString name;
    QSizeF size = QSizeF(1056, 816); // US Letter landscape
    QColor background = Qt::white;
    bool grid = true;                // background grid on screen (never printed)
    double gridSpacing = 24;         // a quarter inch
    bool lineJumps = false;          // hops where connectors cross
    QVector<Shape> shapes;
    QVector<Connector> connectors;
    QJsonObject extra;

    [[nodiscard]] int shapeIndex(const QString &shapeId) const;
    [[nodiscard]] int connectorIndex(const QString &connectorId) const;
    [[nodiscard]] const Shape *shape(const QString &shapeId) const;
    [[nodiscard]] Shape *shape(const QString &shapeId);
    [[nodiscard]] const Connector *connector(const QString &connectorId) const;
    [[nodiscard]] Connector *connector(const QString &connectorId);
    [[nodiscard]] bool contains(const QString &itemId) const;
    friend bool operator==(const Page &, const Page &) = default;
};

struct Drawing {
    QString title;
    QString themeId = QStringLiteral("qinda");
    QVector<Page> pages;
    int serial = 0;          // id counter: ids are never reused in a drawing
    QJsonObject extra;

    [[nodiscard]] int pageIndex(const QString &pageId) const;
    [[nodiscard]] const Page *page(const QString &pageId) const;
    [[nodiscard]] Page *page(const QString &pageId);
    // A fresh id: `prefix` + counter ("s" shapes, "c" connectors, "p" pages,
    // "g" groups). Never repeats an id already in the drawing.
    QString newId(const QString &prefix);
    friend bool operator==(const Drawing &, const Drawing &) = default;
};

// A drawing with one empty page ("Page-1"), the state of File > New.
[[nodiscard]] Drawing newDrawing();
// Page sizes by name ("letter", "legal", "tabloid", "a4", "a3", "a5");
// portrait. An unknown name gives letter.
[[nodiscard]] QSizeF paperSize(const QString &name);
[[nodiscard]] QStringList paperNames();
// "letter", "a4", … for a size in either orientation, or "custom".
[[nodiscard]] QString paperNameOf(QSizeF size);

// Text of the data field `key`, or `fallback`.
[[nodiscard]] QString dataValue(const QList<DataField> &data, const QString &key, const QString &fallback = {});
// Sets (or appends) `key`; an empty key is refused.
bool setDataValue(QList<DataField> &data, const QString &key, const QString &value);

} // namespace QindaOffice::Diagram
