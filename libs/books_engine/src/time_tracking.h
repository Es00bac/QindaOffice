// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Time tracking (M68, D-077; docs/books.md "Time tracking"): who worked how
// long for which customer:job on which service, and whether that time is
// billed. Payroll is out of scope — time never posts.
//
// A time entry (`time_entries`, schema v4): date, who did the work — an
// employee (employees.h) or a vendor, a contractor —, customer:job (0 =
// internal time), service item, duration in whole minutes (1 … 24 h),
// billable flag, notes, class.
//
// AGENT-CONTRACT (billing, with Sales and documents.h): an invoice line
// that bills an entry names it in `timeEntryId`. An entry is BILLED while a
// non-void invoice (a draft included) has such a line, so voiding the
// invoice, or editing it so the line is gone, makes the entry unbilled
// again with nothing stored. A billed entry cannot be edited or deleted.
// Sales::validate refuses an invoice line naming an entry that is not
// billable, belongs to another customer, or another invoice already bills.
//
// Hours on invoices: an invoice line bills `hours(minutes)` — minutes ÷ 60
// to two decimals, half-up (20 minutes = 0.33 h) — at `rateFor()`: the
// employee's default hourly rate when one is set, else the service item's
// sales price; the amount is rate × hours rounded once, half-up, like every
// line. The entry keeps its exact minutes; the reports total minutes.

#include "books_types.h"
#include "decimal.h"
#include "documents.h"
#include "employees.h"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

enum class TimeStatus { NotBillable, Unbilled, Billed };

// Who did the work: an employee or a vendor (a contractor) — exactly one.
struct Worker {
    EmployeeId employeeId = 0;
    qint64 vendorId = 0;

    [[nodiscard]] static Worker employee(EmployeeId id) { return Worker{id, 0}; }
    [[nodiscard]] static Worker vendor(qint64 id) { return Worker{0, id}; }
    [[nodiscard]] bool isValid() const { return (employeeId != 0) != (vendorId != 0); }
    [[nodiscard]] bool isEmpty() const { return employeeId == 0 && vendorId == 0; }
    // "employee:<id>" | "vendor:<id>" (the QML API and the report keys).
    [[nodiscard]] QString key() const;
    [[nodiscard]] static Worker fromKey(const QString &key);
    friend bool operator==(const Worker &, const Worker &) = default;
};

struct TimeEntry {
    qint64 id = 0;
    QDate date;
    Worker worker;
    qint64 customerId = 0; // customer:job; 0 = internal time
    qint64 itemId = 0;     // the service it bills as
    int minutes = 0;       // 1 … 1440
    bool billable = false;
    QString notes;
    qint64 classId = 0;
    // Read back only: the non-void invoice whose line bills it (0 = none).
    qint64 invoiceId = 0;

    [[nodiscard]] TimeStatus status() const
    {
        return invoiceId != 0 ? TimeStatus::Billed : billable ? TimeStatus::Unbilled : TimeStatus::NotBillable;
    }
};

struct TimeFilter {
    Worker worker;         // empty = everyone
    qint64 customerId = 0;
    QDate from;
    QDate to;
    std::optional<TimeStatus> status;
};

// One row of a weekly timesheet: what the time is for, and seven days
// (week start first). A cell names the entry it shows (0 = none yet).
struct TimesheetCell {
    qint64 entryId = 0;
    int minutes = 0;
    bool billed = false; // read back only
};

struct TimesheetRow {
    qint64 customerId = 0;
    qint64 itemId = 0;
    bool billable = false;
    QString notes;
    qint64 classId = 0;
    QList<TimesheetCell> days; // exactly 7
};

class TimeTracking {
public:
    explicit TimeTracking(CompanyFile &file);

    Result create(TimeEntry entry);
    // Refused while the entry is billed (time_tracking.h contract).
    Result update(const TimeEntry &entry);
    Result remove(qint64 id);
    [[nodiscard]] std::optional<TimeEntry> entry(qint64 id) const;
    // Oldest first (date, then id).
    [[nodiscard]] QList<TimeEntry> entries(const TimeFilter &filter = {}) const;
    // A customer's billable, unbilled entries, oldest first.
    [[nodiscard]] QList<TimeEntry> unbilled(qint64 customerId) const;

    // ---- the weekly timesheet (time_sheet.cpp) ----
    // The worker's entries of the seven days from `weekStart`, one row per
    // (customer, item, billable, notes, class); a second entry for the same
    // row and day opens another row, so no cell ever hides two entries.
    [[nodiscard]] QList<TimesheetRow> week(const Worker &worker, const QDate &weekStart) const;
    // Writes the week in ONE SQL transaction: a cell with an entry and 0
    // minutes deletes it, with minutes it updates it, without an entry it
    // creates one; entries the rows do not name are left alone. Any refusal
    // (a billed entry changed, an entry of another worker or week) writes
    // nothing.
    Result saveWeek(const Worker &worker, const QDate &weekStart, const QList<TimesheetRow> &rows);

    // ---- billing ----
    // Invoice lines for `entryIds` (each billable, unbilled, the
    // customer's): the item, hours as the quantity, rateFor() as the rate,
    // the notes (else the item's description) as the description.
    [[nodiscard]] std::optional<QList<DocumentLine>> invoiceLines(qint64 customerId, const QList<qint64> &entryIds,
                                                                  QString *error = nullptr) const;
    // Engine-internal (Sales::validate): the invoice's time lines are allowed.
    [[nodiscard]] Result checkInvoiceLines(const BusinessDocument &invoice) const;
    // What an hour of the entry bills at: the employee's default rate, else
    // the item's price (zero without either).
    [[nodiscard]] Money rateFor(const TimeEntry &entry) const;
    // The worker's display name ("" when unknown).
    [[nodiscard]] QString workerName(const Worker &worker) const;

    // ---- durations ----
    [[nodiscard]] static Decimal hours(int minutes);          // 2 decimals, half-up
    [[nodiscard]] static QString durationText(qint64 minutes); // "1:05", "-0:30"
    // "1:30", "1.5" or "90m"; nullopt when it does not read or is ≤ 0.
    [[nodiscard]] static std::optional<int> parseDuration(const QString &text);
    // Monday of the week that holds `date`.
    [[nodiscard]] static QDate weekStartOf(const QDate &date);

private:
    Result validate(TimeEntry &entry) const;
    Result writeEntry(TimeEntry &entry, bool isNew);
    [[nodiscard]] qint64 billedBy(qint64 entryId, qint64 exceptInvoice = 0) const;
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
