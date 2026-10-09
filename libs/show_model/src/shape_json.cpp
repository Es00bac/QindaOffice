// SPDX-License-Identifier: GPL-3.0-or-later
#include "shape_json.h"

#include "shape_json_parts.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaOffice::Show {

QJsonObject shapeToJson(const Shape &shape)
{
    QJsonObject object;
    object.insert(QStringLiteral("kind"), int(shape.kind));
    object.insert(QStringLiteral("rect"),
                  QJsonArray{shape.rect.x(), shape.rect.y(), shape.rect.width(), shape.rect.height()});
    object.insert(QStringLiteral("rotation"), shape.rotation);
    object.insert(QStringLiteral("fill"), Json::color(shape.fill));
    object.insert(QStringLiteral("stroke"), Json::color(shape.stroke));
    object.insert(QStringLiteral("strokeWidth"), shape.strokeWidth);
    object.insert(QStringLiteral("paragraphs"), Json::paragraphs(shape.paragraphs));
    if (!shape.imagePng.isEmpty()) {
        object.insert(QStringLiteral("imagePng"), QString::fromLatin1(shape.imagePng.toBase64()));
    }
    object.insert(QStringLiteral("buildStep"), shape.buildStep);
    object.insert(QStringLiteral("animation"), int(shape.animation));
    object.insert(QStringLiteral("animationMs"), shape.animationMs);
    object.insert(QStringLiteral("extra"), QJsonObject::fromVariantMap(shape.extra));
    // ---- M62: everything the extended model draws ----
    object.insert(QStringLiteral("name"), shape.name);
    object.insert(QStringLiteral("description"), shape.description);
    object.insert(QStringLiteral("hidden"), shape.hidden);
    object.insert(QStringLiteral("flip"), QJsonArray{shape.flipH, shape.flipV});
    object.insert(QStringLiteral("geometry"), shape.geometry);
    QJsonArray adjust;
    for (const auto &[name, value] : shape.adjust) {
        adjust.append(QJsonArray{name, double(value)});
    }
    object.insert(QStringLiteral("adjust"), adjust);
    object.insert(QStringLiteral("customGeometry"), shape.customGeometry);
    object.insert(QStringLiteral("framed"), shape.framed);
    object.insert(QStringLiteral("fillStyle"), Json::fillStyle(shape.fillStyle));
    object.insert(QStringLiteral("line"), Json::lineStyle(shape.line));
    object.insert(QStringLiteral("textBody"), Json::textBody(shape.textBody));
    object.insert(QStringLiteral("image"),
                  QJsonObject{{QStringLiteral("mime"), shape.image.mime},
                              {QStringLiteral("crop"), QJsonArray{shape.image.cropLeft, shape.image.cropTop,
                                                                  shape.image.cropRight, shape.image.cropBottom}},
                              {QStringLiteral("pixels"), QJsonArray{shape.image.pixels.width(), shape.image.pixels.height()}}});
    if (!shape.table.isEmpty()) {
        object.insert(QStringLiteral("table"), Json::table(shape.table));
    }
    if (!shape.chart.isEmpty() || !shape.chart.type.isEmpty()) {
        object.insert(QStringLiteral("chart"), Json::chart(shape.chart));
    }
    if (!shape.children.empty()) {
        QJsonArray children;
        for (const Shape &child : shape.children) {
            children.append(shapeToJson(child));
        }
        object.insert(QStringLiteral("children"), children);
        object.insert(QStringLiteral("childRect"), QJsonArray{shape.childRect.x(), shape.childRect.y(),
                                                              shape.childRect.width(), shape.childRect.height()});
    }
    object.insert(QStringLiteral("connector"), shape.connector);
    object.insert(QStringLiteral("hyperlink"), shape.hyperlink);
    if (!shape.preserved.isEmpty()) {
        object.insert(QStringLiteral("preserved"),
                      QJsonArray{shape.preserved.fragment, shape.preserved.part, shape.preserved.label});
    }
    // The id is metadata for the editor, not content; consumers assign a
    // fresh id on paste, so it is intentionally not encoded. Likewise the
    // Office identity (shape id, placeholder, connections, provenance): a
    // pasted copy is a new shape, not the original's twin.
    return object;
}

Shape shapeFromJson(const QJsonObject &object)
{
    Shape shape;
    shape.kind = ShapeKind(qBound(0, object.value(QStringLiteral("kind")).toInt(int(ShapeKind::TextBox)),
                                  int(ShapeKind::Preserved)));
    const QJsonArray rect = object.value(QStringLiteral("rect")).toArray();
    if (rect.size() == 4) {
        shape.rect = QRectF(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(), rect[3].toDouble());
    }
    shape.rotation = object.value(QStringLiteral("rotation")).toDouble();
    shape.fill = Json::color(object.value(QStringLiteral("fill")), Qt::transparent);
    shape.stroke = Json::color(object.value(QStringLiteral("stroke")), Qt::black);
    shape.strokeWidth = object.value(QStringLiteral("strokeWidth")).toDouble(1.0);
    shape.paragraphs = Json::paragraphs(object.value(QStringLiteral("paragraphs")).toArray());
    shape.imagePng = QByteArray::fromBase64(object.value(QStringLiteral("imagePng")).toString().toLatin1());
    shape.buildStep = object.value(QStringLiteral("buildStep")).toInt();
    shape.animation = ShapeAnimation(qBound(0, object.value(QStringLiteral("animation")).toInt(),
                                            int(kLastAnimation)));
    shape.animationMs = object.value(QStringLiteral("animationMs")).toInt(350);
    shape.extra = object.value(QStringLiteral("extra")).toObject().toVariantMap();
    shape.name = object.value(QStringLiteral("name")).toString();
    shape.description = object.value(QStringLiteral("description")).toString();
    shape.hidden = object.value(QStringLiteral("hidden")).toBool();
    const QJsonArray flip = object.value(QStringLiteral("flip")).toArray();
    shape.flipH = flip.at(0).toBool();
    shape.flipV = flip.at(1).toBool();
    shape.geometry = object.value(QStringLiteral("geometry")).toString();
    for (const QJsonValue &value : object.value(QStringLiteral("adjust")).toArray()) {
        const QJsonArray pair = value.toArray();
        shape.adjust.append({pair.at(0).toString(), qint64(pair.at(1).toDouble())});
    }
    shape.customGeometry = object.value(QStringLiteral("customGeometry")).toString();
    shape.framed = object.value(QStringLiteral("framed")).toBool();
    if (object.contains(QStringLiteral("fillStyle"))) {
        shape.fillStyle = Json::fillStyle(object.value(QStringLiteral("fillStyle")).toObject());
    }
    if (object.contains(QStringLiteral("line"))) {
        shape.line = Json::lineStyle(object.value(QStringLiteral("line")).toObject());
    }
    if (object.contains(QStringLiteral("textBody"))) {
        shape.textBody = Json::textBody(object.value(QStringLiteral("textBody")).toObject());
    }
    const QJsonObject image = object.value(QStringLiteral("image")).toObject();
    shape.image.mime = image.value(QStringLiteral("mime")).toString();
    const QJsonArray crop = image.value(QStringLiteral("crop")).toArray();
    if (crop.size() == 4) {
        shape.image.cropLeft = crop.at(0).toDouble();
        shape.image.cropTop = crop.at(1).toDouble();
        shape.image.cropRight = crop.at(2).toDouble();
        shape.image.cropBottom = crop.at(3).toDouble();
    }
    const QJsonArray pixels = image.value(QStringLiteral("pixels")).toArray();
    if (pixels.size() == 2) {
        shape.image.pixels = QSize(pixels.at(0).toInt(), pixels.at(1).toInt());
    }
    if (object.contains(QStringLiteral("table"))) {
        shape.table = Json::table(object.value(QStringLiteral("table")).toObject());
    }
    if (object.contains(QStringLiteral("chart"))) {
        shape.chart = Json::chart(object.value(QStringLiteral("chart")).toObject());
    }
    for (const QJsonValue &child : object.value(QStringLiteral("children")).toArray()) {
        shape.children.push_back(shapeFromJson(child.toObject()));
    }
    const QJsonArray childRect = object.value(QStringLiteral("childRect")).toArray();
    if (childRect.size() == 4) {
        shape.childRect = QRectF(childRect[0].toDouble(), childRect[1].toDouble(), childRect[2].toDouble(),
                                 childRect[3].toDouble());
    }
    shape.connector = object.value(QStringLiteral("connector")).toBool();
    shape.hyperlink = object.value(QStringLiteral("hyperlink")).toString();
    const QJsonArray preserved = object.value(QStringLiteral("preserved")).toArray();
    if (preserved.size() == 3) {
        shape.preserved = {preserved.at(0).toString(), preserved.at(1).toString(), preserved.at(2).toString()};
    }
    shape.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return shape;
}

QByteArray shapesToJson(const QList<Shape> &shapes)
{
    QJsonArray array;
    for (const Shape &shape : shapes) {
        array.append(shapeToJson(shape));
    }
    QJsonObject root;
    root.insert(QStringLiteral("v"), 2);
    root.insert(QStringLiteral("shapes"), array);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QList<Shape> shapesFromJson(const QByteArray &payload)
{
    QList<Shape> shapes;
    const QJsonDocument document = QJsonDocument::fromJson(payload);
    const QJsonArray array = document.object().value(QStringLiteral("shapes")).toArray();
    for (const QJsonValue &value : array) {
        shapes.append(shapeFromJson(value.toObject()));
    }
    return shapes;
}

} // namespace QindaOffice::Show
