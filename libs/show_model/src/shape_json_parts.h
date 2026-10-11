// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The pieces of the shape clipboard codec (shape_json.h) below the shape:
// colours, text, fills, outlines, text bodies, pictures, tables and charts.
// Internal to show_model; every encoder has its decoder beside it, and a
// field one side skips is a field a paste loses.

#include "presentation.h"

#include <QJsonArray>
#include <QJsonObject>

namespace QindaOffice::Show::Json {

[[nodiscard]] QJsonValue color(const QColor &color);
[[nodiscard]] QColor color(const QJsonValue &value, const QColor &fallback = QColor());

[[nodiscard]] QJsonArray paragraphs(const QList<TextParagraph> &paragraphs);
[[nodiscard]] QList<TextParagraph> paragraphs(const QJsonArray &array);

[[nodiscard]] QJsonObject fillStyle(const FillStyle &style);
[[nodiscard]] FillStyle fillStyle(const QJsonObject &object);
[[nodiscard]] QJsonObject lineStyle(const LineStyle &style);
[[nodiscard]] LineStyle lineStyle(const QJsonObject &object);
[[nodiscard]] QJsonObject textBody(const TextBodyStyle &style);
[[nodiscard]] TextBodyStyle textBody(const QJsonObject &object);
[[nodiscard]] QJsonObject table(const Table &table);
[[nodiscard]] Table table(const QJsonObject &object);
[[nodiscard]] QJsonObject chart(const ChartData &chart);
[[nodiscard]] ChartData chart(const QJsonObject &object);
// M75: a video's or sound's file and playback.
[[nodiscard]] QJsonObject media(const MediaInfo &media);
[[nodiscard]] MediaInfo media(const QJsonObject &object);
// M91 (D-099, shape_json_actions.cpp): action settings, exit effects, and a
// medium's trim, fades and bookmarks (added to / read from its object).
[[nodiscard]] QJsonObject action(const ShowAction &action);
[[nodiscard]] ShowAction action(const QJsonObject &object);
[[nodiscard]] QJsonObject exit(const ShapeExit &exit);
[[nodiscard]] ShapeExit exit(const QJsonObject &object);
void mediaEdits(QJsonObject &into, const MediaInfo &media);
void mediaEdits(const QJsonObject &from, MediaInfo &media);

} // namespace QindaOffice::Show::Json
