// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Reports: pure functions over the ledger, each returning a ReportTable
// (docs/books.md "Reports" lists every report, its rows and its
// definition). Nothing here writes to the company file.
//
// Basis: reports that recognise income/expense (P&L, Balance Sheet, Trial
// Balance, Cash Flow, sales/expense breakdowns, Budget vs Actual) honour
// `basis` (default: the company setting) through LedgerQuery. Listings of
// what was posted (General Ledger, Journal, Transaction List, aging, open
// invoices/bills, sales tax liability) are accrual by nature.
//
// Row keys are stable (AGENT-CONTRACT with the app's drill-down and the
// tests): "section:<name>", "account:<id>", "subtotal:account:<id>",
// "total:<name>", "party:<id>", "item:<id>", "doc:<id>", "txn:<id>"; the
// M68 time reports nest them ("party:<id>:item:<id>",
// "employee:<id>:party:<id>", "vendor:<id>:party:<id>", "time:<id>").

#include "books_types.h"
#include "report_table.h"

#include <QDate>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

enum class Comparison { None, PriorPeriod, PriorYear };
enum class PnlColumns { Total, Months, Classes };

struct PnlOptions {
    QDate from;
    QDate to;
    std::optional<Basis> basis;
    Comparison comparison = Comparison::None;
    PnlColumns columns = PnlColumns::Total;
};

struct BalanceSheetOptions {
    QDate asOf;
    std::optional<Basis> basis;
    Comparison comparison = Comparison::None; // PriorYear: a year earlier; PriorPeriod: previous month end
};

class Reports {
public:
    explicit Reports(CompanyFile &file);

    // ---- financial statements ----
    [[nodiscard]] ReportTable profitAndLoss(const PnlOptions &options) const;
    [[nodiscard]] ReportTable balanceSheet(const BalanceSheetOptions &options) const;
    [[nodiscard]] ReportTable cashFlow(const QDate &from, const QDate &to,
                                       std::optional<Basis> basis = {}) const;
    [[nodiscard]] ReportTable trialBalance(const QDate &asOf, std::optional<Basis> basis = {}) const;

    // ---- ledger listings (accrual, as posted) ----
    [[nodiscard]] ReportTable generalLedger(const QDate &from, const QDate &to, qint64 accountId = 0) const;
    [[nodiscard]] ReportTable journal(const QDate &from, const QDate &to) const;
    [[nodiscard]] ReportTable transactionList(const QDate &from, const QDate &to) const;

    // ---- receivables / payables ----
    [[nodiscard]] ReportTable arAgingSummary(const QDate &asOf) const;
    [[nodiscard]] ReportTable arAgingDetail(const QDate &asOf) const;
    [[nodiscard]] ReportTable apAgingSummary(const QDate &asOf) const;
    [[nodiscard]] ReportTable apAgingDetail(const QDate &asOf) const;
    [[nodiscard]] ReportTable customerBalances(const QDate &asOf) const;
    [[nodiscard]] ReportTable vendorBalances(const QDate &asOf) const;
    [[nodiscard]] ReportTable openInvoices(const QDate &asOf) const;
    [[nodiscard]] ReportTable unpaidBills(const QDate &asOf) const;

    // ---- sales / expenses / tax / budget ----
    [[nodiscard]] ReportTable salesByCustomer(const QDate &from, const QDate &to,
                                              std::optional<Basis> basis = {}) const;
    [[nodiscard]] ReportTable salesByItem(const QDate &from, const QDate &to,
                                          std::optional<Basis> basis = {}) const;
    [[nodiscard]] ReportTable expensesByVendor(const QDate &from, const QDate &to,
                                               std::optional<Basis> basis = {}) const;
    [[nodiscard]] ReportTable expensesByCategory(const QDate &from, const QDate &to,
                                                 std::optional<Basis> basis = {}) const;
    [[nodiscard]] ReportTable salesTaxLiability(const QDate &from, const QDate &to) const;
    [[nodiscard]] ReportTable budgetVsActual(const QDate &from, const QDate &to,
                                             std::optional<Basis> basis = {}) const;
    [[nodiscard]] ReportTable reconciliation(qint64 sessionId) const;

    // ---- purchases and time (M68, D-077; reports_orders_time.cpp) ----
    // Open purchase orders by vendor: every order neither void nor closed
    // with something left to receive as of `asOf`, its amount and its open
    // balance (what is still to receive, at the order's prices).
    [[nodiscard]] ReportTable openPurchaseOrders(const QDate &asOf) const;
    // Durations (h:mm, whole minutes) per customer:job and service item,
    // and per worker (employee or vendor) and customer:job, for time dated
    // in the period; time without a customer is "No customer:job".
    [[nodiscard]] ReportTable timeByJob(const QDate &from, const QDate &to) const;
    [[nodiscard]] ReportTable timeByName(const QDate &from, const QDate &to) const;
    // Billable time not on an invoice, per customer:job: date, name,
    // service, notes, duration, the rate it bills at (TimeTracking::rateFor)
    // and what it would bill (rate × hours to two decimals, half-up — what
    // the invoice line will say).
    [[nodiscard]] ReportTable unbilledTimeByJob(const QDate &from, const QDate &to) const;

    // Every report id with its title, for the app's report list.
    struct Catalog {
        QString id;
        QString title;
        QString group; // "Business overview", "Who owes you", …
        QString definition;
    };
    [[nodiscard]] static QList<Catalog> catalog();

private:
    [[nodiscard]] Basis basisOr(std::optional<Basis> basis) const;
    [[nodiscard]] ReportTable aging(const QDate &asOf, bool receivable, bool detail) const;
    [[nodiscard]] ReportTable openDocuments(const QDate &asOf, bool receivable) const;
    [[nodiscard]] ReportTable byParty(const QDate &from, const QDate &to, Basis basis, bool sales) const;
    CompanyFile &m_file;
};

// Monetary budget per account per month (schema v2 `budgets`).
class Budgets {
public:
    explicit Budgets(CompanyFile &file);
    Result set(qint64 accountId, int year, int month, qint64 amountMinor, qint64 classId = 0);
    // Spreads an annual amount evenly across 12 months starting at the
    // fiscal-year start (allocation keeps the total exact).
    Result setAnnual(qint64 accountId, int fiscalYear, qint64 amountMinor, qint64 classId = 0);
    [[nodiscard]] qint64 amount(qint64 accountId, int year, int month, qint64 classId = 0) const;
    // The 12 months of fiscal year `fiscalYear` (it starts in that calendar
    // year at the fiscal start month), first month first; writing them is
    // one transaction and one changed() (the app's budget editor, M63).
    [[nodiscard]] QList<qint64> fiscalYearMonths(qint64 accountId, int fiscalYear, qint64 classId = 0) const;
    Result setFiscalYearMonths(qint64 accountId, int fiscalYear, const QList<qint64> &months, qint64 classId = 0);
    // Σ budget of an account over the months touching [from, to].
    [[nodiscard]] qint64 total(qint64 accountId, const QDate &from, const QDate &to, qint64 classId = 0) const;

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
