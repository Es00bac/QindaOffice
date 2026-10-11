// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Equation layout and drawing (M92, D-100): TeX-quality layout driven by
// the math font's OpenType MATH table (math_font.h) — fraction rules and
// gaps, script shifts, radical degrees, stretchy delimiters and display
// n-ary operators, accents and group characters, matrix and equation-array
// alignment.
//
// AGENT-CONTRACT (office_math ⇄ QindaWrite, QindaShow): the host gives the
// surrounding run's size and colour and whether the equation is inline or
// display; EquationLayout returns a box whose ORIGIN is the left end of
// its baseline, with ascent/descent for the host's line (an inline
// equation sits on the text baseline), paint() at any origin, and caret
// geometry for an editor. renderEquation() is the same as a transparent
// high-DPI QImage plus where the baseline lies in it, for hosts that embed
// an equation as a picture. Nothing here touches the model.
//
// Threading: reentrant; needs a QGuiApplication (fonts). See math_font.h.

#include "math_box.h"
#include "math_model.h"
#include "math_position.h"
#include "math_props.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <optional>

class QPainter;

namespace QindaOffice::Math {

struct LayoutOptions {
    qreal fontSizePt = 11;       // the surrounding run's size
    qreal dpi = 96;              // logical pixels per inch of the coordinate space
    QColor color = Qt::black;    // the surrounding run's colour (paper ink)
    std::optional<bool> display; // default: the equation's own kind
    QString mathFont;            // default: settings.mathFont ("Cambria Math")
    QString textFont = QStringLiteral("Cambria"); // m:nor text without a font of its own
    MathSettings settings;       // the document's m:mathPr
    bool placeholders = true;    // empty arguments as dotted boxes (Word's on-screen look)
};

// Where the caret can stand in one argument, in the layout's coordinates.
struct SlotGeometry {
    MathPosition slot;  // offset unused
    QList<qreal> stops; // caret x for each atom offset 0…n
    qreal baseline = 0;
    qreal ascent = 0;   // caret extent above / below the baseline
    qreal descent = 0;
    QRectF rect;        // the argument's box
};

class EquationLayout {
public:
    EquationLayout() = default;
    EquationLayout(const Equation &equation, const LayoutOptions &options);

    // False when no OpenType math font is installed (nothing is drawn).
    [[nodiscard]] bool isValid() const { return m_valid; }
    [[nodiscard]] QString fontFamily() const { return m_family; }
    [[nodiscard]] qreal width() const { return m_box.width; }
    [[nodiscard]] qreal ascent() const { return m_box.ascent; }
    [[nodiscard]] qreal descent() const { return m_box.descent; }
    [[nodiscard]] const Box &box() const { return m_box; }
    // The ink, relative to the origin (glyphs may overhang the box).
    [[nodiscard]] QRectF inkBounds() const { return m_box.inkBounds(); }
    void paint(QPainter &painter, const QPointF &origin) const;

    [[nodiscard]] const QList<SlotGeometry> &slotGeometries() const { return m_slots; }
    // A zero-width rectangle where the caret stands (empty when unknown).
    [[nodiscard]] QRectF caretRect(const MathPosition &position) const;
    // The position nearest to `point` (both relative to the origin).
    [[nodiscard]] MathPosition positionAt(const QPointF &point) const;

private:
    Box m_box;
    QList<SlotGeometry> m_slots;
    QString m_family;
    bool m_valid = false;
};

struct RenderedEquation {
    QImage image;   // transparent, devicePixelRatio set
    QPointF origin; // where the box origin (baseline, left) lies, in logical pixels
    qreal width = 0;
    qreal ascent = 0;
    qreal descent = 0;
};

// Lays out and draws `equation` into an image `devicePixelRatio` times the
// logical size, large enough for every glyph's ink.
[[nodiscard]] RenderedEquation renderEquation(const Equation &equation, const LayoutOptions &options,
                                              qreal devicePixelRatio = 2.0);

} // namespace QindaOffice::Math
