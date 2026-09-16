// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Crash-recovery serialization for QindaCalc (plan section 2.7).
//
// The journal payload is the document's NATIVE snapshot, so it must carry
// everything the app models: cell inputs, the shared style table (including
// styles set on empty cells), column/row geometry, merges, named ranges and
// chart specs. It is deliberately NOT an in-memory xlsx: QXlsx has no chart
// read path, so an xlsx round trip would silently drop every chart from a
// recovered workbook.
//
// The format is the undo layer's WorkbookSnapshot (calc_undo.h) in JSON, so
// recovery and undo restore share one code path and one set of invariants.

#include "calc_undo.h"

#include <QByteArray>

namespace QindaOffice::Apps::QindaCalc {

// Current envelope version. Bump when the shape changes; older payloads are
// refused with an explicit error rather than misread.
inline constexpr int kRecoveryVersion = 1;

[[nodiscard]] QByteArray snapshotToJson(const WorkbookSnapshot &snapshot);
[[nodiscard]] bool snapshotFromJson(const QByteArray &json, WorkbookSnapshot *out,
                                    QString *error = nullptr);

} // namespace QindaOffice::Apps::QindaCalc
