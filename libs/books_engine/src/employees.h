// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Employees (M68, D-077; docs/books.md "Employees"): the people the
// business enters time for. A minimal list — name, contact fields, active,
// and an optional default hourly BILLING rate that billable time bills at
// instead of the service item's price. Contractors are vendors (lists.h);
// a time entry names one or the other (time_tracking.h).
//
// AGENT-CONTRACT (payroll, M71): payroll extends THIS list — the
// `employees` table and EmployeeId — in its own schema version (pay rates,
// tax setup, identity numbers live there, never here). Keep this record
// free of pay and tax data, and never reuse `defaultRate` as a pay rate.
//
// An employee is never deleted (time names them); deactivate instead. Names
// are unique among employees, compared case-insensitively.

#include "books_types.h"
#include "money.h"

#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

using EmployeeId = qint64;

struct Employee {
    EmployeeId id = 0;
    QString name;
    QString email;
    QString phone;
    QString address;                  // postal, multi-line
    QString notes;
    std::optional<Money> defaultRate; // per hour, for billing; none = the item's price
    bool active = true;
};

class Employees {
public:
    explicit Employees(CompanyFile &file);

    // Audited. Result.id = the employee.
    Result create(const Employee &employee);
    Result update(const Employee &employee);
    Result setActive(EmployeeId id, bool active);
    [[nodiscard]] std::optional<Employee> employee(EmployeeId id) const;
    // By name.
    [[nodiscard]] QList<Employee> employees(bool includeInactive = false) const;
    [[nodiscard]] EmployeeId find(const QString &name) const;

private:
    Result validate(const Employee &employee) const;
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
