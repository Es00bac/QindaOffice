// SPDX-License-Identifier: GPL-3.0-or-later
#include "shape_json.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace QindaOffice::Show {

namespace {

QJsonObject colorToJson(const QColor &color)
{
    QJsonObject object;
    object.insert(QStringLiteral("r"), color.red());
    object.insert(QStringLiteral("g"), color.green());
    object.insert(QStringLiteral("b"), color.blue());
    object.insert(QStringLiteral("a"), color.alpha());
    return object;
}

QColor colorFromJson(const QJsonObject &object)
{
    return QColor(object.value(QStringLiteral("r")).toInt(),
                  object.value(QStringLiteral("g")).toInt(),
                  object.value(QStringLiteral("b")).toInt(),
                  object.value(QStringLiteral("a")).toInt(255));
}

QJsonObject runToJson(const TextRun &run)
{
    QJsonObject object;
    object.insert(QStringLiteral("text"), run.text);
    object.insert(QStringLiteral("bold"), run.bold);
    object.insert(QStringLiteral("italic"), run.italic);
    object.insert(QStringLiteral("color"), colorToJson(run.color));
    object.insert(QStringLiteral("pointSize"), run.pointSize);
    object.insert(QStringLiteral("fontFamily"), run.fontFamily);
    return object;
}

TextRun runFromJson(const QJsonObject &object)
{
    TextRun run;
    run.text = object.value(QStringLiteral("text")).toString();
    run.bold = object.value(QStringLiteral("bold")).toBool();
    run.italic = object.value(QStringLiteral("italic")).toBool();
    run.color = colorFromJson(object.value(QStringLiteral("color")).toObject());
    run.pointSize = object.value(QStringLiteral("pointSize")).toInt(18);
    run.fontFamily = object.value(QStringLiteral("fontFamily")).toString();
    return run;
}

QJsonObject paragraphToJson(const TextParagraph &paragraph)
{
    QJsonObject object;
    QJsonArray runs;
    for (const TextRun &run : paragraph.runs) {
        runs.append(runToJson(run));
    }
    object.insert(QStringLiteral("runs"), runs);
    object.insert(QStringLiteral("alignment"), paragraph.alignment);
    object.insert(QStringLiteral("bullet"), paragraph.bullet);
    object.insert(QStringLiteral("listLevel"), paragraph.listLevel);
    return object;
}

TextParagraph paragraphFromJson(const QJsonObject &object)
{
    TextParagraph paragraph;
    const QJsonArray runs = object.value(QStringLiteral("runs")).toArray();
    for (const QJsonValue &value : runs) {
        paragraph.runs.append(runFromJson(value.toObject()));
    }
    paragraph.alignment = object.value(QStringLiteral("alignment")).toInt();
    paragraph.bullet = object.value(QStringLiteral("bullet")).toBool();
    paragraph.listLevel = object.value(QStringLiteral("listLevel")).toInt();
    return paragraph;
}

} // namespace

QJsonObject shapeToJson(const Shape &shape)
{
    QJsonObject object;
    object.insert(QStringLiteral("kind"), int(shape.kind));
    object.insert(QStringLiteral("rect"),
                  QJsonArray{shape.rect.x(), shape.rect.y(), shape.rect.width(),
                             shape.rect.height()});
    object.insert(QStringLiteral("rotation"), shape.rotation);
    object.insert(QStringLiteral("fill"), colorToJson(shape.fill));
    object.insert(QStringLiteral("stroke"), colorToJson(shape.stroke));
    object.insert(QStringLiteral("strokeWidth"), shape.strokeWidth);
    QJsonArray paragraphs;
    for (const TextParagraph &paragraph : shape.paragraphs) {
        paragraphs.append(paragraphToJson(paragraph));
    }
    object.insert(QStringLiteral("paragraphs"), paragraphs);
    if (!shape.imagePng.isEmpty()) {
        object.insert(QStringLiteral("imagePng"),
                      QString::fromLatin1(shape.imagePng.toBase64()));
    }
    object.insert(QStringLiteral("buildStep"), shape.buildStep);
    object.insert(QStringLiteral("extra"), QJsonObject::fromVariantMap(shape.extra));
    // The id is metadata for the editor, not content; consumers assign a
    // fresh id on paste, so it is intentionally not encoded.
    return object;
}

Shape shapeFromJson(const QJsonObject &object)
{
    Shape shape;
    shape.kind = ShapeKind(object.value(QStringLiteral("kind")).toInt(int(ShapeKind::TextBox)));
    const QJsonArray rect = object.value(QStringLiteral("rect")).toArray();
    if (rect.size() == 4) {
        shape.rect = QRectF(rect[0].toDouble(), rect[1].toDouble(), rect[2].toDouble(),
                            rect[3].toDouble());
    }
    shape.rotation = object.value(QStringLiteral("rotation")).toDouble();
    shape.fill = colorFromJson(object.value(QStringLiteral("fill")).toObject());
    shape.stroke = colorFromJson(object.value(QStringLiteral("stroke")).toObject());
    shape.strokeWidth = object.value(QStringLiteral("strokeWidth")).toDouble(1.0);
    const QJsonArray paragraphs = object.value(QStringLiteral("paragraphs")).toArray();
    for (const QJsonValue &value : paragraphs) {
        shape.paragraphs.append(paragraphFromJson(value.toObject()));
    }
    shape.imagePng =
        QByteArray::fromBase64(object.value(QStringLiteral("imagePng")).toString().toLatin1());
    shape.buildStep = object.value(QStringLiteral("buildStep")).toInt();
    shape.extra = object.value(QStringLiteral("extra")).toObject().toVariantMap();
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
    root.insert(QStringLiteral("v"), 1);
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
