// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// InkRenderer + persistence glue (plan sections 6.4/7.4): painting strokes
// with pressure width (highlighter under content), plus the page JSON
// envelope for notebooks.

#include "stroke_model.h"

#include <QSet>

class QPainter;

namespace QindaOffice::Ink {

// Paints one stroke as a filled, antialiased outline; clips to `clip` when
// non-empty. The highlighter fills at a fixed alpha as ONE path, so its
// joints cannot double-paint into blobs.
void renderStroke(QPainter *painter, const Stroke &stroke, const QRectF &clip = {});

// Paints a layer in document order within two passes — highlighters first,
// then pen strokes — so highlighting over ink keeps the pen readable, on
// screen and in print. `visible` (indices, e.g. from SpatialGrid::query)
// restricts what is drawn; nullptr draws everything.
void renderStrokes(QPainter *painter, const QVector<Stroke> &strokes,
                   const QSet<int> *visible = nullptr);

struct PageEnvelope {
    QString id;
    QString title;
    QString background = QStringLiteral("blank"); // blank|ruled|grid|dotted
    StrokeLayer ink;
    // Rich content blocks serialize through the app (text/images/checklists);
    // the envelope carries them verbatim.
    QByteArray blocksJson = "{}";
};

[[nodiscard]] QByteArray pageToJson(const PageEnvelope &page);
[[nodiscard]] bool pageFromJson(const QByteArray &json, PageEnvelope *page);

} // namespace QindaOffice::Ink
