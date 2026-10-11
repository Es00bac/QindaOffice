// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The M90 (D-098) part of the crash-recovery payload: a sheet's sparkline
// groups (calc_engine's sparkline.h). calc_recovery.cpp owns the envelope.
//
// AGENT-CONTRACT (D-012): what the payload leaves out, a recovered workbook
// loses — every field a save writes is carried. An absent key (an older
// payload) reads as "no sparklines".

#include "sparkline.h"

#include <QJsonArray>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

[[nodiscard]] QJsonArray sparklineGroupsToJson(const QList<CalcEng::SparklineGroup> &groups);
[[nodiscard]] QList<CalcEng::SparklineGroup> sparklineGroupsFromJson(const QJsonArray &array);

} // namespace QindaOffice::Apps::QindaCalc
