// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Edit operations on a drawing (M50, D-050). Every function changes the
// values it is given and reports whether anything changed; the caller
// (DrawingDocument::edit) snapshots before and after, so each call the app
// makes is exactly one undo step. Pure C++ over the model — the app's
// service is a QVariant veneer over these, and the engine tests drive them
// directly.
//
// Semantics a Visio user expects, decided here once:
//  * Groups are one level deep: grouping members of other groups regroups
//    them; selecting any member selects the group (expandGroups).
//  * A container (container, lanes, phase, team) carries the shapes lying
//    inside it when it moves (containerMembers).
//  * Deleting a shape leaves its connectors, their ends unglued where the
//    shape was.

#include "drawing.h"

#include <QByteArray>
#include <QPointF>
#include <QString>
#include <QStringList>

#include <optional>

namespace QindaOffice::Diagram::Ops {

enum class Align { Left, Center, Right, Top, Middle, Bottom };
enum class Order { Front, Forward, Backward, Back };

struct ShapeStylePatch {
    std::optional<QColor> fill;
    std::optional<QColor> gradient;
    std::optional<QColor> line;
    std::optional<double> lineWidth;
    std::optional<LinePattern> pattern;
    std::optional<double> cornerRadius;
    std::optional<bool> shadow;
    std::optional<double> opacity;
};

struct TextStylePatch {
    std::optional<QString> family;
    std::optional<double> size;
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<bool> underline;
    std::optional<QColor> color;
    std::optional<Qt::Alignment> horizontal; // Left/HCenter/Right
    std::optional<Qt::Alignment> vertical;   // Top/VCenter/Bottom
};

struct ConnectorPatch {
    std::optional<Routing> routing;
    std::optional<ArrowStyle> startArrow;
    std::optional<ArrowStyle> endArrow;
    std::optional<double> labelPosition;
};

// ---- creation and removal ----
// A shape of stencil `type` centred at `centre`, styled by the drawing's
// theme, with the stencil's default text unless `text` is given. Empty id
// for an unknown type.
QString addShape(Drawing &drawing, Page &page, const QString &type, QPointF centre,
                 const std::optional<QString> &text = std::nullopt);
QString addConnector(Drawing &drawing, Page &page, const Endpoint &from, const Endpoint &to,
                     Routing routing = Routing::Orthogonal);
// Removes shapes and connectors; returns how many items went.
int removeItems(Page &page, const QStringList &ids);

// ---- selection helpers ----
[[nodiscard]] QStringList expandGroups(const Page &page, const QStringList &ids);
[[nodiscard]] QStringList containerMembers(const Page &page, const QString &containerId);
// The topmost shape under `point` (page units), or empty.
[[nodiscard]] QString shapeAt(const Page &page, QPointF point, const QStringList &ignore = {});
// A glue target at `point`: a connection point within `tolerance`, else
// the shape's outline (dynamic glue), else a free point.
[[nodiscard]] Endpoint glueAt(const Page &page, QPointF point, double tolerance, const QStringList &ignore = {});

// ---- geometry ----
bool setShapeGeometry(Page &page, const QString &id, const QRectF &rect, double rotation);
// Moves shapes (carrying container members) and the free ends of the
// connectors listed or attached only to moved shapes.
bool moveItems(Page &page, const QStringList &ids, QPointF delta);
bool align(Page &page, const QStringList &ids, Align edge);
bool distribute(Page &page, const QStringList &ids, Qt::Orientation orientation);
// Rotates the selection about its centre (each shape turns by `degrees`).
bool rotate(Page &page, const QStringList &ids, double degrees);
bool flip(Page &page, const QStringList &ids, Qt::Orientation orientation);
bool setAspectLocked(Page &page, const QStringList &ids, bool locked);
bool reorder(Page &page, const QStringList &ids, Order order);
bool setEndpoint(Page &page, const QString &connectorId, bool start, const Endpoint &end);

// ---- grouping, duplication, clipboard ----
QString group(Drawing &drawing, Page &page, const QStringList &ids);
bool ungroup(Page &page, const QStringList &ids);
// Copies of the items shifted by `offset`; returns the new ids. Connectors
// between copied shapes are copied and re-glued to the copies.
QStringList duplicate(Drawing &drawing, Page &page, const QStringList &ids, QPointF offset);
// Pastes a clipboard fragment with fresh ids, shifted by `offset`.
QStringList paste(Drawing &drawing, Page &page, const QByteArray &fragment, QPointF offset,
                  QString *error = nullptr);

// ---- text, style and data ----
bool setText(Page &page, const QString &id, const QString &text);
bool applyShapeStyle(Page &page, const QStringList &ids, const ShapeStylePatch &patch);
bool applyTextStyle(Page &page, const QStringList &ids, const TextStylePatch &patch);
bool applyConnectorPatch(Page &page, const QStringList &ids, const ConnectorPatch &patch);
// Format painter: `source`'s look (style, text style; for connectors line,
// arrows and routing) onto `targets`.
bool copyFormat(Page &page, const QString &source, const QStringList &targets);
bool setData(Page &page, const QString &id, const QString &key, const QString &value);
bool removeData(Page &page, const QString &id, const QString &key);

// ---- find and replace ----
[[nodiscard]] QStringList find(const Page &page, const QString &text);
int replaceText(Page &page, const QString &find, const QString &replacement);

// ---- pages ----
QString addPage(Drawing &drawing, int index, const QString &name, const Page *like = nullptr);
QString duplicatePage(Drawing &drawing, const QString &pageId);
bool removePage(Drawing &drawing, const QString &pageId);
bool movePage(Drawing &drawing, const QString &pageId, int toIndex);
bool renamePage(Drawing &drawing, const QString &pageId, const QString &name);
// Grows or shrinks the page to its content plus `margin` (moving the content
// so the margin is even), keeping the orientation's paper when it fits.
bool fitPageToContents(Page &page, double margin);

} // namespace QindaOffice::Diagram::Ops
