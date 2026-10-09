// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PivotTables and their caches in the crash-recovery payload (M74, D-083):
// the model in JSON — every setting, the cache's items and records, the
// file ties (source parts, fingerprints, extensions) a later save needs to
// write untouched pivots back byte for byte. The derived layout is not
// stored: a restore lays every pivot out again (rebuildPivotRenders).

#include "workbook.h"

#include <QJsonArray>
#include <QList>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

[[nodiscard]] QJsonArray pivotsToJson(const QList<CalcEng::PivotTable> &pivots);
[[nodiscard]] QList<CalcEng::PivotTable> pivotsFromJson(const QJsonArray &array);
[[nodiscard]] QJsonArray pivotCachesToJson(const QList<CalcEng::PivotCache> &caches);
[[nodiscard]] QList<CalcEng::PivotCache> pivotCachesFromJson(const QJsonArray &array);

} // namespace QindaOffice::Apps::QindaCalc
