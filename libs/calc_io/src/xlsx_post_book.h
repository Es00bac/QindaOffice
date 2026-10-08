// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Post-pass work on the workbook-level parts (M61, D-069): the one
// <definedNames> block, a full recalculation request for Excel, and the
// source's theme and document properties in place of QXlsx's.

#include "xlsx_manifest.h"

#include <QByteArray>
#include <QHash>
#include <QString>

namespace QindaOffice::Calc::IO {

[[nodiscard]] bool applyWorkbookPatches(QHash<QString, QByteArray> *parts, const PatchManifest &manifest,
                                        QString *error);

} // namespace QindaOffice::Calc::IO
