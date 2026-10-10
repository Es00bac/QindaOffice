// SPDX-License-Identifier: GPL-3.0-or-later
#include "drawing_io.h"

#include "drawing_io_m87.h"
#include "routing.h"
#include "shape_path.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace QindaOffice::Diagram {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("DiagramIo", text);
}

QString colorText(const QColor &color)
{
    if (!color.isValid()) {
        return {};
    }
    return color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb);
}

QColor colorOf(const QJsonValue &value, const QColor &fallback)
{
    if (!value.isString()) {
        return fallback;
    }
    const QString text = value.toString();
    if (text.isEmpty()) {
        return QColor();
    }
    const QColor color(text);
    return color.isValid() ? color : fallback;
}

double number(const QJsonObject &o, const char *key, double fallback)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isDouble() && std::isfinite(v.toDouble()) ? v.toDouble() : fallback;
}

template<typename E>
struct Names {
    E value;
    const char *name;
};

constexpr Names<LinePattern> kPatterns[] = {
    {LinePattern::Solid, "solid"}, {LinePattern::Dash, "dash"}, {LinePattern::Dot, "dot"}, {LinePattern::DashDot, "dashDot"}};
constexpr Names<ArrowStyle> kArrows[] = {{ArrowStyle::None, "none"},
                                         {ArrowStyle::Filled, "filled"},
                                         {ArrowStyle::Open, "open"},
                                         {ArrowStyle::Diamond, "diamond"},
                                         {ArrowStyle::Circle, "circle"}};
constexpr Names<Routing> kRoutings[] = {
    {Routing::Orthogonal, "orthogonal"}, {Routing::Straight, "straight"}, {Routing::Curved, "curved"}};

template<typename E, size_t N>
QString nameOf(const Names<E> (&table)[N], E value)
{
    for (const auto &row : table) {
        if (row.value == value) {
            return QString::fromLatin1(row.name);
        }
    }
    return QString::fromLatin1(table[0].name);
}

template<typename E, size_t N>
E valueOf(const Names<E> (&table)[N], const QJsonValue &value, E fallback)
{
    const QString text = value.toString();
    for (const auto &row : table) {
        if (text == QLatin1String(row.name)) {
            return row.value;
        }
    }
    return fallback;
}

QString alignText(Qt::Alignment align)
{
    const QString h = align & Qt::AlignLeft ? QStringLiteral("left") : align & Qt::AlignRight ? QStringLiteral("right")
                                                                                                : QStringLiteral("center");
    const QString v = align & Qt::AlignTop ? QStringLiteral("top") : align & Qt::AlignBottom ? QStringLiteral("bottom")
                                                                                               : QStringLiteral("middle");
    return h + QLatin1Char(' ') + v;
}

Qt::Alignment alignOf(const QString &text)
{
    Qt::Alignment align;
    align |= text.contains(QLatin1String("left")) ? Qt::AlignLeft : text.contains(QLatin1String("right")) ? Qt::AlignRight
                                                                                                           : Qt::AlignHCenter;
    align |= text.contains(QLatin1String("top")) ? Qt::AlignTop : text.contains(QLatin1String("bottom")) ? Qt::AlignBottom
                                                                                                          : Qt::AlignVCenter;
    return align;
}

// Writes `known` over the preserved unknown members.
QJsonObject merged(const QJsonObject &extra, const QJsonObject &known)
{
    QJsonObject out = extra;
    for (auto it = known.begin(); it != known.end(); ++it) {
        out.insert(it.key(), it.value());
    }
    return out;
}

// The members of `object` not in `known` (kept for the round trip).
QJsonObject unknown(const QJsonObject &object, std::initializer_list<const char *> known)
{
    QJsonObject out = object;
    for (const char *key : known) {
        out.remove(QLatin1String(key));
    }
    return out;
}

QJsonObject textToJson(const TextStyle &t)
{
    return {{QStringLiteral("family"), t.family}, {QStringLiteral("size"), t.size},
            {QStringLiteral("bold"), t.bold},     {QStringLiteral("italic"), t.italic},
            {QStringLiteral("underline"), t.underline}, {QStringLiteral("color"), colorText(t.color)},
            {QStringLiteral("align"), alignText(t.align)}};
}

TextStyle textFromJson(const QJsonObject &o)
{
    TextStyle t;
    t.family = o.value(QLatin1String("family")).toString();
    t.size = std::clamp(number(o, "size", t.size), 1.0, 400.0);
    t.bold = o.value(QLatin1String("bold")).toBool();
    t.italic = o.value(QLatin1String("italic")).toBool();
    t.underline = o.value(QLatin1String("underline")).toBool();
    t.color = colorOf(o.value(QLatin1String("color")), t.color);
    if (o.contains(QLatin1String("align"))) {
        t.align = alignOf(o.value(QLatin1String("align")).toString());
    }
    return t;
}

QJsonArray dataToJson(const QList<DataField> &data)
{
    QJsonArray out;
    for (const DataField &field : data) {
        out.append(QJsonObject{{QStringLiteral("key"), field.key}, {QStringLiteral("value"), field.value}});
    }
    return out;
}

QList<DataField> dataFromJson(const QJsonValue &value)
{
    QList<DataField> out;
    for (const QJsonValue &v : value.toArray()) {
        const QJsonObject o = v.toObject();
        const QString key = o.value(QLatin1String("key")).toString();
        if (!key.isEmpty()) {
            out.append({key, o.value(QLatin1String("value")).toString()});
        }
    }
    return out;
}

QJsonObject endpointToJson(const Endpoint &e)
{
    QJsonObject o{{QStringLiteral("x"), e.point.x()}, {QStringLiteral("y"), e.point.y()}};
    if (e.glued()) {
        o.insert(QStringLiteral("shape"), e.shapeId);
        o.insert(QStringLiteral("port"), e.port);
    }
    return o;
}

Endpoint endpointFromJson(const QJsonObject &o)
{
    Endpoint e;
    e.shapeId = o.value(QLatin1String("shape")).toString();
    e.port = o.value(QLatin1String("port")).toInt(-1);
    e.point = QPointF(number(o, "x", 0), number(o, "y", 0));
    return e;
}

} // namespace

QJsonObject shapeToJson(const Shape &s)
{
    const ShapeStyle &st = s.style;
    QJsonObject known{{QStringLiteral("id"), s.id},
                      {QStringLiteral("type"), s.type},
                      {QStringLiteral("x"), s.rect.x()},
                      {QStringLiteral("y"), s.rect.y()},
                      {QStringLiteral("w"), s.rect.width()},
                      {QStringLiteral("h"), s.rect.height()},
                      {QStringLiteral("rotation"), s.rotation},
                      {QStringLiteral("flipX"), s.flipX},
                      {QStringLiteral("flipY"), s.flipY},
                      {QStringLiteral("text"), s.text},
                      {QStringLiteral("textStyle"), textToJson(s.textStyle)},
                      {QStringLiteral("style"),
                       QJsonObject{{QStringLiteral("fill"), colorText(st.fill)},
                                   {QStringLiteral("gradient"), colorText(st.gradient)},
                                   {QStringLiteral("line"), colorText(st.line)},
                                   {QStringLiteral("lineWidth"), st.lineWidth},
                                   {QStringLiteral("pattern"), nameOf(kPatterns, st.pattern)},
                                   {QStringLiteral("corner"), st.cornerRadius},
                                   {QStringLiteral("shadow"), st.shadow},
                                   {QStringLiteral("opacity"), st.opacity}}},
                      {QStringLiteral("data"), dataToJson(s.data)},
                      {QStringLiteral("group"), s.groupId},
                      {QStringLiteral("locked"), s.locked},
                      {QStringLiteral("aspectLocked"), s.aspectLocked}};
    if (!s.image.isEmpty()) {
        known.insert(QStringLiteral("image"), QString::fromLatin1(s.image.toBase64()));
    }
    customToJson(s, known);
    FileM87::shapeToJson(s, known);
    return merged(s.extra, known);
}

Shape shapeFromJson(const QJsonObject &o)
{
    Shape s;
    s.id = o.value(QLatin1String("id")).toString();
    s.type = o.value(QLatin1String("type")).toString(QStringLiteral("rectangle"));
    s.rect = QRectF(number(o, "x", 0), number(o, "y", 0), std::max(1.0, number(o, "w", 120)),
                    std::max(1.0, number(o, "h", 72)));
    s.rotation = number(o, "rotation", 0);
    s.flipX = o.value(QLatin1String("flipX")).toBool();
    s.flipY = o.value(QLatin1String("flipY")).toBool();
    s.text = o.value(QLatin1String("text")).toString();
    s.textStyle = textFromJson(o.value(QLatin1String("textStyle")).toObject());
    const QJsonObject st = o.value(QLatin1String("style")).toObject();
    s.style.fill = colorOf(st.value(QLatin1String("fill")), s.style.fill);
    s.style.gradient = colorOf(st.value(QLatin1String("gradient")), QColor());
    s.style.line = colorOf(st.value(QLatin1String("line")), s.style.line);
    s.style.lineWidth = std::clamp(number(st, "lineWidth", s.style.lineWidth), 0.0, 100.0);
    s.style.pattern = valueOf(kPatterns, st.value(QLatin1String("pattern")), LinePattern::Solid);
    s.style.cornerRadius = std::max(0.0, number(st, "corner", 0));
    s.style.shadow = st.value(QLatin1String("shadow")).toBool();
    s.style.opacity = std::clamp(number(st, "opacity", 1), 0.0, 1.0);
    s.data = dataFromJson(o.value(QLatin1String("data")));
    s.groupId = o.value(QLatin1String("group")).toString();
    s.locked = o.value(QLatin1String("locked")).toBool();
    s.aspectLocked = o.value(QLatin1String("aspectLocked")).toBool();
    s.image = QByteArray::fromBase64(o.value(QLatin1String("image")).toString().toLatin1());
    customFromJson(o, s);
    FileM87::shapeFromJson(o, s);
    s.extra = unknown(o, {"id", "type", "x", "y", "w", "h", "rotation", "flipX", "flipY", "text", "textStyle", "style",
                          "data", "group", "locked", "aspectLocked", "image", "paths", "textBox", "ports"});
    FileM87::stripShapeKeys(s.extra);
    return s;
}

QJsonObject connectorToJson(const Connector &c)
{
    QJsonObject known{{QStringLiteral("id"), c.id},
                            {QStringLiteral("from"), endpointToJson(c.from)},
                            {QStringLiteral("to"), endpointToJson(c.to)},
                            {QStringLiteral("routing"), nameOf(kRoutings, c.routing)},
                            {QStringLiteral("line"), colorText(c.line)},
                            {QStringLiteral("lineWidth"), c.lineWidth},
                            {QStringLiteral("pattern"), nameOf(kPatterns, c.pattern)},
                            {QStringLiteral("startArrow"), nameOf(kArrows, c.startArrow)},
                            {QStringLiteral("endArrow"), nameOf(kArrows, c.endArrow)},
                            {QStringLiteral("text"), c.text},
                            {QStringLiteral("textStyle"), textToJson(c.textStyle)},
                            {QStringLiteral("labelPosition"), c.labelPosition},
                            {QStringLiteral("data"), dataToJson(c.data)},
                            {QStringLiteral("group"), c.groupId}};
    FileM87::connectorToJson(c, known);
    return merged(c.extra, known);
}

Connector connectorFromJson(const QJsonObject &o)
{
    Connector c;
    c.id = o.value(QLatin1String("id")).toString();
    c.from = endpointFromJson(o.value(QLatin1String("from")).toObject());
    c.to = endpointFromJson(o.value(QLatin1String("to")).toObject());
    c.routing = valueOf(kRoutings, o.value(QLatin1String("routing")), Routing::Orthogonal);
    c.line = colorOf(o.value(QLatin1String("line")), c.line);
    c.lineWidth = std::clamp(number(o, "lineWidth", c.lineWidth), 0.1, 100.0);
    c.pattern = valueOf(kPatterns, o.value(QLatin1String("pattern")), LinePattern::Solid);
    c.startArrow = valueOf(kArrows, o.value(QLatin1String("startArrow")), ArrowStyle::None);
    c.endArrow = valueOf(kArrows, o.value(QLatin1String("endArrow")), ArrowStyle::Filled);
    c.text = o.value(QLatin1String("text")).toString();
    c.textStyle = textFromJson(o.value(QLatin1String("textStyle")).toObject());
    c.labelPosition = std::clamp(number(o, "labelPosition", 0.5), 0.0, 1.0);
    c.data = dataFromJson(o.value(QLatin1String("data")));
    c.groupId = o.value(QLatin1String("group")).toString();
    FileM87::connectorFromJson(o, c);
    c.extra = unknown(o, {"id", "from", "to", "routing", "line", "lineWidth", "pattern", "startArrow", "endArrow",
                          "text", "textStyle", "labelPosition", "data", "group"});
    FileM87::stripConnectorKeys(c.extra);
    return c;
}

QJsonObject pageToJson(const Page &p)
{
    QJsonArray shapes;
    for (const Shape &s : p.shapes) {
        shapes.append(shapeToJson(s));
    }
    QJsonArray connectors;
    for (const Connector &c : p.connectors) {
        connectors.append(connectorToJson(c));
    }
    QJsonObject known{{QStringLiteral("id"), p.id},
                            {QStringLiteral("name"), p.name},
                            {QStringLiteral("width"), p.size.width()},
                            {QStringLiteral("height"), p.size.height()},
                            {QStringLiteral("background"), colorText(p.background)},
                            {QStringLiteral("grid"), p.grid},
                            {QStringLiteral("gridSpacing"), p.gridSpacing},
                            {QStringLiteral("lineJumps"), p.lineJumps},
                            {QStringLiteral("shapes"), shapes},
                            {QStringLiteral("connectors"), connectors}};
    FileM87::pageToJson(p, known);
    return merged(p.extra, known);
}

Page pageFromJson(const QJsonObject &o)
{
    Page p;
    p.id = o.value(QLatin1String("id")).toString();
    p.name = o.value(QLatin1String("name")).toString();
    p.size = QSizeF(std::clamp(number(o, "width", 1056), 48.0, 20000.0), std::clamp(number(o, "height", 816), 48.0, 20000.0));
    p.background = colorOf(o.value(QLatin1String("background")), QColor(Qt::white));
    p.grid = o.value(QLatin1String("grid")).toBool(true);
    p.gridSpacing = std::clamp(number(o, "gridSpacing", 24), 2.0, 480.0);
    p.lineJumps = o.value(QLatin1String("lineJumps")).toBool();
    for (const QJsonValue &v : o.value(QLatin1String("shapes")).toArray()) {
        p.shapes.append(shapeFromJson(v.toObject()));
    }
    for (const QJsonValue &v : o.value(QLatin1String("connectors")).toArray()) {
        p.connectors.append(connectorFromJson(v.toObject()));
    }
    FileM87::pageFromJson(o, p); // after the items: it prunes their layer indexes
    p.extra = unknown(o, {"id", "name", "width", "height", "background", "grid", "gridSpacing", "lineJumps", "shapes",
                          "connectors"});
    FileM87::stripPageKeys(p.extra);
    return p;
}

QByteArray drawingToJson(const Drawing &d)
{
    QJsonArray pages;
    for (const Page &p : d.pages) {
        pages.append(pageToJson(p));
    }
    QJsonObject known{{QStringLiteral("format"), QStringLiteral("qindadiagram")},
                      {QStringLiteral("version"), kFormatVersion},
                      {QStringLiteral("title"), d.title},
                      {QStringLiteral("theme"), d.themeId},
                      {QStringLiteral("serial"), d.serial},
                      {QStringLiteral("pages"), pages}};
    FileM87::drawingToJson(d, known);
    return QJsonDocument(merged(d.extra, known)).toJson(QJsonDocument::Indented);
}

bool drawingFromJson(const QByteArray &bytes, Drawing *drawing, QString *error)
{
    QJsonParseError parse{};
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        return fail(tr("This is not a QindaDiagram file (%1).").arg(parse.errorString()));
    }
    const QJsonObject root = document.object();
    if (root.value(QLatin1String("format")).toString() != QLatin1String("qindadiagram")) {
        return fail(tr("This is not a QindaDiagram file."));
    }
    const int version = root.value(QLatin1String("version")).toInt(0);
    if (version < 1) {
        return fail(tr("The file has no valid format version."));
    }
    if (version > kFormatVersion) {
        return fail(tr("The file was saved by a newer QindaDiagram (format %1); this version reads format %2.")
                        .arg(version)
                        .arg(kFormatVersion));
    }
    Drawing out;
    out.title = root.value(QLatin1String("title")).toString();
    out.themeId = root.value(QLatin1String("theme")).toString(QStringLiteral("qinda"));
    out.serial = std::max(0, root.value(QLatin1String("serial")).toInt(0));
    for (const QJsonValue &v : root.value(QLatin1String("pages")).toArray()) {
        out.pages.append(pageFromJson(v.toObject()));
    }
    FileM87::drawingFromJson(root, out);
    out.extra = unknown(root, {"format", "version", "title", "theme", "serial", "pages"});
    FileM87::stripDrawingKeys(out.extra);
    if (out.pages.isEmpty()) {
        return fail(tr("The drawing has no pages."));
    }
    *drawing = out;
    return true;
}

bool saveDrawing(const QString &path, const Drawing &drawing, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = tr("Could not write %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    file.write(drawingToJson(drawing));
    if (!file.commit()) {
        if (error != nullptr) {
            *error = tr("Could not save %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    return true;
}

bool loadDrawing(const QString &path, Drawing *drawing, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error != nullptr) {
            *error = tr("Could not open %1: %2").arg(path, file.errorString());
        }
        return false;
    }
    return drawingFromJson(file.readAll(), drawing, error);
}

QByteArray fragmentOf(const Page &page, const QStringList &ids)
{
    QJsonArray shapes;
    QJsonArray connectors;
    for (const Shape &s : page.shapes) {
        if (ids.contains(s.id)) {
            shapes.append(shapeToJson(s));
        }
    }
    for (const Connector &c : page.connectors) {
        const bool between = c.from.glued() && c.to.glued() && ids.contains(c.from.shapeId) && ids.contains(c.to.shapeId);
        if (ids.contains(c.id) || between) {
            // Glued ends carry where they are now, so a copy pasted without
            // its shapes (another window, another page) frees them in place.
            Connector copy = c;
            const Route route = routeConnector(page, c);
            copy.from.point = route.start();
            copy.to.point = route.end();
            connectors.append(connectorToJson(copy));
        }
    }
    const QJsonObject root{{QStringLiteral("format"), QStringLiteral("qindadiagram-fragment")},
                           {QStringLiteral("version"), kFormatVersion},
                           {QStringLiteral("shapes"), shapes},
                           {QStringLiteral("connectors"), connectors}};
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool parseFragment(const QByteArray &bytes, QVector<Shape> *shapes, QVector<Connector> *connectors, QString *error)
{
    const QJsonDocument document = QJsonDocument::fromJson(bytes);
    const QJsonObject root = document.object();
    if (root.value(QLatin1String("format")).toString() != QLatin1String("qindadiagram-fragment")) {
        if (error != nullptr) {
            *error = tr("The clipboard holds no diagram shapes.");
        }
        return false;
    }
    shapes->clear();
    connectors->clear();
    for (const QJsonValue &v : root.value(QLatin1String("shapes")).toArray()) {
        shapes->append(shapeFromJson(v.toObject()));
    }
    for (const QJsonValue &v : root.value(QLatin1String("connectors")).toArray()) {
        connectors->append(connectorFromJson(v.toObject()));
    }
    return !shapes->isEmpty() || !connectors->isEmpty();
}

} // namespace QindaOffice::Diagram
