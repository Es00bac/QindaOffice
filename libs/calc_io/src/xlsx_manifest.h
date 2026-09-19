// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The PatchManifest: the write half of the bridge builds one while the model
// is in hand, and the post-pass (xlsx_post.cpp) applies it to the saved
// package — package surgery for the parts QXlsx has no model for, without
// touching the vendored library (the write-side mirror of xlsx_package.h's
// read-side normalization).

#include "chart_spec.h"
#include "image_spec.h"
#include "sheet_rules.h"

#include <QByteArray>
#include <QHash>
#include <QString>

namespace QindaOffice::Calc::IO {

struct PatchManifest {
    // One sheet's additions, keyed by the sheet's part name (deterministic
    // from QXlsx: xl/worksheets/sheet<N>.xml in document order).
    struct SheetPatch {
        QString sheetPart;       // "xl/worksheets/sheet1.xml"
        QByteArray autoFilterXml; // "<autoFilter ...>...</autoFilter>", or empty
        QString commentsPart;    // "xl/comments1.xml", or empty
        QByteArray commentsXml;  // the comments part's content
        QList<TableSpec> tables; // structured tables (parts + tableParts element)
        // View state: frozen panes (0/0 = none), the selection cell, and
        // page setup (empty = default). Written by expanding QXlsx's
        // self-closed <sheetView> and inserting <pageSetup>.
        qint32 freezeRows = 0;
        qint32 freezeCols = 0;
        QString activeCell;      // A1 text; empty = none
        QByteArray pageSetupXml; // "<pageSetup .../>", or empty
    };
    QList<SheetPatch> sheets;

    // Workbook-level print names (_xlnm.Print_Area / _xlnm.Print_Titles):
    // (localSheetId, body text) pairs for the <definedNames> post-pass edit
    // (QXlsx's defineName has no scope parameter).
    QList<QPair<int, QString>> printAreas;
    QList<QPair<int, QString>> printTitles;

    // Charts. Grouping-only patches target a chart part QXlsx already wrote
    // (its grouping serialization is commented out upstream); generated
    // charts carry the full spec + anchor and land as new parts with an
    // anchor merged into the sheet's drawing.
    struct ChartPatch {
        QString sheetPart;      // the sheet the chart is anchored on
        QString qxlsxChartPart; // set for grouping-only patches ("xl/charts/chartN.xml")
        bool stacked = false;
        bool generate = false;  // true → generate part + anchor from `spec`
        ChartSpec spec;
        int toCol = 0;          // anchor's bottom-right, computed from the
        int toRow = 0;          // sheet's geometry while the model is in hand
    };
    QList<ChartPatch> charts;

    // Pictures: the file's own bytes into xl/media plus an <xdr:pic> anchor
    // merged into the sheet's drawing (QXlsx re-encodes images as PNG and
    // sizes them from DPI, so it cannot carry these — see xlsx_post_images).
    struct ImagePatch {
        QString sheetPart;
        ImageSpec spec;
    };
    QList<ImagePatch> images;
};

} // namespace QindaOffice::Calc::IO
