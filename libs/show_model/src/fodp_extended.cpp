// SPDX-License-Identifier: GPL-3.0-or-later
#include "fodp_extended.h"

#include "preset_geometry.h"
#include "render_parts.h"
#include "shape_json.h"
#include "shape_json_parts.h"

#include <QBuffer>
#include <QJsonArray>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>

namespace QindaOffice::Show::Fodp {

namespace {

bool legacyKind(ShapeKind kind)
{
    return kind == ShapeKind::TextBox || kind == ShapeKind::Rectangle || kind == ShapeKind::Ellipse
        || kind == ShapeKind::Line || kind == ShapeKind::Arrow || kind == ShapeKind::Image;
}

QString compact(const QJsonValue &value)
{
    return QString::fromUtf8(value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                                             : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

// The clipboard codec gives a pasted shape (and its members) fresh ids;
// a saved file keeps them, and the legacy grouping with them.
QJsonObject exactObject(const Shape &shape)
{
    QJsonObject object = shapeToJson(shape);
    object.insert(QStringLiteral("id"), shape.id);
    if (!shape.groupId.isEmpty()) {
        object.insert(QStringLiteral("groupId"), shape.groupId);
    }
    if (!shape.children.empty()) {
        QJsonArray children;
        for (const Shape &child : shape.children) {
            children.append(exactObject(child));
        }
        object.insert(QStringLiteral("children"), children);
    }
    return object;
}

void restoreIdentity(Shape &shape, const QJsonObject &object)
{
    shape.id = object.value(QStringLiteral("id")).toString(shape.id);
    shape.groupId = object.value(QStringLiteral("groupId")).toString();
    const QJsonArray children = object.value(QStringLiteral("children")).toArray();
    for (qsizetype i = 0; i < qsizetype(shape.children.size()) && i < children.size(); ++i) {
        restoreIdentity(shape.children[size_t(i)], children.at(i).toObject());
    }
}

QJsonArray shapesArray(const QList<Shape> &shapes)
{
    QJsonArray array;
    for (const Shape &shape : shapes) {
        array.append(exactObject(shape));
    }
    return array;
}

QList<Shape> shapesFromArray(const QJsonArray &array)
{
    QList<Shape> shapes;
    for (const QJsonValue &value : array) {
        Shape shape = shapeFromJson(value.toObject());
        restoreIdentity(shape, value.toObject());
        shapes.append(shape);
    }
    return shapes;
}

QJsonObject backgroundObject(const Background &background)
{
    return {{QStringLiteral("color"), Json::color(background.color)},
            {QStringLiteral("style"), Json::fillStyle(background.style)}};
}

Background backgroundFrom(const QJsonObject &object)
{
    Background background;
    if (object.isEmpty()) {
        return background;
    }
    background.set = true;
    background.color = Json::color(object.value(QStringLiteral("color")));
    background.style = Json::fillStyle(object.value(QStringLiteral("style")).toObject());
    return background;
}

// Office presets and the ODF predefined shapes LibreOffice draws for them.
// AGENT-NOTE: each twin was looked at in LibreOffice 25.2's PDF of a bare
// draw:type (no draw:modifiers). isosceles-triangle, right-triangle, star5,
// can and flowchart-decision drew far outside their frame that way, so
// those presets stay rectangles in the ODF view.
const QList<std::pair<QString, QString>> &twins()
{
    static const QList<std::pair<QString, QString>> list = {
        {QStringLiteral("roundRect"), QStringLiteral("round-rectangle")},
        {QStringLiteral("diamond"), QStringLiteral("diamond")},
        {QStringLiteral("parallelogram"), QStringLiteral("parallelogram")},
        {QStringLiteral("hexagon"), QStringLiteral("hexagon")},
        {QStringLiteral("octagon"), QStringLiteral("octagon")},
        {QStringLiteral("rightArrow"), QStringLiteral("right-arrow")},
        {QStringLiteral("leftArrow"), QStringLiteral("left-arrow")},
        {QStringLiteral("upArrow"), QStringLiteral("up-arrow")},
        {QStringLiteral("downArrow"), QStringLiteral("down-arrow")},
        {QStringLiteral("chevron"), QStringLiteral("chevron")},
        {QStringLiteral("homePlate"), QStringLiteral("pentagon-right")},
        {QStringLiteral("plus"), QStringLiteral("cross")},
        {QStringLiteral("cube"), QStringLiteral("cube")},
        {QStringLiteral("heart"), QStringLiteral("heart")},
        {QStringLiteral("smileyFace"), QStringLiteral("smiley")},
        {QStringLiteral("flowChartProcess"), QStringLiteral("flowchart-process")},
    };
    return list;
}

} // namespace

QString odfShapeType(const Shape &shape)
{
    if (shape.kind == ShapeKind::Ellipse || (shape.kind == ShapeKind::AutoShape && shape.geometry == u"ellipse")) {
        return QStringLiteral("ellipse");
    }
    if (shape.kind == ShapeKind::AutoShape) {
        for (const auto &[preset, odf] : twins()) {
            if (preset == shape.geometry) {
                return odf;
            }
        }
    }
    return QStringLiteral("rectangle");
}

QString presetForOdfType(const QString &type)
{
    for (const auto &[preset, odf] : twins()) {
        if (odf == type) {
            return preset;
        }
    }
    // LibreOffice spells the Office presets it imported as "ooxml-<name>".
    if (type.startsWith(QLatin1String("ooxml-"))) {
        const QString preset = type.mid(6);
        if (!Geometry::presetOutline(preset, QRectF(0, 0, 100, 100)).isEmpty()) {
            return preset;
        }
    }
    return {};
}

bool needsExtendedCopy(const Shape &shape)
{
    if (!legacyKind(shape.kind)) {
        return true;
    }
    // The shape as the ODF spelling carries it; anything else differs.
    Shape plain;
    plain.id = shape.id;
    plain.kind = shape.kind;
    plain.rect = shape.rect;
    plain.rotation = shape.rotation;
    plain.fill = shape.fill;
    plain.stroke = shape.stroke;
    plain.strokeWidth = shape.strokeWidth;
    plain.imagePng = shape.imagePng;
    plain.buildStep = shape.buildStep;
    plain.animation = shape.animation;
    plain.animationMs = shape.animationMs;
    plain.groupId = shape.groupId;
    plain.extra = shape.extra;
    for (const TextParagraph &paragraph : shape.paragraphs) {
        TextParagraph p;
        p.alignment = paragraph.alignment;
        p.bullet = paragraph.bullet;
        p.listLevel = paragraph.listLevel;
        for (const TextRun &run : paragraph.runs) {
            TextRun r;
            r.text = run.text;
            r.bold = run.bold;
            r.italic = run.italic;
            r.color = run.color;
            r.pointSize = run.pointSize;
            r.fontFamily = run.fontFamily;
            p.runs.append(r);
        }
        plain.paragraphs.append(p);
    }
    return !(plain == shape);
}

QString extendedCopy(const Shape &shape)
{
    return compact(exactObject(shape));
}

std::optional<Shape> fromExtendedCopy(const QString &json, const QString &id)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    Shape shape = shapeFromJson(document.object());
    restoreIdentity(shape, document.object());
    shape.id = id;
    return shape;
}

QString shapesJson(const QList<Shape> &shapes)
{
    return compact(shapesArray(shapes));
}

QList<Shape> shapesFromJson(const QString &json)
{
    return shapesFromArray(QJsonDocument::fromJson(json.toUtf8()).array());
}

QString layoutsJson(const QList<SlideLayout> &layouts)
{
    QJsonArray array;
    for (const SlideLayout &layout : layouts) {
        QJsonObject object{{QStringLiteral("id"), layout.id},
                           {QStringLiteral("name"), layout.name},
                           {QStringLiteral("type"), layout.type},
                           {QStringLiteral("master"), layout.master},
                           {QStringLiteral("showMaster"), layout.showMasterShapes},
                           {QStringLiteral("shapes"), shapesArray(layout.shapes)}};
        if (layout.background.set) {
            object.insert(QStringLiteral("background"), backgroundObject(layout.background));
        }
        array.append(object);
    }
    return compact(array);
}

QList<SlideLayout> layoutsFromJson(const QString &json)
{
    QList<SlideLayout> layouts;
    for (const QJsonValue &value : QJsonDocument::fromJson(json.toUtf8()).array()) {
        const QJsonObject object = value.toObject();
        SlideLayout layout;
        layout.id = object.value(QStringLiteral("id")).toString();
        layout.name = object.value(QStringLiteral("name")).toString();
        layout.type = object.value(QStringLiteral("type")).toString();
        layout.master = object.value(QStringLiteral("master")).toString();
        layout.showMasterShapes = object.value(QStringLiteral("showMaster")).toBool(true);
        layout.shapes = shapesFromArray(object.value(QStringLiteral("shapes")).toArray());
        layout.background = backgroundFrom(object.value(QStringLiteral("background")).toObject());
        layouts.append(layout);
    }
    return layouts;
}

QByteArray shapePicture(const Shape &shape)
{
    const QRectF rect = shape.rect.normalized();
    if (rect.isEmpty()) {
        return {};
    }
    const qreal scale = qMin<qreal>(2.0, 4000.0 / qMax(rect.width(), rect.height()));
    QImage image(QSize(qMax(1, qRound(rect.width() * scale)), qMax(1, qRound(rect.height() * scale))),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.scale(scale, scale);
        painter.translate(-rect.topLeft());
        Shape flat = shape;
        flat.rotation = 0;
        Render::paintShape(&painter, flat);
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}

QString backgroundJson(const Background &background)
{
    return compact(backgroundObject(background));
}

Background backgroundFromJson(const QString &json)
{
    return backgroundFrom(QJsonDocument::fromJson(json.toUtf8()).object());
}

} // namespace QindaOffice::Show::Fodp
