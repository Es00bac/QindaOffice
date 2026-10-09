// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The look of a shape beyond a plain colour (M62, D-070): gradient, picture
// and pattern fills, outline dash/cap/join and arrowheads, and the text
// body (insets, vertical anchor, autofit, wrap, vertical text). Plain data;
// slide_renderer.h paints it, the format mappers read and write it.
//
// AGENT-NOTE: Shape::fill (a QColor, alpha 0 = no fill) and Shape::stroke /
// strokeWidth stay the solid colour and width every pre-M62 caller edits.
// FillStyle only takes over when its kind is not Plain, so the editor's
// "Fill Colour…" resets it to Plain (SlideCanvasItem::setSelectedFill).

#include <QByteArray>
#include <QColor>
#include <QList>
#include <QString>

namespace QindaOffice::Show {

enum class FillKind {
    Plain,    // Shape::fill: a solid colour, or none when transparent
    Gradient, // stops along an angle, or radiating from the centre
    Picture,  // an image stretched (or tiled) over the shape
    Pattern,  // a two-colour preset pattern (approximated when painted)
};

struct GradientStop {
    qreal position = 0; // 0..1
    QColor color;
    bool operator==(const GradientStop &) const = default;
};

struct FillStyle {
    FillKind kind = FillKind::Plain;
    // Gradient
    QList<GradientStop> stops;
    qreal angle = 0;  // linear: degrees, clockwise from left→right
    QString path;     // empty = linear; "circle" / "rect" / "shape" = radial
    // Picture
    QByteArray image;
    QString imageMime;
    bool tile = false;
    // Pattern (DrawingML preset name: "pct50", "dkHorz", "smGrid", …)
    QString pattern;
    QColor foreground;
    QColor background;

    bool operator==(const FillStyle &) const = default;
};

// An arrowhead (DrawingML a:headEnd / a:tailEnd).
struct LineEnd {
    QString type;   // "" = none; "triangle", "stealth", "diamond", "oval", "arrow"
    QString width;  // "sm" / "med" / "lg" ("" = med)
    QString length; // "sm" / "med" / "lg" ("" = med)
    bool operator==(const LineEnd &) const = default;
    [[nodiscard]] bool isNone() const { return type.isEmpty() || type == u"none"; }
};

struct LineStyle {
    QString dash; // "" = solid; DrawingML preset ("dash", "sysDot", "lgDashDot", …)
    QString cap;  // "" = flat; "rnd", "sq"
    QString join; // "" = round; "bevel", "miter"
    LineEnd head; // at the line's start
    LineEnd tail; // at its end (the Arrow kind's default when unset)
    bool operator==(const LineStyle &) const = default;
};

enum class TextAnchor { Top, Middle, Bottom, Justified, Distributed };
enum class AutoFit { None, Normal, Shape };

// Shape-level text layout (DrawingML a:bodyPr).
//
// AGENT-NOTE: the inset defaults are the pre-M62 renderer's (6 pt sides,
// 4 pt top/bottom), so a shape QindaShow created looks as it always did.
// The pptx reader sets DrawingML's own defaults (7.2 / 3.6 pt) when a file
// leaves them out.
struct TextBodyStyle {
    qreal insetLeft = 6;
    qreal insetTop = 4;
    qreal insetRight = 6;
    qreal insetBottom = 4;
    TextAnchor anchor = TextAnchor::Top;
    bool anchorCenter = false;     // a:bodyPr/@anchorCtr
    AutoFit autoFit = AutoFit::None;
    qreal fontScale = 100;         // percent, normAutofit
    qreal lineSpaceReduction = 0;  // percent, normAutofit
    bool wrap = true;              // false = wrap="none"
    QString vertical;              // "" = horizontal; "vert", "vert270", "eaVert", …
    bool operator==(const TextBodyStyle &) const = default;
};

} // namespace QindaOffice::Show
