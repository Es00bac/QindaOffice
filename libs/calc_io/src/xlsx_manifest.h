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
#include "workbook.h"

#include <QByteArray>
#include <QHash>
#include <QString>

namespace QindaOffice::Calc::IO {

struct PatchManifest {
    // A cell QXlsx can only write as a number (it has no API for an error
    // cell, and writeFormula caches a double): the t attribute and <v> text
    // it really needs (M61, D-069). t is "e", "str" or "b".
    struct CellType {
        QByteArray type;
        QString value;
    };

    // One sheet's additions, keyed by the sheet's part name (deterministic
    // from QXlsx: xl/worksheets/sheet<N>.xml in document order).
    struct SheetPatch {
        QString sheetPart;       // "xl/worksheets/sheet1.xml"
        QHash<QString, CellType> cellTypes; // A1 reference -> real type and value
        QByteArray autoFilterXml; // "<autoFilter ...>...</autoFilter>", or empty
        QString commentsPart;    // "xl/comments1.xml", or empty
        QByteArray commentsXml;  // the comments part's content
        QList<CellAddr> commentCells; // where the notes sit (their legacy VML shapes)
        int vmlNumber = 0;       // N of xl/drawings/vmlDrawingN.vml (and its shape-id block)
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

    // Every defined name the file gets — user names, sheet-scoped and
    // formula names, print areas and titles — written as one <definedNames>
    // block by the post-pass (QXlsx's defineName knows neither hidden names
    // nor comments; D-069).
    struct DefinedNameEntry {
        QString name;
        int localSheetId = -1; // -1: workbook scope
        QString body;          // file spelling, no '='
        bool hidden = false;
        QString comment;
    };
    QList<DefinedNameEntry> definedNames;

    // The source package's theme and document properties (D-069): written
    // over QXlsx's fixed theme and "Qt Xlsx Library" properties.
    WorkbookSource source;

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
