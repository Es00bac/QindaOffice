// SPDX-License-Identifier: GPL-3.0-or-later
#include "ink_persistence.h"

#include "stroke_geometry.h"

#include <QJsonArray>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>

namespace QindaOffice::Ink {

namespace {

// See-through highlight under the ink (was 120 in the segment renderer,
// which stacked at every joint; one flat fill reads stronger at the same
// visual weight).
constexpr int kHighlighterAlpha = 90;

} // namespace

void renderStroke(QPainter *painter, const Stroke &stroke, const QRectF &clip)
{
    if (stroke.samples.size() < 2) {
        return;
    }
    painter->save();
    // Antialiasing is set HERE so every caller (screen view, print
    // renderer, tests) gets smooth ink without remembering to ask.
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (!clip.isEmpty()) {
        painter->setClipRect(clip, Qt::IntersectClip);
    }
    QColor color = stroke.color;
    if (stroke.tool == StrokeTool::Highlighter) {
        color.setAlpha(kHighlighterAlpha);
    }
    painter->fillPath(outline(stroke), color);
    painter->restore();
}

void renderStrokes(QPainter *painter, const QVector<Stroke> &strokes, const QSet<int> *visible)
{
    // Two passes: every highlighter under every pen stroke, whatever the
    // creation order was. AGENT-GUARD: screen AND print must use this same
    // order — the third render path is how screen/paper drift starts.
    for (int pass = 0; pass < 2; ++pass) {
        const StrokeTool wanted = pass == 0 ? StrokeTool::Highlighter : StrokeTool::Pen;
        for (int i = 0; i < strokes.size(); ++i) {
            if (visible != nullptr && !visible->contains(i)) {
                continue;
            }
            if (strokes.at(i).tool != wanted) {
                continue;
            }
            renderStroke(painter, strokes.at(i));
        }
    }
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
