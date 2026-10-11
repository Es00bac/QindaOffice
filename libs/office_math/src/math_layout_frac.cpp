// SPDX-License-Identifier: GPL-3.0-or-later
// Fractions, radicals, phantoms, boxes, bars and accents.
#include "math_layout_p.h"

namespace QindaOffice::Math {

namespace {

// Accents that stretch to the base's width (lines and arrows); the others
// (hat, tilde, check, breve, …) take the widest variant that fits, as
// TeX's \widehat does.
bool stretchesToWidth(char32_t c)
{
    return c == 0x0305 || c == 0x0304 || c == 0x033F || c == 0x00AF || (c >= 0x20D0 && c <= 0x20D1)
        || (c >= 0x20D6 && c <= 0x20D7) || c == 0x20E1 || (c >= 0x20EC && c <= 0x20EF);
}

char32_t firstCodePoint(const QString &s)
{
    const QList<uint> chars = s.toUcs4();
    return chars.isEmpty() ? 0 : char32_t(chars.first());
}

} // namespace

// TeX rules 15a–15e (MathML Core 3.3.2): numerator and denominator shifted
// from the math axis by the MATH constants, with the minimum gaps to the
// rule; m:noBar uses the stack constants, m:lin sets a/b on the baseline
// and m:skw sets the numerator up and the denominator down around a slash.
Box Layouter::fraction(const Node &n, Style st, const SlotPath *path, int item)
{
    const FractionType type = fractionType(n);
    const QColor color = structureColor(n);
    if (type == FractionType::Linear) {
        Box num = argBox(n, 0, st, path, item);
        Box den = argBox(n, 1, st, path, item);
        num.absorbItalic();
        den.absorbItalic();
        Box slash = charBox(U'/', st, color);
        Box out = Box::group();
        const qreal slashX = num.width;
        const qreal denX = slashX + slash.width;
        const qreal end = denX + den.width;
        out.add(std::move(num), QPointF(0, 0));
        out.add(std::move(slash), QPointF(slashX, 0));
        out.add(std::move(den), QPointF(denX, 0));
        out.fitChildren();
        out.width = end;
        return out;
    }
    Style base = st;
    if (m_options.settings.smallFraction) {
        base.display = false; // m:smallFrac
    }
    Box num = argBox(n, 0, base.numerator(), path, item);
    Box den = argBox(n, 1, base.denominator(), path, item);
    num.absorbItalic();
    den.absorbItalic();
    const qreal axis = this->axis(st);
    Box out = Box::group();
    if (type == FractionType::Skewed) {
        const qreal hGap = constant(MathConstant::SkewedFractionHorizontalGap, st);
        const qreal vGap = constant(MathConstant::SkewedFractionVerticalGap, st);
        const qreal up = axis + vGap / 2 + num.descent;
        const qreal down = den.ascent - axis + vGap / 2;
        const qreal px = pixelSize(st);
        const StretchedGlyph s = m_font.stretch(m_font.glyph(U'/', px), Qt::Vertical,
                                                up + num.ascent + down + den.descent, px);
        Box slash = stretchedBox(s, color);
        const qreal raise = axis - (s.ascent - s.descent) / 2;
        const qreal denX = num.width + hGap;
        const qreal end = denX + den.width;
        out.add(std::move(slash), QPointF(num.width + hGap / 2 - s.width / 2, -raise));
        out.add(std::move(num), QPointF(0, -up));
        out.add(std::move(den), QPointF(denX, down));
        out.fitChildren();
        out.width = end;
        return out;
    }
    const bool display = base.display;
    // The rule passes the wider part a little and the fraction keeps a
    // little air on both sides (TeX's null delimiters), as in Word.
    const qreal overhang = 0.06 * em(st);
    const qreal side = 0.06 * em(st);
    const qreal inner = qMax(num.width, den.width) + 2 * overhang;
    qreal up = 0;
    qreal down = 0;
    if (type == FractionType::NoBar) {
        up = constant(display ? MathConstant::StackTopDisplayStyleShiftUp : MathConstant::StackTopShiftUp, st);
        down = constant(display ? MathConstant::StackBottomDisplayStyleShiftDown : MathConstant::StackBottomShiftDown,
                        st);
        const qreal minGap = constant(display ? MathConstant::StackDisplayStyleGapMin : MathConstant::StackGapMin, st);
        const qreal gap = (up - num.descent) - (den.ascent - down);
        if (gap < minGap) {
            up += (minGap - gap) / 2;
            down += (minGap - gap) / 2;
        }
    } else {
        const qreal t = constant(MathConstant::FractionRuleThickness, st);
        const qreal numGap = constant(
            display ? MathConstant::FractionNumDisplayStyleGapMin : MathConstant::FractionNumeratorGapMin, st);
        const qreal denGap = constant(
            display ? MathConstant::FractionDenomDisplayStyleGapMin : MathConstant::FractionDenominatorGapMin, st);
        up = qMax(constant(display ? MathConstant::FractionNumeratorDisplayStyleShiftUp
                                   : MathConstant::FractionNumeratorShiftUp,
                           st),
                  axis + t / 2 + numGap + num.descent);
        down = qMax(constant(display ? MathConstant::FractionDenominatorDisplayStyleShiftDown
                                     : MathConstant::FractionDenominatorShiftDown,
                             st),
                    denGap + den.ascent - axis + t / 2);
        out.add(Box::rule(QRectF(side, -(axis + t / 2), inner, t), color), QPointF(0, 0));
    }
    const qreal numX = side + (inner - num.width) / 2;
    const qreal denX = side + (inner - den.width) / 2;
    out.add(std::move(num), QPointF(numX, -up));
    out.add(std::move(den), QPointF(denX, down));
    out.fitChildren();
    out.width = inner + 2 * side;
    return out;
}

// TeX rule 11 (MathML Core 3.3.3): the surd grows to the radicand plus the
// gap and rule; extra height goes to the gap; the overbar continues the
// surd's top; the degree sits at RadicalDegreeBottomRaisePercent of the
// surd's height, kerned by RadicalKernBefore/AfterDegree.
Box Layouter::radical(const Node &n, Style st, const SlotPath *path, int item)
{
    const QColor color = structureColor(n);
    Box base = argBox(n, 1, st.crampedStyle(), path, item);
    base.absorbItalic();
    const qreal px = pixelSize(st);
    const qreal t = constant(MathConstant::RadicalRuleThickness, st);
    qreal gap = constant(st.display ? MathConstant::RadicalDisplayStyleVerticalGap : MathConstant::RadicalVerticalGap,
                         st);
    const qreal target = base.ascent + base.descent + gap + t;
    const StretchedGlyph surd = m_font.stretch(m_font.glyph(U'√', px), Qt::Vertical, target, px);
    const qreal surdHeight = surd.ascent + surd.descent;
    if (surdHeight > target) {
        gap += (surdHeight - target) / 2;
    }
    const qreal top = base.ascent + gap + t; // the rule's top, above the baseline
    const qreal surdY = surd.ascent - top;
    Box out = Box::group();
    qreal surdX = 0;
    if (!degreeHidden(n)) {
        Box degree = argBox(n, 0, Style{2, false, false}, path, item);
        degree.absorbItalic();
        const qreal before = constant(MathConstant::RadicalKernBeforeDegree, st);
        const qreal after = constant(MathConstant::RadicalKernAfterDegree, st);
        const qreal raise = constant(MathConstant::RadicalDegreeBottomRaisePercent, st) / 100.0;
        const qreal surdBottom = surdHeight - top;
        const qreal degreeY = surdBottom - raise * surdHeight - degree.descent;
        qreal degreeX = before;
        surdX = before + degree.width + after;
        if (surdX < 0) {
            degreeX -= surdX;
            surdX = 0;
        }
        out.add(std::move(degree), QPointF(degreeX, degreeY));
    }
    const qreal surdWidth = surd.width;
    const qreal baseWidth = base.width;
    out.add(stretchedBox(surd, color), QPointF(surdX, surdY));
    out.add(Box::rule(QRectF(surdX + surdWidth, -top, baseWidth, t), color), QPointF(0, 0));
    out.add(std::move(base), QPointF(surdX + surdWidth, 0));
    out.fitChildren();
    out.width = surdX + surdWidth + baseWidth;
    out.ascent = qMax(out.ascent, top + constant(MathConstant::RadicalExtraAscender, st));
    return out;
}

Box Layouter::phantom(const Node &n, Style st, const SlotPath *path, int item)
{
    const PhantomFlags flags = phantomFlags(n);
    Box content = argBox(n, 0, st, path, item);
    content.absorbItalic();
    content.hidden = !flags.show;
    Box out = Box::group();
    out.add(std::move(content), QPointF(0, 0));
    out.fitChildren();
    if (flags.zeroWidth) {
        out.width = 0;
    }
    if (flags.zeroAscent) {
        out.ascent = 0;
    }
    if (flags.zeroDescent) {
        out.descent = 0;
    }
    return out;
}

Box Layouter::boxNode(const Node &n, Style st, const SlotPath *path, int item)
{
    Box content = argBox(n, 0, st, path, item);
    if (!boxIsDifferential(n)) {
        return content;
    }
    // m:diff: a differential (ⅆx) keeps a thin space before it.
    const qreal thin = 3 * em(st) / 18.0;
    Box out = Box::group();
    const qreal italic = content.italic;
    const qreal end = thin + content.width;
    out.add(std::move(content), QPointF(thin, 0));
    out.fitChildren();
    out.width = end;
    out.italic = italic;
    return out;
}

Box Layouter::borderBox(const Node &n, Style st, const SlotPath *path, int item)
{
    const QColor color = structureColor(n);
    const BorderFlags f = borderFlags(n);
    Box content = argBox(n, 0, st, path, item);
    content.absorbItalic();
    const qreal t = constant(MathConstant::FractionRuleThickness, st);
    const qreal pad = 0.15 * em(st);
    const qreal top = content.ascent + pad + t;
    const qreal bottom = content.descent + pad + t;
    const qreal width = content.width + 2 * (pad + t);
    Box out = Box::group();
    out.add(std::move(content), QPointF(pad + t, 0));
    const auto line = [&out, &color](const QRectF &r) { out.add(Box::rule(r, color), QPointF(0, 0)); };
    if (!f.hideTop) {
        line(QRectF(0, -top, width, t));
    }
    if (!f.hideBottom) {
        line(QRectF(0, bottom - t, width, t));
    }
    if (!f.hideLeft) {
        line(QRectF(0, -top, t, top + bottom));
    }
    if (!f.hideRight) {
        line(QRectF(width - t, -top, t, top + bottom));
    }
    if (f.strikeHorizontal) {
        line(QRectF(0, -axis(st) - t / 2, width, t));
    }
    if (f.strikeVertical) {
        line(QRectF(width / 2 - t / 2, -top, t, top + bottom));
    }
    if (f.strikeUp || f.strikeDown) {
        Box stroke;
        stroke.type = Box::Type::Stroke;
        stroke.color = color;
        stroke.pen = t;
        if (f.strikeUp) {
            stroke.path.moveTo(0, bottom);
            stroke.path.lineTo(width, -top);
        }
        if (f.strikeDown) {
            stroke.path.moveTo(0, -top);
            stroke.path.lineTo(width, bottom);
        }
        out.add(std::move(stroke), QPointF(0, 0));
    }
    out.width = width;
    out.ascent = top;
    out.descent = bottom;
    return out;
}

// Over- and underbars (TeX rules 9 and 10): the rule a gap from the base,
// with the extra ascender/descender the MATH table asks for.
Box Layouter::bar(const Node &n, Style st, const SlotPath *path, int item)
{
    const QColor color = structureColor(n);
    const bool top = barPosition(n) == VerticalPosition::Top;
    Box content = argBox(n, 0, top ? st.crampedStyle() : st, path, item);
    content.absorbItalic();
    const qreal width = content.width;
    Box out = Box::group();
    if (top) {
        const qreal gap = constant(MathConstant::OverbarVerticalGap, st);
        const qreal t = constant(MathConstant::OverbarRuleThickness, st);
        const qreal extra = constant(MathConstant::OverbarExtraAscender, st);
        const qreal edge = content.ascent + gap + t;
        out.add(std::move(content), QPointF(0, 0));
        out.add(Box::rule(QRectF(0, -edge, width, t), color), QPointF(0, 0));
        out.fitChildren();
        out.ascent = qMax(out.ascent, edge + extra);
    } else {
        const qreal gap = constant(MathConstant::UnderbarVerticalGap, st);
        const qreal t = constant(MathConstant::UnderbarRuleThickness, st);
        const qreal extra = constant(MathConstant::UnderbarExtraDescender, st);
        const qreal edge = content.descent + gap;
        out.add(std::move(content), QPointF(0, 0));
        out.add(Box::rule(QRectF(0, edge, width, t), color), QPointF(0, 0));
        out.fitChildren();
        out.descent = qMax(out.descent, edge + t + extra);
    }
    out.width = width;
    return out;
}

// TeX rule 12 with OpenType's attachment points: the accent is raised by
// what the base rises above AccentBaseHeight, and its attachment point is
// put over the base's (a single glyph's MATH value, else the middle).
// A spacing character used as the accent (→, ~, ^) sits above the base.
Box Layouter::accent(const Node &n, Style st, const SlotPath *path, int item)
{
    const QColor color = structureColor(n);
    Box base = argBox(n, 0, st.crampedStyle(), path, item);
    const char32_t c = firstCodePoint(accentChar(n));
    if (c == 0) {
        return base;
    }
    const qreal px = pixelSize(st);
    const StretchedGlyph acc = m_font.stretch(m_font.glyph(c, px), Qt::Horizontal, base.width, px,
                                              stretchesToWidth(c) ? StretchRule::AtLeast : StretchRule::AtMost);
    const qreal baseAttach = base.accentX >= 0 ? base.accentX : base.width / 2;
    qreal accentAttach = acc.width / 2;
    if (acc.topAccent && !acc.assembled) {
        accentAttach = *acc.topAccent;
    } else if (!acc.assembled && !acc.glyphs.isEmpty()) {
        accentAttach = acc.font.boundingRect(acc.glyphs.first()).center().x();
    }
    qreal y = 0;
    if (isCombiningMark(c)) {
        y = -qMax<qreal>(0, base.ascent - constant(MathConstant::AccentBaseHeight, st));
    } else {
        y = -(base.ascent + constant(MathConstant::UpperLimitGapMin, st) / 2) - acc.descent;
    }
    const qreal width = base.width;
    const qreal italic = base.italic;
    Box out = Box::group();
    out.add(std::move(base), QPointF(0, 0));
    out.add(stretchedBox(acc, color), QPointF(baseAttach - accentAttach, y));
    out.fitChildren();
    out.width = width;
    out.italic = italic;
    return out;
}

} // namespace QindaOffice::Math
