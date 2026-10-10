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
#include <QDateTime>
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
// Also clears every page's reference to it when it was a background page.
bool removePage(Drawing &drawing, const QString &pageId);
bool movePage(Drawing &drawing, const QString &pageId, int toIndex);
bool renamePage(Drawing &drawing, const QString &pageId, const QString &name);
// Grows or shrinks the page to its content plus `margin` (moving the content
// so the margin is even), keeping the orientation's paper when it fits.
bool fitPageToContents(Page &page, double margin);

// ==== M87 (D-095): Visio's everyday drawing tools ====

// ---- layers (ops_layers.cpp) ----
// A new layer named `name` (made unique: "Layer 2" …); its index.
int addLayer(Page &page, const QString &name);
// Removes layer `index`; items keep their other layers (indexes shift).
bool removeLayer(Page &page, int index);
// Replaces layer `index`'s properties; a blank or taken name is refused.
bool setLayer(Page &page, int index, const Layer &layer);
// Puts the items on exactly `layers` (indexes; unknown ones are dropped).
bool assignLayers(Page &page, const QStringList &ids, const QList<int> &layers);

// ---- hyperlinks and comments (ops_layers.cpp) ----
bool setLinks(Page &page, const QStringList &ids, const QList<Hyperlink> &links);
// The page a link inside the drawing goes to (subAddress = a page name, or
// "page/…"), or empty.
[[nodiscard]] QString linkTargetPage(const Drawing &drawing, const Hyperlink &link);
// A comment on `itemId` (empty: the page), or a reply when `parentId` names
// one; empty text is refused. Returns the comment's id.
QString addComment(Drawing &drawing, Page &page, const QString &itemId, const QString &text, const QString &author,
                   const QString &initials, const QDateTime &when, const QString &parentId = {});
bool editComment(Page &page, const QString &commentId, const QString &text, const QDateTime &when);
// Done marks a whole thread (the comment and its replies).
bool setCommentDone(Page &page, const QString &commentId, bool done);
// Removes a comment and its replies; returns how many went.
int removeComment(Page &page, const QString &commentId);

// ---- background pages, Auto Size, print setup (ops_layers.cpp) ----
// Marks a page as a background page (or not; its users then lose it).
bool setBackgroundPage(Drawing &drawing, const QString &pageId, bool background);
// Shows background page `backId` behind `pageId` (empty: none). Refused for
// a foreground page, the page itself or a cycle.
bool setBackPage(Drawing &drawing, const QString &pageId, const QString &backId);
// A new background page (after every page) shown behind `forPageId`.
QString addBackgroundPage(Drawing &drawing, const QString &forPageId);
// Visio's Borders & Titles: a border and a title block (`title`) on the
// background page of `pageId`, made when the page has none.
bool addBorderAndTitle(Drawing &drawing, const QString &pageId, const QString &title);
// Auto Size: grows (or shrinks) the page by whole tiles of its print paper
// (else its size when Auto Size was turned on) so every item fits; content
// left of or above the page moves in by whole tiles. False when unchanged.
bool applyAutoSize(Page &page);

// ---- containers and swimlanes (ops_containers.cpp) ----
[[nodiscard]] ContainerResize effectiveResize(const Shape &container);
[[nodiscard]] double containerMargin(const Shape &container);
// Lanes of the pool `laneId` belongs to (itself alone when it stands
// alone), in stacking order; empty when it is not a lane.
[[nodiscard]] QStringList poolOf(const Page &page, const QString &laneId);
[[nodiscard]] bool isLane(const Shape &shape);
[[nodiscard]] Qt::Orientation laneOrientation(const Shape &lane);
// A container around `ids` (fitted to them, behind them); its id.
QString wrapInContainer(Drawing &drawing, Page &page, const QStringList &ids, const QString &type = QStringLiteral("container"));
bool fitToContents(Page &page, const QStringList &containerIds);
// Removes the containers only; their members stay where they are.
bool disband(Page &page, const QStringList &containerIds);
bool setContainerResize(Page &page, const QStringList &containerIds, ContainerResize resize);
bool setContainerMargin(Page &page, const QStringList &containerIds, double margin);
// The user's Delete: a container takes its members with it, and a lane's
// pool closes the gap it leaves. Returns how many items went.
int deleteItems(Page &page, const QStringList &ids);
// A lane of `laneId`'s kind before or after it (its pool shifts to make
// room); with no lane given, a lane of `type` appended to the nearest pool
// of that orientation, else a new one centred at `at`. Returns its id.
QString insertLane(Drawing &drawing, Page &page, const QString &laneId, bool after, const QString &type, QPointF at);
// A phase (separator) at `at`: across the whole pool there, else alone.
QString insertPhase(Drawing &drawing, Page &page, QPointF at);
// Visio's container behaviour after an edit, against the page as it was:
// containers expand around (or fit) the members they had and the shapes
// newly dropped in them, pools stay stacked and equally long, phases keep
// spanning their pool. One pass; false when nothing moved.
bool settle(Page &page, const Page &before);

// ---- AutoConnect (ops_connect.cpp) ----
// Visio's AutoConnect arrows: from `fromId` towards `side` (0 top, 1 right,
// 2 bottom, 3 left) connect to the nearest shape lying in line; failing
// that, add a shape of `stencilId` (empty: like the source, without text)
// `gap` beyond the source, clear of other shapes, and connect to it.
// Returns the shape connected to; `connectorId` gets the new connector.
QString autoConnect(Drawing &drawing, Page &page, const QString &fromId, int side, const QString &stencilId,
                    double gap = 48, QString *connectorId = nullptr);

} // namespace QindaOffice::Diagram::Ops
