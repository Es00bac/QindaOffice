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
// AGENT-CONTRACT (drawing_io.cpp, drawing_io_m87.cpp): every struct stored
// as a JSON object of its own (drawing, page, shape, connector, layer,
// comment, recordset) carries `extra`, the members this version does not
// know. The reader keeps them and the writer puts them back, so a file from
// a newer minor revision survives a save. Small values (a hyperlink, a data
// link, a data field) are written whole and read by their known members.
//
// Z-order: shapes paint in vector order (last on top), then every connector
// in vector order above all shapes (docs/diagram.md "Z-order").

#include <QByteArray>
#include <QColor>
#include <QDateTime>
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

// ---- M87 (D-095): Visio's everyday drawing tools ----

// A hyperlink on a shape or connector (Visio's Insert ▸ Link): a web
// address or file, or a page of this drawing (`subAddress` = its name).
struct Hyperlink {
    QString description;
    QString address;
    QString subAddress;
    friend bool operator==(const Hyperlink &, const Hyperlink &) = default;
};

// How a container (or lane) follows its members (Visio's Container Format ▸
// Automatic Resize). Default: the stencil's (phases never resize, every
// other container expands as needed).
enum class ContainerResize { Default, Expand, Fit, None };

// The external data a shape is linked to (Visio's Link Data to Shapes):
// a row of a DataRecordset, by the row's key (DataRecordset::rowKey).
struct DataLink {
    QString recordset;
    QString row;
    [[nodiscard]] bool linked() const { return !recordset.isEmpty(); }
    friend bool operator==(const DataLink &, const DataLink &) = default;
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
    // M87 (D-095), all empty/default for a plain shape:
    QList<int> layers;       // indexes into Page::layers
    QList<Hyperlink> links;
    DataLink dataLink;
    // Visio's container kinds for a shape that is not a container stencil
    // (a container read from Visio): "container", "lane" or "phase".
    QString structure;
    ContainerResize containerResize = ContainerResize::Default;
    double containerMargin = -1; // page units; < 0: the default margin
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
    QList<int> layers;          // M87: as Shape::layers
    QList<Hyperlink> links;     // M87
    QJsonObject extra;
    friend bool operator==(const Connector &, const Connector &) = default;
};

// A layer of a page (Visio's Layer Properties; M87). Items list the layers
// they are on by index. As in Visio, an item is hidden when every layer it
// is on is hidden, left off paper when none of them prints, and locked when
// any of them is locked; a layer colour replaces its items' colours.
struct Layer {
    QString name;
    bool visible = true;
    bool print = true;
    bool locked = false;
    bool active = false;     // new items go on the active layers
    bool snap = true;        // kept for Visio (not modelled: QindaDiagram snaps to every shape)
    bool glue = true;        // connectors glue to its shapes
    QColor color;            // invalid: the items keep their own colours
    QJsonObject extra;
    friend bool operator==(const Layer &, const Layer &) = default;
};

// A comment (Visio's Review ▸ New Comment; M87) on an item or the page.
// A reply names the comment it answers (`parentId`); Visio's comments part
// has no threads, so replies travel to Visio as comments on the same item.
struct Comment {
    QString id;              // "m…"
    QString author;
    QString initials;
    QDateTime date;          // UTC
    QString text;
    bool done = false;
    QString itemId;          // a shape or connector; empty: the page
    QString parentId;
    QJsonObject extra;
    friend bool operator==(const Comment &, const Comment &) = default;
};

// Line jumps (Visio's Page Setup ▸ Layout and Routing; M87).
enum class JumpStyle { Arc, Gap, Square };
// Which connector of two that cross hops: the one routed later (the
// default, Visio's "last routed line"), the horizontal or the vertical one.
enum class JumpOn { LastRouted, Horizontal, Vertical };

// Printing one page (Visio's Print Setup; M87): its own printer paper and
// either a print zoom (1 = actual size, tiled across as many sheets as it
// takes) or "fit to" a number of sheets across and down. The default — no
// paper, fit to 1 × 1 — is the printer's paper with the page fitted on one
// sheet (what QindaDiagram always did).
struct PrintSetup {
    QSizeF paper;            // page units, as oriented; empty: the printer's paper
    double zoom = 0;         // > 0: print zoom; 0: fit to sheets
    int sheetsAcross = 1;
    int sheetsDown = 1;
    bool center = true;      // centre the drawing on the sheets
    [[nodiscard]] bool isDefault() const { return paper.isEmpty() && zoom <= 0 && sheetsAcross == 1 && sheetsDown == 1 && center; }
    friend bool operator==(const PrintSetup &, const PrintSetup &) = default;
};

// External data (Visio's Link Data to Shapes; M87): a table read from an
// Excel workbook or a CSV file, kept in the drawing so the drawing travels
// with it; Refresh reads the source again.
struct DataRecordset {
    QString id;              // "r…"
    QString name;
    QString source;          // the file it was read from
    QString sheet;           // the worksheet, for a workbook
    QStringList columns;
    QVector<QStringList> rows; // values as text, a cell per column
    int keyColumn = -1;      // the column identifying a row; -1: its position
    QDateTime refreshed;     // UTC
    QJsonObject extra;
    // The key of `row` (DataLink::row): the key column's value, else "#n".
    [[nodiscard]] QString rowKey(int row) const;
    [[nodiscard]] int rowOf(const QString &key) const;
    friend bool operator==(const DataRecordset &, const DataRecordset &) = default;
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
    // M87 (D-095):
    JumpStyle jumpStyle = JumpStyle::Arc;
    JumpOn jumpOn = JumpOn::LastRouted;
    QVector<Layer> layers;
    QVector<Comment> comments;
    bool backgroundPage = false;     // a Visio background page: shown behind others
    QString backPageId;              // the background page shown behind this one
    bool autoSize = false;           // Visio's Auto Size: the page grows by paper tiles
    PrintSetup print;
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
    QVector<DataRecordset> recordsets; // M87: external data
    int serial = 0;          // id counter: ids are never reused in a drawing
    QJsonObject extra;

    [[nodiscard]] int pageIndex(const QString &pageId) const;
    [[nodiscard]] const Page *page(const QString &pageId) const;
    [[nodiscard]] Page *page(const QString &pageId);
    [[nodiscard]] const DataRecordset *recordset(const QString &recordsetId) const;
    [[nodiscard]] DataRecordset *recordset(const QString &recordsetId);
    // A fresh id: `prefix` + counter ("s" shapes, "c" connectors, "p" pages,
    // "g" groups, "m" comments, "r" recordsets). Never repeats an id
    // already in the drawing.
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
