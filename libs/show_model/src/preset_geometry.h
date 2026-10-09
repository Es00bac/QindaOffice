// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// DrawingML shape outlines for the renderer (M62, D-070): the preset
// geometries Office decks actually use (ECMA-376 Part 1 §20.1.10.56 names,
// adjust values as in a:avLst), and a:custGeom paths with their guide
// formulas. A preset QindaShow does not know yields an empty path and the
// caller draws the bounding rectangle — the preset NAME is kept in the
// model either way, so nothing is lost on save.

#include <QPainterPath>
#include <QPair>
#include <QRectF>
#include <QString>

namespace QindaOffice::Show::Geometry {

using Adjustments = QList<QPair<QString, qint64>>;

struct Outline {
    QPainterPath fill;   // the filled area (empty for stroke-only presets)
    QPainterPath stroke; // what the outline pen draws
    [[nodiscard]] bool isEmpty() const { return fill.isEmpty() && stroke.isEmpty(); }
};

// The outline of preset `name` inside `rect`. Empty when unknown.
[[nodiscard]] Outline presetOutline(const QString &name, const QRectF &rect, const Adjustments &adjust = {});

// An a:custGeom fragment (Ooxml::serializeFragment) drawn into `rect`.
[[nodiscard]] Outline customOutline(const QString &fragment, const QRectF &rect);

// Where a preset puts its text (an ellipse's inscribed box, …); `rect`
// itself for most shapes.
[[nodiscard]] QRectF textRect(const QString &name, const QRectF &rect, const Adjustments &adjust = {});

// True for the line-like presets (line, connectors, arcs, brackets): they
// are stroked, never filled.
[[nodiscard]] bool isOpenPreset(const QString &name);

} // namespace QindaOffice::Show::Geometry
