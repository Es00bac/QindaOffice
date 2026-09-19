// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal: the direct package readers (xlsx_read_xml.cpp). QXlsx has no
// public model for conditional formats, data validations, the autofilter or
// comments, so those are read straight out of the package XML after the
// cell walk, per sheet.

#include "xlsx_style.h"

namespace QindaOffice::Calc {
class Sheet;
class Workbook;
}

namespace QindaOffice::Calc::IO {

// Reads conditional formats (+ the dxf table), data validations, the
// autofilter and cell comments for one sheet out of the package at
// `packagePath` (the normalized copy). `sheetIndex` selects the part; the
// palette resolves theme colours in dxfs and data bars.
void readSheetExtras(const QString &packagePath, Workbook *wb, qint32 sheetIndex,
                     const ThemePalette &palette, Sheet *sheet);

} // namespace QindaOffice::Calc::IO
