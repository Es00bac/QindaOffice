// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// JSON for the per-sheet rules carried by the crash-recovery payload
// (calc_recovery.h owns the envelope; this owns the rule shapes).

#include "sheet_rules.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

[[nodiscard]] QJsonObject condFormatsToJson(const QList<CalcEng::CondFormatRule> &rules);
[[nodiscard]] QList<CalcEng::CondFormatRule> condFormatsFromJson(const QJsonObject &object);

[[nodiscard]] QJsonObject validationsToJson(const QList<CalcEng::ValidationRule> &rules);
[[nodiscard]] QList<CalcEng::ValidationRule> validationsFromJson(const QJsonObject &object);

[[nodiscard]] QJsonObject autoFilterToJson(const CalcEng::AutoFilter &filter);
[[nodiscard]] CalcEng::AutoFilter autoFilterFromJson(const QJsonObject &object);

[[nodiscard]] QJsonArray commentsToJson(const QHash<quint64, CalcEng::CellComment> &comments);
[[nodiscard]] QHash<quint64, CalcEng::CellComment> commentsFromJson(const QJsonArray &array);

[[nodiscard]] QJsonArray tablesToJson(const QList<CalcEng::TableSpec> &tables);
[[nodiscard]] QList<CalcEng::TableSpec> tablesFromJson(const QJsonArray &array);

} // namespace QindaOffice::Apps::QindaCalc
