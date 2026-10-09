// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to payroll_records.cpp and payroll_records_json.cpp: how the
// payroll records' JSON columns (`w4`, `dr0004`, `deductions`, `custom`)
// are written and read, and the audit view of an employee. Not part of
// the engine's API.

#include "payroll_records.h"

#include <QJsonObject>
#include <QString>
#include <QVariant>

namespace QindaOffice::Books::PayrollRecordsJson {

[[nodiscard]] QString compact(const QJsonObject &o);
[[nodiscard]] Decimal decimalFrom(const QVariant &v);
[[nodiscard]] QJsonObject objectFrom(const QVariant &text);
[[nodiscard]] QJsonObject w4Json(const Payroll::W4Certificate &w);
[[nodiscard]] Payroll::W4Certificate w4From(const QJsonObject &o, const QString &cur);
[[nodiscard]] QJsonObject dr0004Json(const Payroll::ColoradoCertificate &c);
[[nodiscard]] Payroll::ColoradoCertificate dr0004From(const QJsonObject &o, const QString &cur);
[[nodiscard]] QJsonObject deductionsJson(const PayDeductions &d);
[[nodiscard]] PayDeductions deductionsFrom(const QJsonObject &o, const QString &cur);
// {"exempt": [ids], "withholding": {"<id>": {"percent": "0.04", "extra": "5.00"}}}
[[nodiscard]] QJsonObject customJson(const EmployeePayroll &e);
void customFrom(const QJsonObject &o, const QString &cur, EmployeePayroll *e);
// Everything but the SSN's digits.
[[nodiscard]] QString employeeAuditJson(const EmployeePayroll &e);

} // namespace QindaOffice::Books::PayrollRecordsJson
