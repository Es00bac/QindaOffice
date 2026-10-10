// SPDX-License-Identifier: GPL-3.0-or-later
// The .qdiagram members of M87 (drawing_io_m87.h).
#include "drawing_io_m87.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram::FileM87 {

namespace {

QString colorText(const QColor &color)
{
    if (!color.isValid()) {
        return {};
    }
    return color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb);
}

QColor colorOf(const QJsonValue &value)
{
    const QColor color(value.toString());
    return color.isValid() ? color : QColor();
}

QJsonArray intsToJson(const QList<int> &values)
{
    QJsonArray out;
    for (const int v : values) {
        out.append(v);
    }
    return out;
}

QList<int> intsFromJson(const QJsonValue &value)
{
    QList<int> out;
    for (const QJsonValue &v : value.toArray()) {
        const int i = v.toInt(-1);
        if (i >= 0 && !out.contains(i)) {
            out.append(i);
        }
    }
    return out;
}

QJsonArray linksToJson(const QList<Hyperlink> &links)
{
    QJsonArray out;
    for (const Hyperlink &l : links) {
        out.append(QJsonObject{{QStringLiteral("description"), l.description},
                               {QStringLiteral("address"), l.address},
                               {QStringLiteral("subAddress"), l.subAddress}});
    }
    return out;
}

QList<Hyperlink> linksFromJson(const QJsonValue &value)
{
    QList<Hyperlink> out;
    for (const QJsonValue &v : value.toArray()) {
        const QJsonObject o = v.toObject();
        Hyperlink l{o.value(QLatin1String("description")).toString(), o.value(QLatin1String("address")).toString(),
                    o.value(QLatin1String("subAddress")).toString()};
        if (!l.address.isEmpty() || !l.subAddress.isEmpty()) {
            out.append(l);
        }
    }
    return out;
}

QJsonObject without(QJsonObject object, std::initializer_list<const char *> keys)
{
    for (const char *key : keys) {
        object.remove(QLatin1String(key));
    }
    return object;
}

QJsonObject merged(const QJsonObject &extra, const QJsonObject &known)
{
    QJsonObject out = extra;
    for (auto it = known.begin(); it != known.end(); ++it) {
        out.insert(it.key(), it.value());
    }
    return out;
}

QString dateText(const QDateTime &when)
{
    return when.isValid() ? when.toUTC().toString(Qt::ISODate) : QString();
}

QDateTime dateOf(const QJsonValue &value)
{
    QDateTime when = QDateTime::fromString(value.toString(), Qt::ISODate);
    if (when.isValid()) {
        when = when.toUTC();
    }
    return when;
}

const char *resizeName(ContainerResize resize)
{
    switch (resize) {
    case ContainerResize::Expand: return "expand";
    case ContainerResize::Fit: return "fit";
    case ContainerResize::None: return "none";
    case ContainerResize::Default: break;
    }
    return "";
}

ContainerResize resizeOf(const QString &text)
{
    if (text == QLatin1String("expand")) return ContainerResize::Expand;
    if (text == QLatin1String("fit")) return ContainerResize::Fit;
    if (text == QLatin1String("none")) return ContainerResize::None;
    return ContainerResize::Default;
}

QJsonObject layerToJson(const Layer &l)
{
    QJsonObject known{{QStringLiteral("name"), l.name},       {QStringLiteral("visible"), l.visible},
                      {QStringLiteral("print"), l.print},     {QStringLiteral("locked"), l.locked},
                      {QStringLiteral("active"), l.active},   {QStringLiteral("snap"), l.snap},
                      {QStringLiteral("glue"), l.glue},       {QStringLiteral("color"), colorText(l.color)}};
    return merged(l.extra, known);
}

Layer layerFromJson(const QJsonObject &o)
{
    Layer l;
    l.name = o.value(QLatin1String("name")).toString();
    l.visible = o.value(QLatin1String("visible")).toBool(true);
    l.print = o.value(QLatin1String("print")).toBool(true);
    l.locked = o.value(QLatin1String("locked")).toBool();
    l.active = o.value(QLatin1String("active")).toBool();
    l.snap = o.value(QLatin1String("snap")).toBool(true);
    l.glue = o.value(QLatin1String("glue")).toBool(true);
    l.color = colorOf(o.value(QLatin1String("color")));
    l.extra = without(o, {"name", "visible", "print", "locked", "active", "snap", "glue", "color"});
    return l;
}

QJsonObject commentToJson(const Comment &c)
{
    QJsonObject known{{QStringLiteral("id"), c.id},          {QStringLiteral("author"), c.author},
                      {QStringLiteral("initials"), c.initials}, {QStringLiteral("date"), dateText(c.date)},
                      {QStringLiteral("text"), c.text},      {QStringLiteral("done"), c.done},
                      {QStringLiteral("item"), c.itemId},    {QStringLiteral("parent"), c.parentId}};
    return merged(c.extra, known);
}

Comment commentFromJson(const QJsonObject &o)
{
    Comment c;
    c.id = o.value(QLatin1String("id")).toString();
    c.author = o.value(QLatin1String("author")).toString();
    c.initials = o.value(QLatin1String("initials")).toString();
    c.date = dateOf(o.value(QLatin1String("date")));
    c.text = o.value(QLatin1String("text")).toString();
    c.done = o.value(QLatin1String("done")).toBool();
    c.itemId = o.value(QLatin1String("item")).toString();
    c.parentId = o.value(QLatin1String("parent")).toString();
    c.extra = without(o, {"id", "author", "initials", "date", "text", "done", "item", "parent"});
    return c;
}

QJsonObject recordsetToJson(const DataRecordset &r)
{
    QJsonArray columns;
    for (const QString &c : r.columns) {
        columns.append(c);
    }
    QJsonArray rows;
    for (const QStringList &row : r.rows) {
        QJsonArray cells;
        for (const QString &cell : row) {
            cells.append(cell);
        }
        rows.append(cells);
    }
    QJsonObject known{{QStringLiteral("id"), r.id},         {QStringLiteral("name"), r.name},
                      {QStringLiteral("source"), r.source}, {QStringLiteral("sheet"), r.sheet},
                      {QStringLiteral("columns"), columns}, {QStringLiteral("rows"), rows},
                      {QStringLiteral("key"), r.keyColumn}, {QStringLiteral("refreshed"), dateText(r.refreshed)}};
    return merged(r.extra, known);
}

DataRecordset recordsetFromJson(const QJsonObject &o)
{
    DataRecordset r;
    r.id = o.value(QLatin1String("id")).toString();
    r.name = o.value(QLatin1String("name")).toString();
    r.source = o.value(QLatin1String("source")).toString();
    r.sheet = o.value(QLatin1String("sheet")).toString();
    for (const QJsonValue &v : o.value(QLatin1String("columns")).toArray()) {
        r.columns.append(v.toString());
    }
    for (const QJsonValue &v : o.value(QLatin1String("rows")).toArray()) {
        QStringList cells;
        for (const QJsonValue &cell : v.toArray()) {
            cells.append(cell.toString());
        }
        // AGENT-GUARD: every row has exactly one cell per column, so the
        // data window and the links can index without checks.
        while (cells.size() < r.columns.size()) {
            cells.append(QString());
        }
        cells = cells.mid(0, r.columns.size());
        r.rows.append(cells);
    }
    r.keyColumn = o.value(QLatin1String("key")).toInt(-1);
    if (r.keyColumn >= r.columns.size()) {
        r.keyColumn = -1;
    }
    r.refreshed = dateOf(o.value(QLatin1String("refreshed")));
    r.extra = without(o, {"id", "name", "source", "sheet", "columns", "rows", "key", "refreshed"});
    return r;
}

} // namespace

void shapeToJson(const Shape &s, QJsonObject &o)
{
    if (!s.layers.isEmpty()) {
        o.insert(QStringLiteral("layers"), intsToJson(s.layers));
    }
    if (!s.links.isEmpty()) {
        o.insert(QStringLiteral("links"), linksToJson(s.links));
    }
    if (s.dataLink.linked()) {
        o.insert(QStringLiteral("dataLink"), QJsonObject{{QStringLiteral("recordset"), s.dataLink.recordset},
                                                         {QStringLiteral("row"), s.dataLink.row}});
    }
    if (!s.structure.isEmpty()) {
        o.insert(QStringLiteral("structure"), s.structure);
    }
    if (s.containerResize != ContainerResize::Default) {
        o.insert(QStringLiteral("containerResize"), QString::fromLatin1(resizeName(s.containerResize)));
    }
    if (s.containerMargin >= 0) {
        o.insert(QStringLiteral("containerMargin"), s.containerMargin);
    }
}

void shapeFromJson(const QJsonObject &o, Shape &s)
{
    s.layers = intsFromJson(o.value(QLatin1String("layers")));
    s.links = linksFromJson(o.value(QLatin1String("links")));
    const QJsonObject link = o.value(QLatin1String("dataLink")).toObject();
    s.dataLink = DataLink{link.value(QLatin1String("recordset")).toString(), link.value(QLatin1String("row")).toString()};
    if (s.dataLink.row.isEmpty()) {
        s.dataLink = {};
    }
    const QString structure = o.value(QLatin1String("structure")).toString();
    if (structure == QLatin1String("container") || structure == QLatin1String("lane")
        || structure == QLatin1String("phase")) {
        s.structure = structure;
    }
    s.containerResize = resizeOf(o.value(QLatin1String("containerResize")).toString());
    const double margin = o.value(QLatin1String("containerMargin")).toDouble(-1);
    s.containerMargin = std::isfinite(margin) && margin >= 0 ? std::min(margin, 480.0) : -1;
}

void connectorToJson(const Connector &c, QJsonObject &o)
{
    if (!c.layers.isEmpty()) {
        o.insert(QStringLiteral("layers"), intsToJson(c.layers));
    }
    if (!c.links.isEmpty()) {
        o.insert(QStringLiteral("links"), linksToJson(c.links));
    }
}

void connectorFromJson(const QJsonObject &o, Connector &c)
{
    c.layers = intsFromJson(o.value(QLatin1String("layers")));
    c.links = linksFromJson(o.value(QLatin1String("links")));
}

void pageToJson(const Page &p, QJsonObject &o)
{
    if (p.jumpStyle != JumpStyle::Arc) {
        o.insert(QStringLiteral("jumpStyle"), p.jumpStyle == JumpStyle::Gap ? QStringLiteral("gap") : QStringLiteral("square"));
    }
    if (p.jumpOn != JumpOn::LastRouted) {
        o.insert(QStringLiteral("jumpOn"), p.jumpOn == JumpOn::Horizontal ? QStringLiteral("horizontal")
                                                                          : QStringLiteral("vertical"));
    }
    if (!p.layers.isEmpty()) {
        QJsonArray layers;
        for (const Layer &l : p.layers) {
            layers.append(layerToJson(l));
        }
        o.insert(QStringLiteral("layers"), layers);
    }
    if (!p.comments.isEmpty()) {
        QJsonArray comments;
        for (const Comment &c : p.comments) {
            comments.append(commentToJson(c));
        }
        o.insert(QStringLiteral("comments"), comments);
    }
    if (p.backgroundPage) {
        o.insert(QStringLiteral("backgroundPage"), true);
    }
    if (!p.backPageId.isEmpty()) {
        o.insert(QStringLiteral("backPage"), p.backPageId);
    }
    if (p.autoSize) {
        o.insert(QStringLiteral("autoSize"), true);
    }
    if (!p.print.isDefault()) {
        o.insert(QStringLiteral("print"), QJsonObject{{QStringLiteral("paperWidth"), p.print.paper.width()},
                                                      {QStringLiteral("paperHeight"), p.print.paper.height()},
                                                      {QStringLiteral("zoom"), p.print.zoom},
                                                      {QStringLiteral("across"), p.print.sheetsAcross},
                                                      {QStringLiteral("down"), p.print.sheetsDown},
                                                      {QStringLiteral("center"), p.print.center}});
    }
}

void pageFromJson(const QJsonObject &o, Page &p)
{
    const QString style = o.value(QLatin1String("jumpStyle")).toString();
    p.jumpStyle = style == QLatin1String("gap") ? JumpStyle::Gap : style == QLatin1String("square") ? JumpStyle::Square
                                                                                                      : JumpStyle::Arc;
    const QString on = o.value(QLatin1String("jumpOn")).toString();
    p.jumpOn = on == QLatin1String("horizontal") ? JumpOn::Horizontal
             : on == QLatin1String("vertical")   ? JumpOn::Vertical
                                                 : JumpOn::LastRouted;
    for (const QJsonValue &v : o.value(QLatin1String("layers")).toArray()) {
        p.layers.append(layerFromJson(v.toObject()));
    }
    for (const QJsonValue &v : o.value(QLatin1String("comments")).toArray()) {
        const Comment c = commentFromJson(v.toObject());
        if (!c.id.isEmpty()) {
            p.comments.append(c);
        }
    }
    p.backgroundPage = o.value(QLatin1String("backgroundPage")).toBool();
    p.backPageId = o.value(QLatin1String("backPage")).toString();
    p.autoSize = o.value(QLatin1String("autoSize")).toBool();
    const QJsonObject print = o.value(QLatin1String("print")).toObject();
    if (!print.isEmpty()) {
        const double w = print.value(QLatin1String("paperWidth")).toDouble();
        const double h = print.value(QLatin1String("paperHeight")).toDouble();
        p.print.paper = w >= 48 && h >= 48 && w <= 20000 && h <= 20000 ? QSizeF(w, h) : QSizeF();
        const double zoom = print.value(QLatin1String("zoom")).toDouble();
        p.print.zoom = std::isfinite(zoom) ? std::clamp(zoom, 0.0, 10.0) : 0.0;
        p.print.sheetsAcross = std::clamp(print.value(QLatin1String("across")).toInt(1), 1, 50);
        p.print.sheetsDown = std::clamp(print.value(QLatin1String("down")).toInt(1), 1, 50);
        p.print.center = print.value(QLatin1String("center")).toBool(true);
    }
    // Layer indexes past the end name nothing; drop them so every consumer
    // can index without checks.
    const int count = int(p.layers.size());
    const auto prune = [count](QList<int> &layers) { layers.removeIf([count](int i) { return i >= count; }); };
    for (Shape &s : p.shapes) {
        prune(s.layers);
    }
    for (Connector &c : p.connectors) {
        prune(c.layers);
    }
}

void drawingToJson(const Drawing &d, QJsonObject &o)
{
    if (d.recordsets.isEmpty()) {
        return;
    }
    QJsonArray recordsets;
    for (const DataRecordset &r : d.recordsets) {
        recordsets.append(recordsetToJson(r));
    }
    o.insert(QStringLiteral("recordsets"), recordsets);
}

void drawingFromJson(const QJsonObject &o, Drawing &d)
{
    for (const QJsonValue &v : o.value(QLatin1String("recordsets")).toArray()) {
        const DataRecordset r = recordsetFromJson(v.toObject());
        if (!r.id.isEmpty()) {
            d.recordsets.append(r);
        }
    }
}

void stripShapeKeys(QJsonObject &extra)
{
    extra = without(extra, {"layers", "links", "dataLink", "structure", "containerResize", "containerMargin"});
}

void stripConnectorKeys(QJsonObject &extra)
{
    extra = without(extra, {"layers", "links"});
}

void stripPageKeys(QJsonObject &extra)
{
    extra = without(extra, {"jumpStyle", "jumpOn", "layers", "comments", "backgroundPage", "backPage", "autoSize", "print"});
}

void stripDrawingKeys(QJsonObject &extra)
{
    extra = without(extra, {"recordsets"});
}

} // namespace QindaOffice::Diagram::FileM87
