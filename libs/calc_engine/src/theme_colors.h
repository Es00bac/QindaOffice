// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The workbook theme's colours (M90, D-098), for what Excel colours by
// theme — sparklines first: the twelve colours of the theme part's
// <a:clrScheme> in SpreadsheetML's theme-index order (0 Light 1, 1 Dark 1,
// 2 Light 2, 3 Dark 2, 4–9 Accent 1–6, 10 Hyperlink, 11 Followed
// Hyperlink — Excel swaps the dark/light pairs of DrawingML's order), and
// Excel's tint. A workbook without a theme part gets the theme calc_io
// writes for it (QXlsx's Office theme), so the screen matches the file.
// Qt Core only.

#include "value.h"

#include <QByteArray>
#include <QVector>

namespace QindaOffice::Calc {

[[nodiscard]] QVector<Rgb> themePalette(const QByteArray &themeXml);
// Excel's tint: the colour's HLS lightness moved towards black (tint < 0)
// or white (tint > 0) by |tint|.
[[nodiscard]] Rgb tinted(const Rgb &color, double tint);
// A theme index (with its tint) or an explicit colour through the palette;
// `fallback` when neither is set or the index is out of range.
[[nodiscard]] Rgb themeColor(const QVector<Rgb> &palette, int theme, double tint, const Rgb &fallback);

} // namespace QindaOffice::Calc
