// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel's built-in PivotTable styles (M74, D-083), drawn the way table
// styles are (M36): from the style's name at paint time, never baked into
// the cells — the file keeps only pivotTableStyleInfo, so Excel draws its
// own and a style change in either program changes nothing else.
//
// Each style is a list of Excel's PivotTable style elements (wholeTable,
// headerRow, totalRow, row and column subheadings, subtotal rows and
// columns, stripes, page fields …), merged in Excel's precedence order for
// a cell from its place in the layout. The colours come from the theme's
// accents (index 4 … 9 of `theme`: dk1, lt1, dk2, lt2, accent1 … accent6),
// so a workbook with its own theme draws in its own colours.

#include "pivot.h"

#include <optional>

namespace QindaOffice::Calc {

struct PivotCellLook {
    std::optional<Rgb> fill;
    std::optional<Rgb> text;
    bool bold = false;
    std::optional<Rgb> top;    // a rule along the cell's top edge
    std::optional<Rgb> bottom;
    bool topDouble = false;
    quint8 indent = 0;         // compact form: label levels to indent
};

// The Office theme's colours (2013–2022): dk1, lt1, dk2, lt2, accent1 … 6.
[[nodiscard]] QVector<Rgb> pivotDefaultTheme();
// The styles QindaCalc draws, gallery order: PivotStyleLight15 … 21 (the
// family of Excel's default, Light16), PivotStyleMedium2, PivotStyleMedium9.
[[nodiscard]] QStringList pivotStyleNames();
// What a cell of a rendered pivot looks like; nullopt outside it or with no style.
[[nodiscard]] std::optional<PivotCellLook> pivotCellLook(const PivotTable &pivot, const CellAddr &addr,
                                                         const QVector<Rgb> &theme);

} // namespace QindaOffice::Calc
