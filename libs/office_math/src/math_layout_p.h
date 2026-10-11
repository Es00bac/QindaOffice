// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to office_math's layout (math_layout*.cpp): the Layouter that
// turns the model into a Box tree. The algorithms are TeX's (The TeXbook,
// Appendix G) as MathML Core restates them for OpenType MATH constants;
// each structure's function names the rules it follows.
//
// Units are logical pixels at the font size LayoutOptions gives; y grows
// down. A Style is TeX's D/T/S/SS with the cramped flag: level 0 is text
// size, 1 script, 2 scriptscript (negative levels come from m:argSz).

#include "math_box.h"
#include "math_font.h"
#include "math_layout.h"
#include "math_symbols.h"

namespace QindaOffice::Math {

struct Style {
    int level = 0;
    bool display = false;
    bool cramped = false;

    [[nodiscard]] Style superscript() const { return {qMin(level + 1, 2), false, cramped}; }
    [[nodiscard]] Style subscript() const { return {qMin(level + 1, 2), false, true}; }
    [[nodiscard]] Style numerator() const
    {
        return display ? Style{level, false, cramped} : Style{qMin(level + 1, 2), false, cramped};
    }
    [[nodiscard]] Style denominator() const
    {
        return display ? Style{level, false, true} : Style{qMin(level + 1, 2), false, true};
    }
    [[nodiscard]] Style crampedStyle() const { return {level, display, true}; }
};

// The slot an argument box records caret geometry for; null = no slot
// (content of a Kept node, which the caret cannot enter).
struct SlotPath {
    int line = 0;
    QList<MathPosition::Step> steps;

    [[nodiscard]] SlotPath child(int item, int arg) const
    {
        SlotPath p = *this;
        p.steps.append({item, arg});
        return p;
    }
};

// One atom of a sequence: a character of a run or a whole structure.
struct Piece {
    Box box;
    AtomClass left = AtomClass::Ord;
    AtomClass right = AtomClass::Ord;
    bool alignMark = false; // '&': an alignment point, drawn as nothing, transparent to spacing
};

// A run's formatting as the layout needs it.
struct RunFormat {
    QColor color;
    qreal sizeFactor = 1;
    MathStyle style = MathStyle::Default;
    MathScript script = MathScript::Roman;
    bool normalText = false;
    QString textFamily;
    bool bold = false;
    bool italic = false;
};

class Layouter {
public:
    Layouter(const LayoutOptions &options, MathFont font);

    [[nodiscard]] Box layout(const Equation &equation);
    // Slot geometry in root coordinates; valid after layout().
    [[nodiscard]] QList<SlotGeometry> slotGeometry(const Box &root) const;

    // ---- core (math_layout.cpp) -----------------------------------------------------
    [[nodiscard]] Box sequence(const Argument &arg, Style st, const SlotPath *path, bool placeholder = true);
    [[nodiscard]] QList<Piece> pieces(const Argument &arg, Style st, const SlotPath *path);
    // x of each piece (spacing, italic corrections); returns the total width.
    qreal positions(QList<Piece> &pieces, Style st, QList<qreal> *xs) const;
    // The argument box from positioned pieces; records the slot when `path`.
    [[nodiscard]] Box assemble(QList<Piece> &pieces, const QList<qreal> &xs, qreal width, Style st,
                               const SlotPath *path, bool placeholder);
    [[nodiscard]] Box argBox(const Node &node, int index, Style st, const SlotPath *path, int item,
                             bool placeholder = true);
    [[nodiscard]] Box placeholderBox(Style st);
    [[nodiscard]] Box glyphBox(const GlyphInfo &glyph, const QColor &color) const;
    [[nodiscard]] Box stretchedBox(const StretchedGlyph &glyph, const QColor &color) const;
    [[nodiscard]] Box charBox(char32_t c, Style st, const QColor &color) const;
    [[nodiscard]] qreal pixelSize(Style st) const;
    [[nodiscard]] qreal constant(MathConstant c, Style st) const;
    [[nodiscard]] qreal axis(Style st) const { return constant(MathConstant::AxisHeight, st); }
    [[nodiscard]] qreal em(Style st) const { return pixelSize(st); }
    [[nodiscard]] qreal space(AtomClass left, AtomClass right, Style st) const;
    [[nodiscard]] RunFormat runFormat(const Node &run) const;
    [[nodiscard]] QColor structureColor(const Node &node) const;
    [[nodiscard]] static AtomClass leftClass(const Node &node);
    [[nodiscard]] static AtomClass rightClass(const Node &node);
    [[nodiscard]] static bool isSingleChar(const Argument &arg);

    // ---- structures -----------------------------------------------------------------
    // math_layout_frac.cpp
    [[nodiscard]] Box fraction(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] Box radical(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] Box phantom(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] Box boxNode(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] Box borderBox(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] Box bar(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] Box accent(const Node &n, Style st, const SlotPath *path, int item);
    // math_layout_scripts.cpp
    [[nodiscard]] Box scripts(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] Box nary(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] Box limit(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] Box groupChar(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] Box function(const Node &n, Style st, const SlotPath *path, int item);
    // math_layout_table.cpp
    [[nodiscard]] Box delimiter(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] Box eqArray(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] Box matrix(const Node &n, Style st, const SlotPath *path, int item);
    [[nodiscard]] qreal rowSkip(const Node &n, Style st) const;

    // Shared by scripts and n-ary limits: TeX rule 18 shifts.
    struct ScriptShifts {
        qreal up = 0;   // superscript baseline above the base baseline
        qreal down = 0; // subscript baseline below it
    };
    [[nodiscard]] ScriptShifts scriptShifts(const Box &base, bool simpleBase, const Box *sup, const Box *sub,
                                            Style st) const;

private:
    [[nodiscard]] Piece nodePiece(const Node &node, Style st, const SlotPath *path, int item);
    void appendRun(QList<Piece> &out, const Node &run, Style st) const;

    LayoutOptions m_options;
    MathFont m_font;
    qreal m_basePixels = 16;
    QList<SlotGeometry> m_slots; // box-relative until slotGeometry() resolves them
};

} // namespace QindaOffice::Math
