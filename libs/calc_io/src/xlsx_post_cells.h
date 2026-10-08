// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Post-pass work inside one worksheet part (M61, D-069): giving cells the
// type QXlsx could not write, and the legacy VML drawing comments need.

#include "xlsx_manifest.h"

#include <QByteArray>
#include <QHash>
#include <QString>

namespace QindaOffice::Calc::IO {

// Rewrites the t attribute and <v> of each listed cell (A1 reference ->
// real type and value) in a QXlsx-written sheet part, keeping its style and
// formula, in one pass. Returns false when a listed cell is missing — the
// caller then fails the save rather than writing a wrong type silently.
[[nodiscard]] bool retypeCells(QByteArray *sheetXml, const QHash<QString, PatchManifest::CellType> &cells);

// Adds the sheet's legacy VML drawing for its notes: the vmlDrawing part,
// its content type and relationship, and the sheet's <legacyDrawing> at its
// schema position. Excel draws a note only from this drawing; without it
// the comments load but cannot be seen.
[[nodiscard]] bool addCommentsVml(QHash<QString, QByteArray> *parts, const PatchManifest::SheetPatch &patch,
                                  QString *error);

} // namespace QindaOffice::Calc::IO
