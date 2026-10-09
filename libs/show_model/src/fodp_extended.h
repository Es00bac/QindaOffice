// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What .fodp carries of the M62 model beyond its ODF subset (D-070).
// ODF spells QindaShow's own shapes; a shape with anything more (an Office
// geometry, a gradient, an outline style, text-body or paragraph
// properties, a table, a chart, a group, preserved Office content) also
// gets an exact copy in qq:shape — the shape clipboard's JSON
// (shape_json.h) — which the reader prefers, while LibreOffice ignores it
// and shows the ODF approximation (a picture of tables, charts, groups and
// preserved content, rendered by QindaShow).
//
// The deck-level M62 model rides qq: attributes beside it: slide
// backgrounds and visibility, master decorations and background fills,
// Office slide layouts, the notes page size.
//
// AGENT-NOTE: what the clipboard JSON leaves out stays out of .fodp too:
// the Office identity of a shape (its Office id, placeholder, connections)
// and provenance. The look survives (the reader resolved every value into
// the shape); the PowerPoint structure does not. .pptx is the format that
// carries everything (and the crash journal).

#include "presentation.h"

#include <optional>

namespace QindaOffice::Show::Fodp {

[[nodiscard]] bool needsExtendedCopy(const Shape &shape);
[[nodiscard]] QString extendedCopy(const Shape &shape);
[[nodiscard]] std::optional<Shape> fromExtendedCopy(const QString &json, const QString &id);

// The ODF predefined shape (draw:type) LibreOffice draws for `shape`: its
// Office preset's twin where LibreOffice has one, else a rectangle; and
// the preset an ODF type reads back as (empty: the reader's own kinds).
[[nodiscard]] QString odfShapeType(const Shape &shape);
[[nodiscard]] QString presetForOdfType(const QString &type);

// A PNG of what QindaShow draws for `shape` (unrotated, at 2x).
[[nodiscard]] QByteArray shapePicture(const Shape &shape);

// Slide backgrounds, master decorations and layouts as JSON attributes
// (shapes keep their ids, unlike a paste).
[[nodiscard]] QString backgroundJson(const Background &background);
[[nodiscard]] Background backgroundFromJson(const QString &json);
[[nodiscard]] QString shapesJson(const QList<Shape> &shapes);
[[nodiscard]] QList<Shape> shapesFromJson(const QString &json);
[[nodiscard]] QString layoutsJson(const QList<SlideLayout> &layouts);
[[nodiscard]] QList<SlideLayout> layoutsFromJson(const QString &json);

// ---- M75 (D-084): what the Slide Master view, sections and comments add ----
// A master's or layout's placeholders (their placeholder identity kept).
[[nodiscard]] QString placeholdersJson(const QList<Shape> &shapes);
[[nodiscard]] QList<Shape> placeholdersFromJson(const QString &json);
// fodp_design.cpp: a master's theme and text styles, the deck's sections,
// a slide's comments.
[[nodiscard]] QString themeJson(const ThemeSpec &theme);
[[nodiscard]] ThemeSpec themeFromJson(const QString &json);
[[nodiscard]] QString textStylesJson(const SlideMaster &master);
void textStylesFromJson(const QString &json, SlideMaster &master);
[[nodiscard]] QString sectionsJson(const QList<SlideSection> &sections);
[[nodiscard]] QList<SlideSection> sectionsFromJson(const QString &json);
[[nodiscard]] QString commentsJson(const QList<SlideComment> &comments);
[[nodiscard]] QList<SlideComment> commentsFromJson(const QString &json);

} // namespace QindaOffice::Show::Fodp
