// SPDX-License-Identifier: GPL-3.0-or-later
// Scripts, n-ary operators, limits, group characters and functions.
#include "math_layout_p.h"

namespace QindaOffice::Math {

namespace {

char32_t firstCodePoint(const QString &s)
{
    const QList<uint> chars = s.toUcs4();
    return chars.isEmpty() ? 0 : char32_t(chars.first());
}

} // namespace

// TeX rule 18 (MathML Core 3.4.3): superscript up by the shift (cramped
// smaller), at least SuperscriptBottomMin above the baseline; subscript
// down at least SubscriptShiftDown with its top under SubscriptTopMax; a
// non-character base drags both with its own height and depth; with both,
// SubSuperscriptGapMin between them, the pair lifted toward
// SuperscriptBottomMaxWithSubscript.
Layouter::ScriptShifts Layouter::scriptShifts(const Box &base, bool simpleBase, const Box *sup, const Box *sub,
                                              Style st) const
{
    ScriptShifts s;
    if (sup != nullptr) {
        s.up = constant(st.cramped ? MathConstant::SuperscriptShiftUpCramped : MathConstant::SuperscriptShiftUp, st);
        if (!simpleBase) {
            s.up = qMax(s.up, base.ascent - constant(MathConstant::SuperscriptBaselineDropMax, st));
        }
        s.up = qMax(s.up, constant(MathConstant::SuperscriptBottomMin, st) + sup->descent);
    }
    if (sub != nullptr) {
        s.down = constant(MathConstant::SubscriptShiftDown, st);
        if (!simpleBase) {
            s.down = qMax(s.down, base.descent + constant(MathConstant::SubscriptBaselineDropMin, st));
        }
        s.down = qMax(s.down, sub->ascent - constant(MathConstant::SubscriptTopMax, st));
    }
    if (sup != nullptr && sub != nullptr) {
        const qreal minGap = constant(MathConstant::SubSuperscriptGapMin, st);
        const qreal gap = (s.up - sup->descent) - (sub->ascent - s.down);
        if (gap < minGap) {
            s.down += minGap - gap;
            const qreal lift =
                constant(MathConstant::SuperscriptBottomMaxWithSubscript, st) - (s.up - sup->descent);
            if (lift > 0) {
                s.up += lift;
                s.down -= lift;
            }
        }
    }
    return s;
}

Box Layouter::scripts(const Node &n, Style st, const SlotPath *path, int item)
{
    int baseIndex = 0;
    int subIndex = -1;
    int supIndex = -1;
    switch (n.kind) {
    case NodeKind::Superscript:
        supIndex = 1;
        break;
    case NodeKind::Subscript:
        subIndex = 1;
        break;
    case NodeKind::SubSup:
        subIndex = 1;
        supIndex = 2;
        break;
    default: // m:sPre: sub, sup, e
        subIndex = 0;
        supIndex = 1;
        baseIndex = 2;
        break;
    }
    Box base = argBox(n, baseIndex, st, path, item);
    const bool simple = baseIndex < n.args.size() && isSingleChar(n.args.at(baseIndex));
    Box sup;
    Box sub;
    if (supIndex >= 0) {
        sup = argBox(n, supIndex, st.superscript(), path, item);
        sup.absorbItalic();
    }
    if (subIndex >= 0) {
        sub = argBox(n, subIndex, st.subscript(), path, item);
        sub.absorbItalic();
    }
    const ScriptShifts shift =
        scriptShifts(base, simple, supIndex >= 0 ? &sup : nullptr, subIndex >= 0 ? &sub : nullptr, st);
    const qreal after = constant(MathConstant::SpaceAfterScript, st);
    Box out = Box::group();
    qreal width = 0;
    if (n.kind == NodeKind::PreScript) {
        const qreal scriptsWidth = qMax(sup.width, sub.width);
        const qreal italic = base.italic;
        width = scriptsWidth + after + base.width;
        out.add(std::move(sub), QPointF(scriptsWidth - sub.width, shift.down));
        out.add(std::move(sup), QPointF(scriptsWidth - sup.width, -shift.up));
        out.add(std::move(base), QPointF(scriptsWidth + after, 0));
        out.fitChildren();
        out.width = width;
        out.italic = italic;
        return out;
    }
    const qreal supX = base.width + (alignedScripts(n) ? 0 : base.italic);
    const qreal subX = base.width;
    width = qMax(base.width + base.italic, qMax(supIndex >= 0 ? supX + sup.width : 0,
                                                subIndex >= 0 ? subX + sub.width : 0)) + after;
    out.add(std::move(base), QPointF(0, 0));
    if (supIndex >= 0) {
        out.add(std::move(sup), QPointF(supX, -shift.up));
    }
    if (subIndex >= 0) {
        out.add(std::move(sub), QPointF(subX, shift.down));
    }
    out.fitChildren();
    out.width = width;
    return out;
}

// TeX rules 13 and 13a (MathML Core 3.4.4): the operator centred on the
// axis, display size (DisplayOperatorMinHeight) in display style or as
// tall as its argument with m:grow; limits under/over with the limit gaps
// in display style, else as scripts carrying the operator's italic
// correction; a thin space before the argument.
Box Layouter::nary(const Node &n, Style st, const SlotPath *path, int item)
{
    const QColor color = structureColor(n);
    const char32_t op = firstCodePoint(naryChar(n));
    LimitLocation location = limitLocation(n);
    if (location == LimitLocation::Default) {
        location = isIntegral(op) ? m_options.settings.integralLimits : m_options.settings.naryLimits;
    }
    const bool underOver = location == LimitLocation::UnderOver && st.display;
    Box arg = argBox(n, 2, st, path, item);
    const qreal px = pixelSize(st);
    const GlyphInfo glyph = m_font.glyph(op != 0 ? op : char32_t(0x222B), px);
    qreal target = st.display ? constant(MathConstant::DisplayOperatorMinHeight, st) : 0;
    if (naryGrows(n)) {
        target = qMax(target, arg.ascent + arg.descent);
    }
    const StretchedGlyph s = target > 0 ? m_font.stretch(glyph, Qt::Vertical, target, px) : MathFont::single(glyph);
    Box opBox = Box::group();
    opBox.add(stretchedBox(s, color), QPointF(0, -(axis(st) - (s.ascent - s.descent) / 2)));
    opBox.fitChildren();
    opBox.width = s.width;
    const qreal italic = s.italic;
    const bool hasSup = !supHidden(n);
    const bool hasSub = !subHidden(n);
    Box sup;
    Box sub;
    if (hasSup) {
        sup = argBox(n, 1, st.superscript(), path, item);
        sup.absorbItalic();
    }
    if (hasSub) {
        sub = argBox(n, 0, st.subscript(), path, item);
        sub.absorbItalic();
    }
    Box limits = Box::group();
    const qreal opWidth = opBox.width;
    const qreal opAscent = opBox.ascent;
    const qreal opDescent = opBox.descent;
    if (underOver) {
        const qreal width = qMax(opWidth, qMax(hasSup ? sup.width : 0, hasSub ? sub.width : 0));
        if (hasSup) {
            const qreal rise = qMax(constant(MathConstant::UpperLimitGapMin, st) + sup.descent,
                                    constant(MathConstant::UpperLimitBaselineRiseMin, st));
            const qreal x = (width - sup.width) / 2 + italic / 2;
            limits.add(std::move(sup), QPointF(x, -(opAscent + rise)));
        }
        if (hasSub) {
            const qreal drop = qMax(constant(MathConstant::LowerLimitGapMin, st) + sub.ascent,
                                    constant(MathConstant::LowerLimitBaselineDropMin, st));
            const qreal x = (width - sub.width) / 2 - italic / 2;
            limits.add(std::move(sub), QPointF(x, opDescent + drop));
        }
        limits.add(std::move(opBox), QPointF((width - opWidth) / 2, 0));
        limits.fitChildren();
        limits.width = width;
    } else {
        const ScriptShifts shift = scriptShifts(opBox, false, hasSup ? &sup : nullptr, hasSub ? &sub : nullptr, st);
        const qreal supX = opWidth + italic;
        const qreal subX = opWidth;
        qreal width = opWidth + italic;
        if (hasSup) {
            width = qMax(width, supX + sup.width);
            limits.add(std::move(sup), QPointF(supX, -shift.up));
        }
        if (hasSub) {
            width = qMax(width, subX + sub.width);
            limits.add(std::move(sub), QPointF(subX, shift.down));
        }
        limits.add(std::move(opBox), QPointF(0, 0));
        limits.fitChildren();
        limits.width = width + ((hasSup || hasSub) ? constant(MathConstant::SpaceAfterScript, st) : 0);
    }
    const qreal gap = 3 * em(st) / 18.0;
    Box out = Box::group();
    const qreal argX = limits.width + gap;
    const qreal argItalic = arg.italic;
    const qreal end = argX + arg.width;
    out.add(std::move(limits), QPointF(0, 0));
    out.add(std::move(arg), QPointF(argX, 0));
    out.fitChildren();
    out.width = end;
    out.italic = argItalic;
    return out;
}

// m:limLow / m:limUpp: the limit centred below or above in script style
// with the lower/upper limit gaps (lim under n→∞, text over an arrow).
Box Layouter::limit(const Node &n, Style st, const SlotPath *path, int item)
{
    const bool low = n.kind == NodeKind::LimLow;
    Box base = argBox(n, 0, st, path, item);
    Box lim = argBox(n, 1, low ? st.subscript() : st.superscript(), path, item);
    base.absorbItalic();
    lim.absorbItalic();
    const qreal width = qMax(base.width, lim.width);
    qreal y = 0;
    if (low) {
        y = base.descent + qMax(constant(MathConstant::LowerLimitGapMin, st) + lim.ascent,
                                constant(MathConstant::LowerLimitBaselineDropMin, st));
    } else {
        y = -(base.ascent + qMax(constant(MathConstant::UpperLimitGapMin, st) + lim.descent,
                                 constant(MathConstant::UpperLimitBaselineRiseMin, st)));
    }
    Box out = Box::group();
    const qreal baseX = (width - base.width) / 2;
    const qreal limX = (width - lim.width) / 2;
    out.add(std::move(base), QPointF(baseX, 0));
    out.add(std::move(lim), QPointF(limX, y));
    out.fitChildren();
    out.width = width;
    return out;
}

// m:groupChr: the character stretched along the base (⏟ ⏞ arrows) with
// the stretch-stack gaps.
Box Layouter::groupChar(const Node &n, Style st, const SlotPath *path, int item)
{
    const QColor color = structureColor(n);
    Box base = argBox(n, 0, st, path, item);
    base.absorbItalic();
    const char32_t c = firstCodePoint(Math::groupChar(n));
    if (c == 0) {
        return base;
    }
    const bool top = groupCharPosition(n) == VerticalPosition::Top;
    const qreal px = pixelSize(st);
    const StretchedGlyph g = m_font.stretch(m_font.glyph(c, px), Qt::Horizontal, base.width, px);
    const qreal width = qMax(base.width, g.width);
    qreal y = 0;
    if (top) {
        y = -(base.ascent + constant(MathConstant::StretchStackGapAboveMin, st)) - g.descent;
    } else {
        y = base.descent + constant(MathConstant::StretchStackGapBelowMin, st) + g.ascent;
    }
    Box out = Box::group();
    const qreal baseX = (width - base.width) / 2;
    out.add(std::move(base), QPointF(baseX, 0));
    out.add(stretchedBox(g, color), QPointF((width - g.width) / 2, y));
    out.fitChildren();
    out.width = width;
    return out;
}

// m:func: the upright name, the function-application space (TeX's Op–Ord
// thin space; none before an opening bracket), the argument.
Box Layouter::function(const Node &n, Style st, const SlotPath *path, int item)
{
    Box name = argBox(n, 0, st, path, item);
    Box arg = argBox(n, 1, st, path, item);
    name.absorbItalic();
    AtomClass next = AtomClass::Ord;
    if (n.args.size() > 1 && !n.args.at(1).items.isEmpty()) {
        next = leftClass(*n.args.at(1).items.first());
    }
    const qreal gap = next == AtomClass::Open ? 0 : 3 * em(st) / 18.0;
    Box out = Box::group();
    const qreal argX = name.width + gap;
    const qreal end = argX + arg.width;
    const qreal italic = arg.italic;
    out.add(std::move(name), QPointF(0, 0));
    out.add(std::move(arg), QPointF(argX, 0));
    out.fitChildren();
    out.width = end;
    out.italic = italic;
    return out;
}

} // namespace QindaOffice::Math
