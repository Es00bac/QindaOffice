// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The parts QindaCalc does not model, carried through a save (M66, D-075;
// the D-068 rule 2 the Word mapper follows): pivot tables and their caches
// (marked refreshOnLoad, so Excel recomputes them from the edited data),
// slicers and timelines with their caches, query tables and connections,
// external links, the data model, custom XML, threaded comments and their
// persons, form and ActiveX controls, OLE objects, printer settings and
// page setup, sheet backgrounds, the drawing objects the engine does not
// model (shapes, text boxes, the slicer and timeline frames), and a
// macro-enabled workbook's VBA project (kept, never run).
//
// Read side: buildCarriedPackage keeps a reduced copy of the source
// package (WorkbookSource::carried — the workbook and sheet parts with
// their cell data stripped, and every part a save may carry) and says what
// the file holds that no save can keep.
//
// Save side: applyCarry copies from that copy into the package being
// written, through office_ooxml's RelationshipCopier (fresh relationship
// ids, every referenced part with it), and wires each carried part back to
// the sheet or workbook part that named it — only while that sheet still
// exists, at the place its cells moved to (SheetSource's journal). A part
// that cannot be carried safely stays behind and the save says so; the
// package is never written half-wired.
//
// AGENT-CONTRACT: the reader (xlsx_read.cpp) sets each sheet's SheetSource
// and WorkbookSource::carried; the writer (xlsx_write.cpp, xlsx_post.cpp)
// hands the SheetSource of every sheet and the written table parts to
// applyCarry, which runs last in the post-pass, after every part QXlsx and
// the post-pass write exists.

#include "xlsx_manifest.h"

#include <QByteArray>
#include <QHash>
#include <QPair>
#include <QStringList>

namespace QindaOffice::Calc::IO {

[[nodiscard]] QByteArray buildCarriedPackage(const QString &packagePath, const Workbook &wb, QStringList *warnings);

// `tableSources`: each table part the post-pass wrote → the source table
// part it came from (query tables hang from it). `drawingPartCount` is the
// post-pass's running drawing counter (a sheet that gains a drawing for a
// carried shape takes the next number).
[[nodiscard]] bool applyCarry(QHash<QString, QByteArray> *parts, const PatchManifest &manifest,
                              const QList<QPair<QString, QString>> &tableSources, int *drawingPartCount,
                              QString *error);

} // namespace QindaOffice::Calc::IO
