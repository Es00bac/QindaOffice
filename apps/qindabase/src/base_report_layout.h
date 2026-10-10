// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Report page control (M86, D-094): the engine's Force New Page and Keep
// Together (base_report.h band flags) put onto the document QindaTK lays a
// report out as (LocalReport::documentJson), so preview, print and PDF —
// which all paginate that document — break pages where Access would.
//
// AGENT-CONTRACT (with QindaTK's LocalReport::documentJson, 0.1.0-r17):
// every band other than a detail row is one paragraph block, each run of
// detail rows one table block, and an empty paragraph closes a document
// that would end with a table. applyReportPageControl checks that the
// block count matches that walk and changes nothing when it does not (a
// toolkit that lays reports out another way keeps working, unbroken).
//  * pageBreakBefore → the paragraph's "pageBreakBefore"; a spacer band →
//    an empty 1 pt paragraph that starts a page (a break before detail
//    rows, which share one table); keepTogether → "keepTogether".
//  * Keep Together "Whole Group" (a group header's keepWith) needs the
//    pages: the document is paginated by QindaTK's own RichTextLayout and
//    a group that would cross a page while it fits on one starts a new page
//    instead, the first such group first, until none is left (at most
//    kKeepTogetherPasses groups are moved; a group taller than a page runs
//    on, as in Access).

#include "base_report.h"

#include <QJsonObject>

namespace QindaOffice::Apps::Base {

constexpr int kKeepTogetherPasses = 64;

struct PageControl {
    bool applied = false; // the toolkit's document matched the bands
    int pageBreaks = 0;   // paragraphs that start a page
    int groupsMoved = 0;  // whole groups moved to a new page
};

// Needs a QGuiApplication (text is shaped to paginate).
PageControl applyReportPageControl(QJsonObject *document, const QList<QindaOffice::Base::ReportBand> &bands);

} // namespace QindaOffice::Apps::Base
