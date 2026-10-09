// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The M82 (D-091) part of the crash-recovery payload: Excel's Custom Views
// (calc_engine's custom_view.h) — the workbook's list of views and each
// sheet's part of them. calc_recovery.cpp owns the envelope.
//
// AGENT-CONTRACT (D-012): what the payload leaves out, a recovered
// workbook loses — every field a save writes is carried. Absent keys (an
// older payload) read as "no views".

#include "custom_view.h"
#include "sheet.h"

#include <QJsonArray>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

[[nodiscard]] QJsonArray customViewSpecsToJson(const QList<CalcEng::CustomViewSpec> &views);
[[nodiscard]] QList<CalcEng::CustomViewSpec> customViewSpecsFromJson(const QJsonArray &array);

[[nodiscard]] QJsonArray sheetCustomViewsToJson(const QList<CalcEng::Sheet::CustomView> &parts);
[[nodiscard]] QList<CalcEng::Sheet::CustomView> sheetCustomViewsFromJson(const QJsonArray &array);

} // namespace QindaOffice::Apps::QindaCalc
