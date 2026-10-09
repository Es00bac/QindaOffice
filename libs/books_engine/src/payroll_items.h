// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A company's own payroll tax items (D-081 amendment; docs/books.md
// "Payroll tax items"): the payroll taxes of any state or locality that
// QindaBooks has no built-in tables for. Federal taxes are built in, and
// Colorado is the one state with built-in tables (payroll_records.h
// `PayrollSetup::colorado`); everything else a company defines here — a
// name, the employee's or the employer's side, a flat percentage, an
// optional wage base and annual limit, the liability account, the agency
// and the due schedule — or, for state income tax withholding with no
// built-in table, an item whose rate each employee carries (a flat
// percentage and/or a fixed extra amount per paycheck, user-entered).
//
// The arithmetic is books_payroll's computeCustomItem; this module stores
// the items (`payroll_items`, schema v5), what each paycheck took
// (`paycheck_items`, immutable like `paychecks`), the year to date, and
// each item's filing period and due date for the liabilities.
//
// AGENT-CONTRACT (liabilities, payroll_liabilities.h): an item's group key
// is "custom:<id>"; its share is credited to the item's own liability
// account, which cannot change once a paycheck used the item.

#include "books_types.h"
#include "decimal.h"
#include "money.h"

#include "custom_items.h"
#include "payroll_calendar.h"

#include <QDate>
#include <QList>
#include <QMap>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;
struct EmployeePayroll;
struct PaycheckRecord;

struct PayrollItem {
    qint64 id = 0;
    QString name;
    Payroll::CustomItem::Side side = Payroll::CustomItem::Side::Employer;
    Payroll::CustomItem::Kind kind = Payroll::CustomItem::Kind::Tax;
    Decimal rate;                      // Tax: the flat percentage as a fraction (0.027)
    std::optional<Money> wageBase;     // per employee per calendar year
    std::optional<Money> annualLimit;  // per employee per calendar year
    qint64 accountId = 0;              // its liability account; 0 on save = create one
    QString agency;                    // who it is paid to
    enum class Frequency { Monthly, Quarterly, Annually };
    Frequency frequency = Frequency::Quarterly;
    int dueDay = 0;                    // day of the month after the period; 0 = that month's last day
    bool active = true;

    [[nodiscard]] Payroll::CustomItem computation() const;
};

[[nodiscard]] QString payrollItemFrequencyKey(PayrollItem::Frequency frequency); // "monthly" | "quarterly" | "annually"
[[nodiscard]] std::optional<PayrollItem::Frequency> payrollItemFrequencyFromKey(const QString &key);

// What one employee's paychecks took for an item earlier in the year.
struct PayrollItemToDate {
    Money wages;
    Money amount;
};

class PayrollItems {
public:
    explicit PayrollItems(CompanyFile &file);

    [[nodiscard]] QList<PayrollItem> items(bool includeInactive = false) const;
    [[nodiscard]] std::optional<PayrollItem> item(qint64 id) const;
    // Validates (a unique name, a withholding item on the employee's side,
    // a percentage item's rate above zero and at most 100 %, positive
    // limits, a current-liability account, a due day 0–31) and stores;
    // with no account, creates one named after the item under "Payroll
    // Liabilities". The account cannot change once a paycheck used the
    // item. Audited. Result.id = the item.
    Result saveItem(const PayrollItem &item);
    Result setActive(qint64 id, bool active);

    // ---- per paycheck (payroll_ledger.cpp) ----
    // The active items as inputs for one employee's paycheck: exempt items
    // and withholding items the employee carries no rate for are left out.
    [[nodiscard]] QList<Payroll::CustomItemInput> inputsFor(const EmployeePayroll &employee, const QDate &payDate,
                                                            qint64 beforeDocId = 0) const;
    // The employee's live paychecks in `payDate`'s year before it, by item.
    [[nodiscard]] QMap<qint64, PayrollItemToDate> yearToDate(qint64 employeeId, const QDate &payDate,
                                                             qint64 beforeDocId = 0) const;
    // Stores what a new paycheck took (rows with wages or an amount).
    Result recordPaycheck(qint64 docId, const QList<Payroll::CustomItemResult> &results);
    // Fills each record's taxes.custom from what was stored and adds the
    // items to its employee and employer tax totals.
    void attach(QList<PaycheckRecord> *records) const;

    // The period an item's share of a paycheck paid on `payDate` belongs
    // to, and when it is due (the day after the period's month, moved to a
    // business day of `calendar`).
    [[nodiscard]] static Payroll::DuePeriod periodFor(const PayrollItem &item, const QDate &payDate,
                                                      const Payroll::BusinessCalendar &calendar);

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
