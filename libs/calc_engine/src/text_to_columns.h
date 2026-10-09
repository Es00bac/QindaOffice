// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Data ▸ Text to Columns (M78, D-087): Excel's Convert Text to Columns
// Wizard as pure functions — split one column of text by delimiters or at
// fixed widths, and give every field the cell input its column format
// asks for. The app applies the edits as one undo step. Qt Core only.

#include "value.h"

#include <QChar>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace QindaOffice::Calc {

struct TextToColumnsOptions {
    enum class Mode { Delimited, FixedWidth };
    // Step 3's column data formats. Dates convert text in the given order
    // of day, month and year ("1/2/2026" as MDY is January 2).
    enum class Format { General, Text, DateMDY, DateDMY, DateYMD, DateMYD, DateDYM, DateYDM, Skip };

    Mode mode = Mode::Delimited;
    // Step 2, delimited: Excel's checkboxes (Tab is on by default) and
    // "Other" (its first character).
    bool tab = true;
    bool semicolon = false;
    bool comma = false;
    bool space = false;
    QString other;
    bool consecutiveAsOne = false; // "Treat consecutive delimiters as one"
    QChar qualifier = QLatin1Char('"'); // "Text qualifier"; QChar() = {none}
    // Step 2, fixed width: the character positions where a new column
    // starts (ascending, each > 0).
    QList<int> breaks;
    // Step 3: one per output column; a missing entry is General.
    QList<Format> formats;
    // Step 3 ▸ Advanced: how General recognises numbers.
    QChar decimalSeparator = QLatin1Char('.');
    QChar thousandsSeparator = QLatin1Char(',');
    bool trailingMinus = true; // "123-" is −123
};

// The fields of each line.
[[nodiscard]] QList<QStringList> splitTextToColumns(const QStringList &lines, const TextToColumnsOptions &options);

// Where Excel proposes breaks for fixed width: the positions after a run
// of spaces that every non-empty line has blank at the same place.
[[nodiscard]] QList<int> guessFixedWidthBreaks(const QStringList &lines);

// One converted field: the input the cell takes ("'007" keeps text, a date
// is its serial number) and whether the cell should take a date format
// (Excel's m/d/yyyy) because a date column converted it.
struct TextColumnCell {
    CellAddr addr;
    QString input;
    bool dateFormat = false;
    // The number format the cell should take, when the conversion implies
    // one: "m/d/yyyy" (or "m/d/yyyy h:mm" with a time) for a date, "0%" /
    // "0.00%" for a percentage. Empty: leave the cell's format alone.
    QString numberFormat;
};

// The whole conversion of `lines` (the source column's texts, line i from
// row `firstRow` + i) into cells: line i lands on row destination.row + i
// (on row firstRow + i when destination.row < 0), its first kept field in
// column destination.col. Fields of Skip columns are left out and the
// columns after them close up, as in Excel; a field whose text is empty
// clears its cell; an empty source line writes nothing, and a line with
// fewer fields than others leaves the cells past its last field alone.
// General turns numbers (with the Advanced separators; "(12)", "12-",
// "$1,234", "12%") into numbers, ISO and month/day/year dates into dates
// (Excel's US reading), and leaves the rest to the engine's input rules —
// TRUE, a formula after '=' (as Excel does); a leading apostrophe is kept
// as a character. Text keeps every field as text. A Date column converts
// in its order, two-digit years Excel's way (00–29 → 2000s, 30–99 →
// 1900s), month names allowed, a time after the date kept; what does not
// read as a date stays text.
[[nodiscard]] QList<TextColumnCell> textToColumnsCells(const QStringList &lines, qint32 firstRow,
                                                       const CellAddr &destination,
                                                       const TextToColumnsOptions &options);

// The number of columns the conversion fills (Skip columns not counted):
// what Excel checks to ask "Do you want to replace the contents of the
// destination cells?".
[[nodiscard]] int textToColumnsWidth(const QStringList &lines, const TextToColumnsOptions &options);

} // namespace QindaOffice::Calc
