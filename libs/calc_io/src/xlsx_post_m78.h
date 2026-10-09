// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Post-pass work for a sheet's M78 parts (D-087): scenarios, conditional
// formats, validations, print settings and the worksheet <extLst> entries
// at their schema positions; the <f> elements data tables put in cells; and
// the <dxfs> element of xl/styles.xml. The fragments come ready-made from
// xlsx_rules.h, xlsx_print.h and xlsx_whatif.h; this only places them, and
// fails the save when an anchor is missing rather than writing a broken
// package (D-012).

#include "xlsx_manifest.h"

#include <QByteArray>
#include <QHash>
#include <QString>

namespace QindaOffice::Calc::IO {

[[nodiscard]] bool applyM78SheetPatch(QByteArray *sheetXml, const PatchManifest::SheetPatch &patch, QString *error);

// Replaces the <dxfs> element QXlsx wrote (M78: the manifest's own, whose
// first entries are the source package's dxfs as read). No-op for an empty
// `dxfsXml`.
[[nodiscard]] bool applyDxfs(QByteArray *stylesXml, const QByteArray &dxfsXml);

} // namespace QindaOffice::Calc::IO
