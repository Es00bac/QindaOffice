// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "parser.h"
#include "value.h"

namespace QindaOffice::Calc {

// A sparse sheet cell (plan section 7.2): the formula text as entered, the
// parsed AST (shared so identical formulas cost one parse), the last
// computed value, the style id and the volatility flag (volatile cells
// recalculate on every edit regardless of dependencies).
struct Cell {
    QString formula;      // text after '='; empty for literal cells
    NodePtr ast;          // parsed formula; null for literal cells
    Value value;
    qint32 styleId = 0;
    bool isVolatile = false;
    // Dynamic arrays (M66, D-075; spill.cpp). An anchor's array result
    // spills into the cells below and to the right; those cells hold the
    // values and name their anchor. A spilled cell is never user content:
    // writing into it blocks the anchor (#SPILL!), and the engine drops and
    // rewrites spilled cells whenever the anchor recalculates.
    bool dynamicArray = false; // the formula spills (typed in the app, or Excel's cm/XLDAPR)
    CellAddr spillAnchor;      // set on a spilled cell: the anchor it shows a value of

    [[nodiscard]] bool hasFormula() const { return ast != nullptr; }
    [[nodiscard]] bool isSpilled() const { return spillAnchor.isValid() && !ast; }
};

// Formatting record. NumberFormat code follows Excel syntax (plan section
// 7.2); font/fg/bg follow Qt conventions so the view can apply them directly.
struct CellStyle {
    QString numberFormat = QStringLiteral("General");
    QString fontFamily;
    qreal fontSize = 0;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikeout = false;
    std::optional<Rgb> fg;
    std::optional<Rgb> bg;
    int alignment = 0; // 0 left, 1 center, 2 right
    int vAlign = 1;    // 0 top, 1 center, 2 bottom (center matches legacy painting)
    bool wrap = false;
    int borders = 0;   // bit0 left bit1 right bit2 top bit3 bottom
    // Cell protection (M66, D-075), in force while the sheet is protected:
    // a locked cell refuses edits, a hidden one keeps its formula out of
    // the formula bar. Excel's default is locked, not hidden.
    bool locked = true;
    bool hiddenFormula = false;
};

} // namespace QindaOffice::Calc
