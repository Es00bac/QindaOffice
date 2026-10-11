// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The OpenType math font an equation is set in (M92, D-100).
//
// Word asks for "Cambria Math" (m:mathFont, the run fonts). open() takes
// the requested family when it is installed AND carries a MATH table, else
// the first installed of the OpenType math fonts the suite maps it to —
// "DejaVu Math TeX Gyre" (media-fonts/dejavu, on both machines), then
// "Noto Sans Math", then other common math fonts. A character the math font
// lacks comes from a fallback font (Noto Sans Math, DejaVu Sans, …); only
// math-font glyphs take part in MATH-table constructions.
//
// Glyph metrics are unhinted design metrics scaled to the pixel size, with
// y growing DOWN (Qt's convention): a glyph's ascent is -bounds.top().
//
// AGENT-GUARD: needs a QGuiApplication (QRawFont::fromFont). Opened fonts
// and their per-size QRawFonts are cached per thread. MathFont/GlyphInfo
// and StretchedGlyph stay on their creating thread (Qt QRawFont contract).
// EquationLayout freezes glyph outlines and can be painted on another thread.
// Never hand out a cached QRawFont for mutation.

#include "math_table.h"

#include <QList>
#include <QPointF>
#include <QRawFont>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

namespace QindaOffice::Math {

struct GlyphInfo {
    QRawFont font;
    quint32 glyph = 0;
    qreal advance = 0;
    QRectF bounds; // ink, relative to the glyph origin, y down
    qreal italic = 0;
    std::optional<qreal> topAccent; // MATH top accent attachment, x from the origin
    bool fromMathFont = false;
    bool missing = false; // no installed font has the character (.notdef drawn)
};

// A glyph or an assembly of glyphs that one box draws.
struct StretchedGlyph {
    QRawFont font;
    QList<quint32> glyphs;
    QList<QPointF> positions; // glyph origins relative to the box origin
    qreal width = 0;
    qreal ascent = 0;  // above the baseline
    qreal descent = 0; // below the baseline
    qreal italic = 0;
    std::optional<qreal> topAccent;
    bool assembled = false;
};

enum class StretchRule : quint8 {
    AtLeast, // the smallest variant covering the target, else an assembly, else the largest
    AtMost,  // the largest variant not exceeding the target (TeX's wide accents)
};

class MathFont {
public:
    MathFont() = default;

    [[nodiscard]] static MathFont open(const QString &requestedFamily);
    // The order open() tries: the request, then the suite's math fonts.
    [[nodiscard]] static QStringList candidateFamilies(const QString &requestedFamily);

    [[nodiscard]] bool isValid() const { return bool(d); }
    [[nodiscard]] QString family() const;
    [[nodiscard]] const MathTable &table() const;
    [[nodiscard]] qreal unitsPerEm() const;
    [[nodiscard]] qreal designToPixels(qreal units, qreal pixelSize) const;
    // A MATH constant in pixels at `pixelSize` (the percentages unscaled).
    [[nodiscard]] qreal constant(MathConstant c, qreal pixelSize) const;
    [[nodiscard]] QRawFont raw(qreal pixelSize) const;

    // The character in the math font, else in a fallback font.
    [[nodiscard]] GlyphInfo glyph(char32_t c, qreal pixelSize) const;
    [[nodiscard]] GlyphInfo glyphById(quint32 glyph, qreal pixelSize) const;
    // `base` grown along `orientation` to `target` pixels (variants, then
    // an assembly); a glyph without a construction comes back as it is.
    [[nodiscard]] StretchedGlyph stretch(const GlyphInfo &base, Qt::Orientation orientation, qreal target,
                                         qreal pixelSize, StretchRule rule = StretchRule::AtLeast) const;
    [[nodiscard]] static StretchedGlyph single(const GlyphInfo &glyph);

    // Normal text (m:nor): the character in `family` (bold/italic), else
    // through glyph().
    [[nodiscard]] GlyphInfo textGlyph(const QString &family, bool bold, bool italic, char32_t c,
                                      qreal pixelSize) const;

private:
    struct Data;
    std::shared_ptr<const Data> d;

    [[nodiscard]] GlyphInfo describe(const QRawFont &font, quint32 glyph, bool mathFont) const;
    [[nodiscard]] StretchedGlyph assemble(const GlyphConstruction &c, Qt::Orientation orientation,
                                          qreal targetUnits, qreal pixelSize) const;
};

// The font's glyph for one code point; 0 when it has none.
[[nodiscard]] quint32 glyphIndex(const QRawFont &font, char32_t c);

} // namespace QindaOffice::Math
