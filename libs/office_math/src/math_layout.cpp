// SPDX-License-Identifier: GPL-3.0-or-later
// The layout core: sequences of atoms with TeX's spacing, runs set in the
// math font's styled alphabets, slots for the caret, and the equation's
// lines. Structures live in math_layout_{frac,scripts,table}.cpp.
#include "math_alphabets.h"
#include "math_layout_p.h"
#include "math_omml.h"

#include <functional>

namespace QindaOffice::Math {

namespace {

// TeX's inter-atom spacing (The TeXbook, chapter 18): 1 thin (3mu),
// 2 medium (4mu), 3 thick (5mu); negative = not in script styles.
constexpr int kSpacing[8][8] = {
    // Ord Op Bin Rel Open Close Punct Inner
    {0, 1, -2, -3, 0, 0, 0, -1},     // Ord
    {1, 1, 0, -3, 0, 0, 0, -1},      // Op
    {-2, -2, 0, 0, -2, 0, 0, -2},    // Bin
    {-3, -3, 0, 0, -3, 0, 0, -3},    // Rel
    {0, 0, 0, 0, 0, 0, 0, 0},        // Open
    {0, 1, -2, -3, 0, 0, 0, -1},     // Close
    {-1, -1, 0, -1, -1, -1, -1, -1}, // Punct
    {-1, 1, -2, -3, -1, 0, -1, -1},  // Inner
};

bool isBinTrigger(AtomClass c)
{
    return c == AtomClass::Bin || c == AtomClass::Op || c == AtomClass::Rel || c == AtomClass::Open
        || c == AtomClass::Punct;
}

} // namespace

Layouter::Layouter(const LayoutOptions &options, MathFont font)
    : m_options(options)
    , m_font(std::move(font))
    , m_basePixels(qMax<qreal>(1, options.fontSizePt * options.dpi / 72.0))
{
}

qreal Layouter::pixelSize(Style st) const
{
    const qreal script = m_font.table().constant(MathConstant::ScriptPercentScaleDown);
    const qreal scriptScript = m_font.table().constant(MathConstant::ScriptScriptPercentScaleDown);
    const qreal s1 = script > 0 ? script / 100.0 : 0.7;
    const qreal s2 = scriptScript > 0 ? scriptScript / 100.0 : 0.5;
    switch (st.level) {
    case 0:
        return m_basePixels;
    case 1:
        return m_basePixels * s1;
    case -1:
        return m_basePixels / s1;
    case -2:
        return m_basePixels / s2;
    default:
        return m_basePixels * s2;
    }
}

qreal Layouter::constant(MathConstant c, Style st) const
{
    return m_font.constant(c, pixelSize(st));
}

qreal Layouter::space(AtomClass left, AtomClass right, Style st) const
{
    int code = kSpacing[int(left)][int(right)];
    if (code < 0) {
        if (st.level > 0) {
            return 0;
        }
        code = -code;
    }
    static constexpr int kMu[] = {0, 3, 4, 5};
    return code == 0 ? 0 : kMu[code] * em(st) / 18.0;
}

Box Layouter::glyphBox(const GlyphInfo &glyph, const QColor &color) const
{
    Box b;
    b.type = Box::Type::Glyphs;
    b.path = glyph.font.pathForGlyph(glyph.glyph);
    b.glyphs = {glyph.glyph};
    b.positions = {QPointF(0, 0)};
    b.color = color;
    b.width = glyph.advance;
    b.ascent = qMax<qreal>(0, -glyph.bounds.top());
    b.descent = qMax<qreal>(0, glyph.bounds.bottom());
    b.italic = glyph.italic;
    if (glyph.topAccent) {
        b.accentX = *glyph.topAccent;
    }
    return b;
}

Box Layouter::stretchedBox(const StretchedGlyph &glyph, const QColor &color) const
{
    Box b;
    b.type = Box::Type::Glyphs;
    // OpenType extender glyphs intentionally overlap. Winding fill keeps
    // those joins solid; odd-even fill cuts holes at every overlap seam.
    b.path.setFillRule(Qt::WindingFill);
    for (qsizetype i = 0; i < glyph.glyphs.size() && i < glyph.positions.size(); ++i)
        b.path.addPath(glyph.font.pathForGlyph(glyph.glyphs.at(i)).translated(glyph.positions.at(i)));
    b.glyphs = glyph.glyphs;
    b.positions = glyph.positions;
    b.color = color;
    b.width = glyph.width;
    b.ascent = glyph.ascent;
    b.descent = glyph.descent;
    b.italic = glyph.italic;
    if (glyph.topAccent) {
        b.accentX = *glyph.topAccent;
    }
    return b;
}

Box Layouter::charBox(char32_t c, Style st, const QColor &color) const
{
    return glyphBox(m_font.glyph(c, pixelSize(st)), color);
}

Box Layouter::placeholderBox(Style st)
{
    Box b;
    if (!m_options.placeholders) {
        return b;
    }
    const qreal e = em(st);
    b.type = Box::Type::Placeholder;
    b.width = 0.6 * e;
    b.ascent = 0.7 * e;
    b.descent = 0.02 * e;
    b.rect = QRectF(0.08 * e, -0.68 * e, 0.44 * e, 0.68 * e);
    b.pen = qMax<qreal>(0.6, 0.045 * e);
    b.color = m_options.color;
    b.color.setAlphaF(0.55f);
    return b;
}

void Layouter::appendRun(QList<Piece> &out, const Node &run, Style st) const
{
    const RunFormat f = runFormat(run);
    const qreal px = pixelSize(st) * f.sizeFactor;
    for (const uint code : run.text().toUcs4()) {
        const char32_t c = code;
        Piece p;
        if (c == U'&' && !f.normalText) {
            p.alignMark = true; // an alignment point (m:eqArr), never drawn
            out.append(std::move(p));
            continue;
        }
        if (f.normalText) {
            p.box = glyphBox(m_font.textGlyph(f.textFamily, f.bold, f.italic, c, px), f.color);
        } else {
            const char32_t shown = displayChar(c);
            const bool bold = f.style == MathStyle::Bold || f.style == MathStyle::BoldItalic;
            const bool italic = f.style == MathStyle::Italic || f.style == MathStyle::BoldItalic
                || (f.style == MathStyle::Default && italicByDefault(shown));
            p.box = glyphBox(m_font.glyph(mathAlphanumeric(shown, f.script, bold, italic), px), f.color);
            p.left = p.right = atomClassOf(shown);
        }
        out.append(std::move(p));
    }
}

Piece Layouter::nodePiece(const Node &node, Style st, const SlotPath *path, int item)
{
    Piece p;
    switch (node.kind) {
    case NodeKind::Fraction:
        p.box = fraction(node, st, path, item);
        break;
    case NodeKind::Superscript:
    case NodeKind::Subscript:
    case NodeKind::SubSup:
    case NodeKind::PreScript:
        p.box = scripts(node, st, path, item);
        break;
    case NodeKind::Radical:
        p.box = radical(node, st, path, item);
        break;
    case NodeKind::Nary:
        p.box = nary(node, st, path, item);
        break;
    case NodeKind::Delimiter:
        p.box = delimiter(node, st, path, item);
        break;
    case NodeKind::Function:
        p.box = function(node, st, path, item);
        break;
    case NodeKind::Accent:
        p.box = accent(node, st, path, item);
        break;
    case NodeKind::Bar:
        p.box = bar(node, st, path, item);
        break;
    case NodeKind::Box:
        p.box = boxNode(node, st, path, item);
        break;
    case NodeKind::BorderBox:
        p.box = borderBox(node, st, path, item);
        break;
    case NodeKind::EqArray:
        p.box = eqArray(node, st, path, item);
        break;
    case NodeKind::Matrix:
        p.box = matrix(node, st, path, item);
        break;
    case NodeKind::GroupChar:
        p.box = groupChar(node, st, path, item);
        break;
    case NodeKind::LimLow:
    case NodeKind::LimUpp:
        p.box = limit(node, st, path, item);
        break;
    case NodeKind::Phantom:
        p.box = phantom(node, st, path, item);
        break;
    case NodeKind::Kept:
        if (const std::optional<Argument> shown = keptMath(node)) {
            p.box = sequence(*shown, st, nullptr, false);
            p.box.absorbItalic();
        }
        break;
    case NodeKind::Run:
        break;
    }
    p.left = leftClass(node);
    p.right = rightClass(node);
    return p;
}

QList<Piece> Layouter::pieces(const Argument &arg, Style st, const SlotPath *path)
{
    QList<Piece> out;
    for (int i = 0; i < arg.items.size(); ++i) {
        const Node &node = *arg.items.at(i);
        if (node.kind == NodeKind::Run) {
            appendRun(out, node, st);
        } else {
            out.append(nodePiece(node, st, path, i));
        }
    }
    return out;
}

qreal Layouter::positions(QList<Piece> &pieces, Style st, QList<qreal> *xs) const
{
    // TeX rules 5 and 6: a binary operator with nothing to bind is ordinary.
    QList<int> visible;
    for (int i = 0; i < pieces.size(); ++i) {
        if (!pieces.at(i).alignMark) {
            visible.append(i);
        }
    }
    for (int k = 0; k < visible.size(); ++k) {
        Piece &p = pieces[visible.at(k)];
        if (p.left != AtomClass::Bin) {
            continue;
        }
        bool ord = k == 0 || k + 1 == visible.size() || isBinTrigger(pieces.at(visible.at(k - 1)).right);
        if (!ord) {
            const AtomClass next = pieces.at(visible.at(k + 1)).left;
            ord = next == AtomClass::Rel || next == AtomClass::Close || next == AtomClass::Punct;
        }
        if (ord) {
            p.left = AtomClass::Ord;
            if (p.right == AtomClass::Bin) {
                p.right = AtomClass::Ord;
            }
        }
    }
    xs->resize(pieces.size());
    qreal x = 0;
    int previous = -1;
    for (int i = 0; i < pieces.size(); ++i) {
        const Piece &p = pieces.at(i);
        if (p.alignMark) {
            (*xs)[i] = x;
            continue;
        }
        if (previous >= 0) {
            x += pieces.at(previous).box.italic + space(pieces.at(previous).right, p.left, st);
        }
        (*xs)[i] = x;
        x += p.box.width;
        previous = i;
    }
    return x;
}

Box Layouter::assemble(QList<Piece> &pieces, const QList<qreal> &xs, qreal width, Style st, const SlotPath *path,
                       bool placeholder)
{
    Box out = Box::group();
    if (pieces.isEmpty()) {
        if (placeholder) {
            out.add(placeholderBox(st), QPointF(0, 0));
            out.fitChildren();
        }
    } else {
        qreal italic = 0;
        if (pieces.size() == 1 && pieces.first().box.accentX >= 0) {
            out.accentX = pieces.first().box.accentX + xs.first();
        }
        for (int i = 0; i < pieces.size(); ++i) {
            if (!pieces.at(i).alignMark) {
                italic = pieces.at(i).box.italic;
            }
            out.add(std::move(pieces[i].box), QPointF(xs.at(i), 0));
        }
        out.fitChildren();
        out.width = width;
        out.italic = italic;
    }
    if (path != nullptr) {
        SlotGeometry g;
        g.slot.line = path->line;
        g.slot.steps = path->steps;
        if (pieces.isEmpty()) {
            g.stops = {out.width / 2};
        } else {
            g.stops = xs;
            g.stops.append(width);
        }
        g.ascent = qMax(out.ascent, 0.7 * em(st));
        g.descent = qMax(out.descent, 0.2 * em(st));
        g.rect = out.bounds();
        out.slot = int(m_slots.size());
        m_slots.append(g);
    }
    return out;
}

Box Layouter::sequence(const Argument &arg, Style st, const SlotPath *path, bool placeholder)
{
    Style s = st;
    if (const int size = argumentSize(arg); size != 0) {
        s.level = qBound(-2, st.level - size, 2);
    }
    QList<Piece> ps = pieces(arg, s, path);
    QList<qreal> xs;
    const qreal width = positions(ps, s, &xs);
    return assemble(ps, xs, width, s, path, placeholder);
}

Box Layouter::argBox(const Node &node, int index, Style st, const SlotPath *path, int item, bool placeholder)
{
    if (index < 0 || index >= node.args.size()) {
        return Box::group();
    }
    if (path == nullptr) {
        return sequence(node.args.at(index), st, nullptr, placeholder);
    }
    const SlotPath child = path->child(item, index);
    return sequence(node.args.at(index), st, &child, placeholder);
}

Box Layouter::layout(const Equation &equation)
{
    Box root = Box::group();
    if (!m_font.isValid()) {
        return root;
    }
    const Style st{0, m_options.display.value_or(equation.display), false};
    std::vector<Box> lines;
    for (int i = 0; i < equation.lines.size(); ++i) {
        const SlotPath path{i, {}};
        Box line = sequence(equation.lines.at(i), st, &path);
        line.absorbItalic();
        lines.push_back(std::move(line));
    }
    if (lines.size() == 1) {
        root.add(std::move(lines.front()), QPointF(0, 0));
        root.fitChildren();
        return root;
    }
    // Several m:oMath in one m:oMathPara: one line each, justified as m:jc says.
    const Justification jc = justification(equation, m_options.settings.defaultJustification);
    qreal widest = 0;
    for (const Box &b : lines) {
        widest = qMax(widest, b.width);
    }
    const qreal skip = 1.3 * em(st);
    const qreal gap = 0.3 * em(st);
    qreal y = 0;
    qreal previousDescent = 0;
    for (size_t i = 0; i < lines.size(); ++i) {
        Box &b = lines.at(i);
        if (i > 0) {
            y += qMax(skip, previousDescent + b.ascent + gap);
        }
        previousDescent = b.descent;
        const qreal x = jc == Justification::Center ? (widest - b.width) / 2
            : jc == Justification::Right            ? widest - b.width
                                                    : 0;
        root.add(std::move(b), QPointF(x, y));
    }
    root.fitChildren(); // the origin stays on the FIRST line's baseline
    return root;
}

QList<SlotGeometry> Layouter::slotGeometry(const Box &root) const
{
    QList<SlotGeometry> out = m_slots;
    std::function<void(const Box &, QPointF)> walk = [&](const Box &b, QPointF origin) {
        if (b.slot >= 0 && b.slot < out.size()) {
            SlotGeometry &g = out[b.slot];
            for (qreal &stop : g.stops) {
                stop += origin.x();
            }
            g.baseline += origin.y();
            g.rect.translate(origin);
        }
        for (const Box &c : b.children) {
            walk(c, origin + c.pos);
        }
    };
    walk(root, QPointF(0, 0));
    return out;
}

} // namespace QindaOffice::Math
