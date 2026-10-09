// SPDX-License-Identifier: GPL-3.0-or-later
// What .fodp carries of M75's model (D-084; fodp_extended.h): a master's
// theme and text styles, the deck's sections and a slide's comments, as
// qq: JSON beside the ODF LibreOffice reads — so a deck keeps its theme,
// master styles, sections and comments through .fodp, and a later .pptx
// save writes them (pptx_write_parts.h's generated masters use the theme,
// styles and placeholders a master brings).

#include "fodp_extended.h"

#include "shape_json_parts.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaOffice::Show::Fodp {

namespace {

QString compact(const QJsonValue &value)
{
    return QString::fromUtf8(value.isArray() ? QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact)
                                             : QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

QJsonObject parsed(const QString &json)
{
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

} // namespace

QString themeJson(const ThemeSpec &theme)
{
    QJsonArray colors;
    for (const QColor &color : theme.colors.values) {
        colors.append(Json::color(color));
    }
    QJsonObject object{{QStringLiteral("name"), theme.name},
                       {QStringLiteral("colorsName"), theme.colors.name},
                       {QStringLiteral("colors"), colors},
                       {QStringLiteral("fonts"), QJsonArray{theme.fonts.name, theme.fonts.major, theme.fonts.minor}},
                       {QStringLiteral("effects"), theme.effects}};
    if (!theme.part.isEmpty()) {
        object.insert(QStringLiteral("part"), QString::fromLatin1(theme.part.toBase64()));
    }
    return compact(object);
}

ThemeSpec themeFromJson(const QString &json)
{
    const QJsonObject object = parsed(json);
    ThemeSpec theme;
    theme.name = object.value(QStringLiteral("name")).toString();
    theme.colors.name = object.value(QStringLiteral("colorsName")).toString();
    const QJsonArray colors = object.value(QStringLiteral("colors")).toArray();
    for (qsizetype i = 0; i < qsizetype(theme.colors.values.size()) && i < colors.size(); ++i) {
        theme.colors.values[size_t(i)] = Json::color(colors.at(i));
    }
    const QJsonArray fonts = object.value(QStringLiteral("fonts")).toArray();
    theme.fonts = {fonts.at(0).toString(), fonts.at(1).toString(), fonts.at(2).toString()};
    theme.effects = object.value(QStringLiteral("effects")).toString();
    theme.part = QByteArray::fromBase64(object.value(QStringLiteral("part")).toString().toLatin1());
    return theme;
}

QString textStylesJson(const SlideMaster &master)
{
    return compact(QJsonObject{{QStringLiteral("title"), Json::paragraphs(master.titleStyle)},
                               {QStringLiteral("body"), Json::paragraphs(master.bodyStyle)},
                               {QStringLiteral("other"), Json::paragraphs(master.otherStyle)}});
}

void textStylesFromJson(const QString &json, SlideMaster &master)
{
    const QJsonObject object = parsed(json);
    master.titleStyle = Json::paragraphs(object.value(QStringLiteral("title")).toArray());
    master.bodyStyle = Json::paragraphs(object.value(QStringLiteral("body")).toArray());
    master.otherStyle = Json::paragraphs(object.value(QStringLiteral("other")).toArray());
}

QString sectionsJson(const QList<SlideSection> &sections)
{
    QJsonArray array;
    for (const SlideSection &section : sections) {
        array.append(QJsonObject{{QStringLiteral("id"), section.id},
                                 {QStringLiteral("name"), section.name},
                                 {QStringLiteral("slides"), QJsonArray::fromStringList(section.slides)}});
    }
    return compact(array);
}

QList<SlideSection> sectionsFromJson(const QString &json)
{
    QList<SlideSection> sections;
    for (const QJsonValue &value : QJsonDocument::fromJson(json.toUtf8()).array()) {
        const QJsonObject object = value.toObject();
        SlideSection section;
        section.id = object.value(QStringLiteral("id")).toString();
        section.name = object.value(QStringLiteral("name")).toString();
        for (const QJsonValue &slide : object.value(QStringLiteral("slides")).toArray()) {
            section.slides.append(slide.toString());
        }
        sections.append(section);
    }
    return sections;
}

QString commentsJson(const QList<SlideComment> &comments)
{
    QJsonArray array;
    for (const SlideComment &c : comments) {
        array.append(QJsonObject{{QStringLiteral("id"), c.id},
                                 {QStringLiteral("parent"), c.parentId},
                                 {QStringLiteral("author"), c.author},
                                 {QStringLiteral("initials"), c.initials},
                                 {QStringLiteral("authorId"), c.authorId},
                                 {QStringLiteral("created"), c.created.toUTC().toString(Qt::ISODateWithMs)},
                                 {QStringLiteral("text"), c.text},
                                 {QStringLiteral("resolved"), c.resolved},
                                 {QStringLiteral("shape"), c.shapeId},
                                 {QStringLiteral("position"), QJsonArray{c.position.x(), c.position.y()}}});
    }
    return compact(array);
}

QList<SlideComment> commentsFromJson(const QString &json)
{
    QList<SlideComment> comments;
    for (const QJsonValue &value : QJsonDocument::fromJson(json.toUtf8()).array()) {
        const QJsonObject o = value.toObject();
        SlideComment c;
        c.id = o.value(QStringLiteral("id")).toString();
        c.parentId = o.value(QStringLiteral("parent")).toString();
        c.author = o.value(QStringLiteral("author")).toString();
        c.initials = o.value(QStringLiteral("initials")).toString();
        c.authorId = o.value(QStringLiteral("authorId")).toString();
        c.created = QDateTime::fromString(o.value(QStringLiteral("created")).toString(), Qt::ISODateWithMs).toUTC();
        c.text = o.value(QStringLiteral("text")).toString();
        c.resolved = o.value(QStringLiteral("resolved")).toBool();
        c.shapeId = o.value(QStringLiteral("shape")).toString();
        const QJsonArray position = o.value(QStringLiteral("position")).toArray();
        c.position = QPointF(position.at(0).toDouble(), position.at(1).toDouble());
        comments.append(c);
    }
    return comments;
}

} // namespace QindaOffice::Show::Fodp
