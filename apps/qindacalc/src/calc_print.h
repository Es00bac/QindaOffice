// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Printing a sheet the way Excel prints it (M78, D-087): the pages
// calc_engine's page_layout.h cuts — print area, print titles, scale or
// Fit to, manual breaks, margins, page order — drawn with the cells' look
// (fills, conditional formats with their bars and icons, fonts, number
// formats, alignment, borders, pictures), the gridlines and row and column
// headings when Page Setup asks for them, centred on the page when asked,
// with the header and footer for each page (first page and odd/even
// pages, &P, &N, &D, &T, &F, &Z, &A). Print, Print Preview and Export PDF
// share this one renderer (office_quick's PrintRenderCallback), so paper,
// preview and PDF cannot drift.

#include "print_service.h"

#include <QFont>
#include <QPageLayout>
#include <QString>

namespace QindaOffice::Apps::QindaCalc {

class SpreadsheetDocument;

struct SheetPrintContext {
    QFont baseFont;    // the sheet's default cell font (the grid's)
    QString fileName;  // &F ("Book1" for a workbook never saved)
    QString filePath;  // &Z, with its trailing separator
};

// The renderer for one sheet of `document`, as it is when printing starts.
[[nodiscard]] Quick::PrintRenderCallback sheetPrintRender(const SpreadsheetDocument *document, qint32 sheet,
                                                          const SheetPrintContext &context);

// The paper the sheet's Page Setup asks for (size and orientation; the
// renderer draws its own margins).
[[nodiscard]] QPageLayout sheetPageLayout(const SpreadsheetDocument *document, qint32 sheet);

} // namespace QindaOffice::Apps::QindaCalc
