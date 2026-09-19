// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The post-pass half that writes pictures (M39, D-040). QXlsx DOES have
// Document::insertImage, but it re-encodes every image as PNG and derives
// the on-sheet size from the QImage's DPI — it drops the author's format,
// inflates the package (a JPEG photo becomes a lossless PNG several times
// its size) and forgets the anchor's extent. By the write-path rule (D-035)
// that is "QXlsx drops semantics", so pictures go through the package like
// tables and comments do: the file's own bytes into xl/media, a rel from the
// sheet's drawing, and an <xdr:pic> anchor.

#include "xlsx_manifest.h"

namespace QindaOffice::Calc::IO {

// Ensures the sheet has a drawing part, creating it (plus its content-type
// override, its sheet relationship and the sheet's <drawing> element) when
// the sheet has none. `drawingPartCount` is the running count used to name
// a new part. Returns false with `error` set when an anchor is missing.
[[nodiscard]] bool ensureDrawingPart(QHash<QString, QByteArray> *parts, const QString &sheetPart,
                                     int *drawingPartCount, QString *drawingPart, QString *error);

// Writes every image patch: media part, drawing relationship and anchor.
// `mediaCount` is the running media-part counter across the package.
[[nodiscard]] bool applyImagePatches(QHash<QString, QByteArray> *parts,
                                     const QList<PatchManifest::ImagePatch> &images,
                                     int *drawingPartCount, int *mediaCount, QString *error);

} // namespace QindaOffice::Calc::IO
