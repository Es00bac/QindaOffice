// SPDX-License-Identifier: GPL-3.0-or-later
#include "ink_persistence.h"

#include <QJsonArray>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaOffice::Ink {

void renderStroke(QPainter *painter, const Stroke &stroke, const QRectF &clip)
{
    if (stroke.samples.size() < 2) {
        return;
    }
    painter->save();
    if (!clip.isEmpty()) {
        painter->setClipRect(clip, Qt::IntersectClip);
    }
    QColor color = stroke.color;
    if (stroke.tool == StrokeTool::Highlighter) {
        color.setAlpha(120); // see-through highlight over content
    }
    QPen pen(color);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    if (stroke.tool == StrokeTool::Highlighter) {
        pen.setWidthF(stroke.width * 3.0);
    }
    for (int i = 1; i < stroke.samples.size(); ++i) {
        pen.setWidthF(stroke.widthAt(i));
        painter->setPen(pen);
        painter->drawLine(stroke.samples.at(i - 1).pos, stroke.samples.at(i).pos);
    }
    painter->restore();
}

QByteArray pageToJson(const PageEnvelope &page)
{
    QJsonObject object;
    object.insert(QStringLiteral("id"), page.id);
    object.insert(QStringLiteral("title"), page.title);
    object.insert(QStringLiteral("background"), page.background);
    const QJsonDocument inkDoc = QJsonDocument::fromJson(page.ink.toJson());
    object.insert(QStringLiteral("ink"), inkDoc.object().value(QStringLiteral("strokes")));
    const QJsonDocument blocksDoc = QJsonDocument::fromJson(page.blocksJson);
    object.insert(QStringLiteral("blocks"), blocksDoc.object());
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool pageFromJson(const QByteArray &json, PageEnvelope *page)
{
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    page->id = object.value(QStringLiteral("id")).toString();
    page->title = object.value(QStringLiteral("title")).toString();
    page->background = object.value(QStringLiteral("background")).toString(QStringLiteral("blank"));
    const QJsonObject inkRoot{{
        QStringLiteral("strokes"), object.value(QStringLiteral("ink")).toArray()}};
    page->ink.fromJson(QJsonDocument(inkRoot).toJson(QJsonDocument::Compact));
    const QJsonObject blocks = object.value(QStringLiteral("blocks")).toObject();
    page->blocksJson = QJsonDocument(blocks).toJson(QJsonDocument::Compact);
    return true;
}

} // namespace QindaOffice::Ink
