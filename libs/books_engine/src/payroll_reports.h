// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Payroll reports and tax-form worksheets (M71, D-081; docs/books.md
// "Payroll reports and forms"): every one a ReportTable, so the app shows
// it, books_io prints it to PDF and paper and it exports to CSV and Excel
// like every other report — no form is drawn by hand.
//
// Reports (QuickBooks' names): Payroll Summary, Payroll Liability
// Balances, Employee Earnings Summary, Paycheck History.
// Worksheets: Form 941 (with Schedule B's daily liabilities for a
// semiweekly depositor), Form 940, W-2 for each employee and the W-3
// totals, Colorado DR 1094 for a filing period and DR 1093 for the year,
// the Colorado UI and FAMLI quarterly wage reports.
//
// AGENT-GUARD (identity numbers): forms that require an SSN or EIN (W-2,
// W-3, the UI and FAMLI wage reports, EFW2) show it in full only when the
// caller asks with `revealIds`; every other report shows the last four.
//
// AGENT-NOTE (no e-filing): QindaBooks prepares the numbers. The owner
// files and pays through EFTPS, the SSA's Business Services Online,
// Colorado Revenue Online, MyUI Employer+ and My FAMLI+ Employer.

#include "report_table.h"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

class PayrollReports {
public:
    explicit PayrollReports(CompanyFile &file);

    // ---- reports ----
    // Payroll items down, each employee and the total across.
    [[nodiscard]] ReportTable payrollSummary(const QDate &from, const QDate &to) const;
    // Each liability group and its accounts: accrued, paid, balance.
    [[nodiscard]] ReportTable liabilityBalances(const QDate &from, const QDate &to) const;
    // One row per employee: gross, deductions, each tax, net, employer taxes.
    [[nodiscard]] ReportTable employeeEarnings(const QDate &from, const QDate &to) const;
    // One row per paycheck (void ones marked).
    [[nodiscard]] ReportTable paycheckHistory(const QDate &from, const QDate &to, qint64 employeeId = 0) const;

    // ---- form worksheets (payroll_forms_federal.cpp, _w2.cpp, _colorado.cpp) ----
    [[nodiscard]] ReportTable form941(int year, int quarter) const;
    [[nodiscard]] ReportTable schedule941B(int year, int quarter) const;
    [[nodiscard]] ReportTable form940(int year) const;
    // One employee, or every employee paid in the year (employeeId 0), each
    // under its own heading.
    [[nodiscard]] ReportTable formW2(int year, qint64 employeeId = 0, bool revealIds = false) const;
    [[nodiscard]] ReportTable formW3(int year, bool revealIds = false) const;
    [[nodiscard]] ReportTable dr1094(const QDate &start, const QDate &end) const;
    [[nodiscard]] ReportTable dr1093(int year) const;
    [[nodiscard]] ReportTable uiWageReport(int year, int quarter, bool revealIds = false) const;
    [[nodiscard]] ReportTable famliWageReport(int year, int quarter, bool revealIds = false) const;

    // The report ids above, their titles and definitions for the catalog
    // (group "Payroll" and "Payroll tax forms"), and building one from a
    // period: a quarterly form takes the quarter `to` falls in, a yearly
    // one `to`'s year, DR 1094 the period itself.
    struct Entry {
        QString id;
        QString title;
        QString group;
        QString definition;
    };
    [[nodiscard]] static QList<Entry> catalog();
    [[nodiscard]] std::optional<ReportTable> build(const QString &id, const QDate &from, const QDate &to,
                                                   bool revealIds = false) const;

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
