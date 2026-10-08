// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Cell text and error spellings as SpreadsheetML stores them (M61, D-069).
//
// ST_Xstring: XML 1.0 cannot carry most control characters, so Excel writes
// them as _xHHHH_ (a carriage return is _x000D_), and a literal "_xHHHH_" in
// the text has its underscore escaped as _x005F_. Readers that skip the
// decoding show "_x000D_" in the cell; writers that skip the encoding emit a
// part no XML parser accepts.

#include "value.h"

#include <QString>

namespace QindaOffice::Calc::IO {

[[nodiscard]] QString decodeXString(const QString &text);
[[nodiscard]] QString encodeXString(const QString &text);

// An error cell's text ("#DIV/0!") as an engine error. Errors the engine has
// no kind for (#SPILL!, #CALC!, #GETTING_DATA, …) read as #VALUE!.
[[nodiscard]] Err errorFromFile(const QString &text);
// The file spelling of an engine error; empty for the engine-only circular
// marker, which has no Excel spelling (the cell is then written as 0, what
// Excel itself shows for a circular reference).
[[nodiscard]] QString errorForFile(Err err);

} // namespace QindaOffice::Calc::IO
