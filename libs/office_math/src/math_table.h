// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The OpenType MATH table (OpenType 1.9 "MATH — The Mathematical Typesetting
// Table"), parsed from the bytes QRawFont::fontTable("MATH") returns. It is
// what makes the layout TeX-quality without TeX: the font's own constants
// for every gap, shift and rule thickness, italic corrections and accent
// attachment points per glyph, and the larger variants and part assemblies
// that stretch delimiters, radicals, n-ary operators, accents and braces.
//
// All values are in font DESIGN units (MathFont converts to pixels), except
// the two script percentages and RadicalDegreeBottomRaisePercent.
//
// AGENT-NOTE: the parser is bounds-checked throughout and never trusts an
// offset; a malformed table yields isValid() == false (the font is then
// skipped as a math font) rather than reading out of range. Device tables
// (hinting deltas) are ignored: the layout works in unhinted design metrics.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QSet>

#include <array>
#include <optional>

namespace QindaOffice::Math {

// In MathConstants order.
enum class MathConstant : quint8 {
    ScriptPercentScaleDown,
    ScriptScriptPercentScaleDown,
    DelimitedSubFormulaMinHeight,
    DisplayOperatorMinHeight,
    MathLeading,
    AxisHeight,
    AccentBaseHeight,
    FlattenedAccentBaseHeight,
    SubscriptShiftDown,
    SubscriptTopMax,
    SubscriptBaselineDropMin,
    SuperscriptShiftUp,
    SuperscriptShiftUpCramped,
    SuperscriptBottomMin,
    SuperscriptBaselineDropMax,
    SubSuperscriptGapMin,
    SuperscriptBottomMaxWithSubscript,
    SpaceAfterScript,
    UpperLimitGapMin,
    UpperLimitBaselineRiseMin,
    LowerLimitGapMin,
    LowerLimitBaselineDropMin,
    StackTopShiftUp,
    StackTopDisplayStyleShiftUp,
    StackBottomShiftDown,
    StackBottomDisplayStyleShiftDown,
    StackGapMin,
    StackDisplayStyleGapMin,
    StretchStackTopShiftUp,
    StretchStackBottomShiftDown,
    StretchStackGapAboveMin,
    StretchStackGapBelowMin,
    FractionNumeratorShiftUp,
    FractionNumeratorDisplayStyleShiftUp,
    FractionDenominatorShiftDown,
    FractionDenominatorDisplayStyleShiftDown,
    FractionNumeratorGapMin,
    FractionNumDisplayStyleGapMin,
    FractionRuleThickness,
    FractionDenominatorGapMin,
    FractionDenomDisplayStyleGapMin,
    SkewedFractionHorizontalGap,
    SkewedFractionVerticalGap,
    OverbarVerticalGap,
    OverbarRuleThickness,
    OverbarExtraAscender,
    UnderbarVerticalGap,
    UnderbarRuleThickness,
    UnderbarExtraDescender,
    RadicalVerticalGap,
    RadicalDisplayStyleVerticalGap,
    RadicalRuleThickness,
    RadicalExtraAscender,
    RadicalKernBeforeDegree,
    RadicalKernAfterDegree,
    RadicalDegreeBottomRaisePercent,
    Count
};

struct GlyphVariant {
    quint32 glyph = 0;
    int advance = 0; // height (vertical) or width (horizontal), design units
};

struct GlyphPart {
    quint32 glyph = 0;
    int startConnector = 0;
    int endConnector = 0;
    int fullAdvance = 0;
    bool extender = false;
};

// MathGlyphConstruction: larger pre-drawn variants (the base glyph first,
// growing), and optionally an assembly — parts bottom-to-top (vertical) or
// left-to-right (horizontal), extenders repeated as needed.
struct GlyphConstruction {
    QList<GlyphVariant> variants;
    QList<GlyphPart> parts;
    int assemblyItalic = 0;
};

class MathTable {
public:
    [[nodiscard]] static MathTable parse(const QByteArray &math);

    [[nodiscard]] bool isValid() const { return m_valid; }
    [[nodiscard]] int constant(MathConstant c) const { return m_constants.at(size_t(c)); }
    [[nodiscard]] std::optional<int> italicCorrection(quint32 glyph) const;
    [[nodiscard]] std::optional<int> topAccentAttachment(quint32 glyph) const;
    [[nodiscard]] bool isExtendedShape(quint32 glyph) const { return m_extended.contains(glyph); }
    [[nodiscard]] int minConnectorOverlap() const { return m_minOverlap; }
    [[nodiscard]] const GlyphConstruction *vertical(quint32 glyph) const;
    [[nodiscard]] const GlyphConstruction *horizontal(quint32 glyph) const;
    [[nodiscard]] int verticalCount() const { return int(m_vertical.size()); }
    [[nodiscard]] int horizontalCount() const { return int(m_horizontal.size()); }

private:
    std::array<int, size_t(MathConstant::Count)> m_constants{};
    QHash<quint32, int> m_italics;
    QHash<quint32, int> m_accents;
    QSet<quint32> m_extended;
    QHash<quint32, GlyphConstruction> m_vertical;
    QHash<quint32, GlyphConstruction> m_horizontal;
    int m_minOverlap = 0;
    bool m_valid = false;
};

} // namespace QindaOffice::Math
