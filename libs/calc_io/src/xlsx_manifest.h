// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The PatchManifest: the write half of the bridge builds one while the model
// is in hand, and the post-pass (xlsx_post.cpp) applies it to the saved
// package — package surgery for the parts QXlsx has no model for, without
// touching the vendored library (the write-side mirror of xlsx_package.h's
// read-side normalization).

#include "chart_spec.h"
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
    };
    QList<SheetPatch> sheets;

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
};

} // namespace QindaOffice::Calc::IO
