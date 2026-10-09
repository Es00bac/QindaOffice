// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Payroll test helpers (M71): a company set up for payroll — a Colorado
// S corporation with fake identity numbers (EIN 00-0000000, SSN
// 900-00-0001) — and one salaried owner-employee working in Denver,
// $78,000 a year paid biweekly ($3,000.00 a check).

#include "books_fixture.h"

#include "employees.h"
#include "payroll_ledger.h"
#include "payroll_records.h"

namespace BooksTest {

struct PayrollCompany : TestCompany {
    qint64 bank = 0;
    qint64 jane = 0;

    PayrollCompany()
    {
        bank = account(QStringLiteral("1000"));
        PayrollRecords records(f());
        PayrollSetup s = records.setup();
        s.entity = EntityTax::SCorporation;
        s.ein = QStringLiteral("00-0000000");
        s.payrollBankAccountId = bank;
        s.optCity = QStringLiteral("denver");
        s.colorado = true; // the test company pays Colorado's taxes
        records.saveSetup(s);
        records.ensurePayrollAccounts();
        Employee e;
        e.name = QStringLiteral("Jane Doe");
        jane = Employees(f()).create(e).id;
        EmployeePayroll p = records.employee(jane);
        p.firstName = QStringLiteral("Jane");
        p.lastName = QStringLiteral("Doe");
        p.ssn = QStringLiteral("900-00-0001");
        p.street = QStringLiteral("1 Elm Street");
        p.city = QStringLiteral("Denver");
        p.state = QStringLiteral("CO");
        p.zip = QStringLiteral("80202");
        p.payRate = Decimal(78000, 0);
        p.frequency = Payroll::PayFrequency::Biweekly;
        p.ownerEmployee = true;
        records.saveEmployee(p);
    }
    PayrollSetup setup() { return PayrollRecords(f()).setup(); }
    qint64 payrollAccount(const QString &role) { return setup().account(role); }
    qint64 roleBalance(const QString &role, const QDate &asOf = {})
    {
        return ChartOfAccounts(f()).balance(payrollAccount(role), asOf).minor();
    }
    PaycheckRequest request(const QDate &payDate, qint64 employee = 0) const
    {
        PaycheckRequest r;
        r.employeeId = employee != 0 ? employee : jane;
        r.payDate = payDate;
        r.periodStart = payDate.addDays(-13);
        r.periodEnd = payDate;
        return r;
    }
    qint64 pay(const QDate &payDate)
    {
        return PayrollLedger(f()).createPaycheck(request(payDate)).id;
    }
};

} // namespace BooksTest
