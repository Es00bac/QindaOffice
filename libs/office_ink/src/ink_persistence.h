// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// InkRenderer + persistence glue (plan sections 6.4/7.4): painting strokes
// with pressure width (highlighter under content), plus the page JSON
// envelope for notebooks.

#include "stroke_model.h"

class QPainter;

namespace QindaOffice::Ink {

// Paints one stroke; clips to `clip` when non-empty.
void renderStroke(QPainter *painter, const Stroke &stroke, const QRectF &clip = {});

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
