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
    // A table part as the post-pass writes it (M69, D-078): the model's
    // record — its column names already the file's (tableForFile) — and,
    // per column, what the cells say: the calculated column formula and the
    // totals row's function, label or formula, in Excel's spelling.
    struct TablePart {
        TableSpec spec;
        struct Column {
            QString calculatedFormula;
            QString totalsFunction; // sum, average, count, countNums, max, min, stdDev, var, custom; empty: none
            QString totalsLabel;
            QString totalsFormula;  // with totalsFunction "custom"
        };
        QList<Column> columns;
    };

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
        QList<TablePart> tables; // structured tables (parts + tableParts element)
        // View state: frozen panes (0/0 = none), the selection cell, and
        // page setup (empty = default). Written by expanding QXlsx's
        // self-closed <sheetView> and inserting <pageSetup>.
        qint32 freezeRows = 0;
        qint32 freezeCols = 0;
        QString activeCell;      // A1 text; empty = none
        QByteArray pageSetupXml; // "<pageSetup .../>", or empty

        // ---- M66 (D-075): xlsx_post_sheet.cpp writes these ----
        QString name;            // the sheet's name (the carry stage maps sources by it)
        SheetSource source;      // the source part and its structural journal (carry stage)
        // <sheetPr>: the VBA code name and where outline summaries sit.
        bool summaryBelow = true;
        bool summaryRight = true;
        // Rows with outline state (level, hidden, collapsed), patched onto
        // QXlsx's <row> elements (rows it did not write are added).
        QMap<qint32, LineOutline> rowOutline;
        // A <cols> replacing QXlsx's when columns are hidden or grouped.
        QByteArray colsXml;
        quint8 outlineLevelRow = 0;
        quint8 outlineLevelCol = 0;
        QByteArray sheetProtectionXml; // "<sheetProtection …/>", or empty
        // Hyperlinks: the ref, the external target (written to the sheet's
        // rels, TargetMode External) or the in-workbook location.
        struct Link {
            QString ref;
            QString target;
            QString location;
            QString display;
            QString tooltip;
        };
        QList<Link> hyperlinks;
        // Dynamic-array anchors: A1 → the spill ref its <f t="array"> got.
        // The post-pass gives each cm="1" (the XLDAPR block of metadata.xml).
        QHash<QString, QString> arrayRefs;
        // One cell per cell format whose protection is not Excel's default
        // (locked, not hidden): A1 → bit 1 unlocked, bit 2 formula hidden.
        // QXlsx writes no <protection>; the post-pass adds it to the xf
        // those cells use.
        QHash<QString, quint8> protectionCells;
        // Table parts in this sheet's order → the source table part each
        // came from (empty: a new table), for query tables.
        QStringList tableSources;
        // M69 (D-078): the sheet is a chart sheet. QXlsx writes an empty
        // worksheet in its place (every sheet keeps its part number) and
        // the carry stage swaps in the source chartsheet part.
        bool chartSheet = false;
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
    // over QXlsx's fixed theme and "Qt Xlsx Library" properties. Since M66
    // also the carried package (xlsx_carry_write.cpp copies from it).
    WorkbookSource source;

    // ---- M66 (D-075) ----
    WorkbookProtection protection; // <workbookProtection>, written when set
    bool dynamicArrays = false;    // some cell spills: xl/metadata.xml with XLDAPR
    // The file is a macro-enabled workbook (.xlsm): the main part's content
    // type says so and the source's VBA project is carried. For .xlsx the
    // project stays behind (Excel refuses an .xlsx that holds one).
    bool macroEnabled = false;
    // Save-time notes for the user: a part the carry stage had to leave
    // behind (a thread whose note was edited, macros left out of an .xlsx).
    QStringList *warnings = nullptr;

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
