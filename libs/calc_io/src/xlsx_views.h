// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Excel's Custom Views in the package (M82, D-091; calc_engine's
// custom_view.h): workbook.xml's <customWorkbookViews>, each worksheet's
// <customSheetViews> (after the autoFilter, before mergeCells) and the
// views' hidden defined names — Z_<GUID>_.wvu.Rows, .wvu.Cols,
// .wvu.PrintArea, .wvu.PrintTitles and .wvu.FilterData — read into the
// model and written from it. Before M82 the reader kept the names as
// ordinary hidden names and the writer dropped the views.
//
// AGENT-CONTRACT: readDefinedNames (xlsx_read_book.cpp) leaves every name
// isCustomViewName() accepts to readCustomViews, and definedNamesForFile
// gets them back from customViewNames(); <pivotCaches> follows
// <customWorkbookViews> in workbook.xml (pivotCachesAnchor), which both the
// pivot writer and the carry stage honour.

#include "xlsx_manifest.h"

#include "workbook.h"

#include <QByteArray>
#include <QList>

namespace QXlsx {
class ZipReader;
}

namespace QindaOffice::Calc::IO {

// Z_<GUID>_.wvu.*: one of a view's names.
[[nodiscard]] bool isCustomViewName(const QString &name);

// Reads the views of the package into `wb` (after its sheets are read).
void readCustomViews(QXlsx::ZipReader &zip, Workbook *wb);

// workbook.xml's <customWorkbookViews> (empty: none).
[[nodiscard]] QByteArray customWorkbookViewsXml(const Workbook &wb);
// The sheet's <customSheetViews> (empty: none).
[[nodiscard]] QByteArray customSheetViewsXml(const Workbook &wb, qint32 sheet);
// The views' hidden names.
[[nodiscard]] QList<PatchManifest::DefinedNameEntry> customViewNames(const Workbook &wb);

// Where <pivotCaches> goes in workbook.xml: after <customWorkbookViews>
// when there is one, else after <calcPr …/>; -1 when neither is there.
[[nodiscard]] qsizetype pivotCachesAnchor(const QByteArray &workbookXml);

} // namespace QindaOffice::Calc::IO
