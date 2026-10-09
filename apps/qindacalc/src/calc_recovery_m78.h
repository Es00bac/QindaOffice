// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The M78 (D-087) parts of the crash-recovery payload: the whole Page
// Setup, scenarios, data tables, the rules kept as Excel wrote them, and
// the fields conditional formats and validations gained. calc_recovery.cpp
// owns the envelope, calc_recovery_rules.cpp the rule shapes it extends
// through the two *Extras pairs here.
//
// AGENT-CONTRACT (D-012): what the payload leaves out, a recovered
// workbook loses — every field a save writes is carried. Absent keys (an
// older payload) read as the model's defaults.

#include "sheet.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace CalcEng = QindaOffice::Calc;
namespace QindaOffice::Apps::QindaCalc {

[[nodiscard]] QJsonObject pageSetupToJson(const CalcEng::Sheet::PageSetup &setup);
// Reads the M78 object, or the M37 array [orientation, fitToWidth, fitToHeight].
[[nodiscard]] CalcEng::Sheet::PageSetup pageSetupFromJson(const QJsonValue &value);

[[nodiscard]] QJsonObject scenariosToJson(const CalcEng::ScenarioSet &set);
[[nodiscard]] CalcEng::ScenarioSet scenariosFromJson(const QJsonObject &object);

[[nodiscard]] QJsonArray dataTablesToJson(const QList<CalcEng::DataTableSpec> &tables);
[[nodiscard]] QList<CalcEng::DataTableSpec> dataTablesFromJson(const QJsonArray &array);

[[nodiscard]] QJsonArray keptRulesToJson(const QList<CalcEng::KeptRule> &rules);
[[nodiscard]] QList<CalcEng::KeptRule> keptRulesFromJson(const QJsonArray &array);

// The fields conditional formats and validations gained in M78, added to
// (and read from) the object calc_recovery_rules.cpp writes for each rule.
void condRuleExtrasToJson(const CalcEng::CondFormatRule &rule, QJsonObject *out);
void condRuleExtrasFromJson(const QJsonObject &in, CalcEng::CondFormatRule *rule);
void validationExtrasToJson(const CalcEng::ValidationRule &rule, QJsonObject *out);
void validationExtrasFromJson(const QJsonObject &in, CalcEng::ValidationRule *rule);

} // namespace QindaOffice::Apps::QindaCalc
