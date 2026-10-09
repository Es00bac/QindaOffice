// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the carry stage (M66, D-075): the save-side phases applyCarry
// runs in order, and what one phase tells the next.
//
//   1. pivot tables (xlsx_carry_book.cpp) — kept while their location's
//      cells exist; they decide which pivot caches are needed;
//   2. workbook parts (xlsx_carry_book.cpp) — caches (refreshOnLoad), the
//      slicer and timeline caches whose pivot tables were kept, links,
//      connections, the data model, custom XML, persons, the VBA project,
//      and the workbook elements that name them;
//   3. sheets (xlsx_carry_sheet.cpp, xlsx_carry_drawing.cpp) — drawing
//      objects and legacy-drawing shapes, controls, page setup, the sheet
//      extLst (slicers and timelines whose caches were kept), threaded
//      comments, query tables.
//
// AGENT-GUARD: every relationship this stage adds goes through the
// Context (it seeds an owner's existing relationships first); a rels part
// edited directly in the part map after its owner was seeded would be
// overwritten by Context::flush.

#include "xlsx_carry_context.h"

namespace QindaOffice::Calc::IO::Carry {

struct CarryState {
    QSet<QString> keptPivots;         // "<source sheetId>|<pivot name>"
    QSet<QString> keptCacheParts;     // source cache definitions a kept pivot table uses
    QSet<QString> keptSlicerCaches;   // names of the carried slicer and timeline caches
    QHash<QString, QString> sourceToDestTable; // source table part → written table part
};

void carryPivotTables(Context &ctx, CarryState &state);
void carryWorkbookParts(Context &ctx, CarryState &state);
[[nodiscard]] bool carrySheets(Context &ctx, CarryState &state, int *drawingPartCount, QString *error);

// xlsx_carry_drawing.cpp, called by carrySheets for one sheet: the drawing
// objects the engine does not model and the legacy-drawing shapes of its
// controls and OLE objects (renumbered into a fresh block of shape ids;
// `shapeIds` receives old → new for the <controls>/<oleObjects> elements).
[[nodiscard]] bool carrySheetDrawings(Context &ctx, const DestSheet &dest, int *drawingPartCount,
                                      QHash<QString, QString> *shapeIds, QString *error);

// The source part a relationship of `owner` with this kind leads to (first
// one), or every such part.
[[nodiscard]] QString sourceTarget(const Ooxml::Package &source, const QString &owner, const QString &kind);
[[nodiscard]] QStringList sourceTargets(const Ooxml::Package &source, const QString &owner, const QString &kind);

} // namespace QindaOffice::Calc::IO::Carry
