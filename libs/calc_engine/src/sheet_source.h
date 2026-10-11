// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What ties a sheet to the package it was opened from (M66, D-075, D-068
// rule 2). A save carries the parts the engine does not model — pivot
// tables, slicers, controls, threaded comments, shapes — from that package
// (WorkbookSource::carried), and needs to know two things about each sheet:
// which source worksheet part it IS (a rename or a move does not change
// that; a duplicate is a new sheet with no source), and how its rows and
// columns moved since the load, so a pivot table, a shape or a comment
// thread lands where its cells went. The engine records every structural
// edit of a sourced sheet here (structure.cpp); calc_io maps source
// positions through the steps at save time.
//
// AGENT-CONTRACT: calc_io's carry and legacy-print readers consume this;
// structure.cpp is the only writer of `steps`. Undo snapshots and the
// recovery payload carry the whole struct, so an undone insert un-moves
// the carried parts too.

#include "value.h"

#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Calc {

struct SourceStep {
    bool rows = true;   // rows or columns
    bool insert = true; // inserted or removed
    qint32 at = 0;
    qint32 count = 0;
    bool operator==(const SourceStep &) const = default;
};

struct SheetSource {
    QString part;     // "xl/worksheets/sheet2.xml"; empty: the sheet is not from a package
    QString codeName; // <sheetPr codeName>: the VBA project's name for the sheet
    QList<SourceStep> steps; // structural edits since the load, oldest first
    // The top-level objects of the source drawing (their positions among
    // its children) the engine took in as charts and pictures; every other
    // object of that drawing is carried (calc_io decides both).
    QList<qint32> modelledObjects;
    bool operator==(const SheetSource &) const = default;
};

// Where a source row (rows = true) or column is now; nullopt when it was
// removed.
[[nodiscard]] std::optional<qint32> mapSourceLine(const QList<SourceStep> &steps, bool rows, qint32 index);
[[nodiscard]] std::optional<CellAddr> mapSourceCell(const QList<SourceStep> &steps, const CellAddr &addr);
// A range the way a formula reference moves: it grows when lines are
// inserted inside it, shrinks when some of its lines are removed, and is
// gone (nullopt) when all of them are.
[[nodiscard]] std::optional<CellRange> mapSourceRange(const QList<SourceStep> &steps, const CellRange &range);

} // namespace QindaOffice::Calc
