// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel's Custom Views (View ▸ Workbook Views ▸ Custom Views; M82, D-091):
// a named view of the workbook that Show puts back — on every sheet its
// zoom, frozen or split panes, the cell shown at the top left and the
// selection, and, as chosen when the view was added, its hidden rows and
// columns with the filter settings and its print settings.
//
// Excel keeps a view in three places and so does the model: its name and
// options (Workbook::customViews — <customWorkbookView> in workbook.xml),
// each sheet's part (Sheet::customViews — <customSheetView> in the
// worksheet) and, as hidden defined names, its hidden rows and columns,
// print area, print titles and filter range (Z_<GUID>_.wvu.Rows, .wvu.Cols,
// .wvu.PrintArea, .wvu.PrintTitles, .wvu.FilterData). calc_io reads and
// writes all three; the GUID ties them together.
//
// AGENT-NOTE: Excel offers no Custom Views in a workbook that holds a
// table (the button is greyed out); customViewsAllowed() says the same, and
// QindaCalc refuses as Excel does rather than write views Excel would not
// make.
//
// Threading: plain functions over a Workbook the caller owns.

#include "sheet.h"

#include <QList>
#include <QString>

#include <utility>

namespace QindaOffice::Calc {

class Workbook;

struct CustomViewSpec {
    QString name;
    QString guid; // "{8C0C7A35-…}" upper case, as Excel writes it
    bool includePrintSettings = true;
    bool includeHiddenRowCol = true;
    QString activeSheet; // the sheet the view shows, by name
    // <customWorkbookView>'s other attributes as read (window size, …).
    QList<std::pair<QString, QString>> attributes;
    bool operator==(const CustomViewSpec &) const = default;
};

// A fresh view GUID in Excel's spelling.
[[nodiscard]] QString newCustomViewGuid();

// Whether Custom Views may be used: not in a workbook with a table.
[[nodiscard]] bool customViewsAllowed(const Workbook &wb);

// Add: the workbook as it is now, as view `name` (replacing a view of that
// name, as Excel's Add does). `window` gives each sheet's window part —
// zoom, split, top-left cell, selection — by sheet index; the document part
// (frozen panes, hidden lines, filter, print settings) is read here. The
// new view's GUID.
QString addCustomView(Workbook &wb, const QString &name, bool printSettings, bool hiddenRowsCols, qint32 activeSheet,
                      const QHash<qint32, Sheet::CustomView> &window);

// Show: view `guid`'s document part put back on every sheet that has one —
// frozen panes, sheet visibility, and as the view holds them hidden rows
// and columns with the filter, and print settings. `activeSheet` gets the
// sheet it shows (-1 when its sheet is gone). False when there is no such
// view.
bool showCustomView(Workbook &wb, const QString &guid, qint32 *activeSheet);

// Delete: the view and every sheet's part of it.
bool deleteCustomView(Workbook &wb, const QString &guid);

// A sheet's part of view `guid`, or nullptr.
[[nodiscard]] const Sheet::CustomView *customViewOf(const Sheet &sheet, const QString &guid);

} // namespace QindaOffice::Calc
