// SPDX-License-Identifier: GPL-3.0-or-later
// Time tracking (M68, D-077): entries and durations, the weekly timesheet,
// billable time onto an invoice and off it again (void, a removed line),
// and the time reports worked by hand.
#include "books_fixture.h"

#include "employees.h"
#include "reports.h"
#include "time_tracking.h"

using namespace BooksTest;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

struct Agency : TestCompany {
    Worker maya;
    Worker jun;
    qint64 harbor = 0;
    qint64 riverbend = 0;
    qint64 design = 0;
    qint64 admin = 0;
    qint64 e1 = 0, e2 = 0, e3 = 0, e4 = 0, e5 = 0;

    Agency()
    {
        Employee employee;
        employee.name = str("Maya Okafor");
        employee.email = str("maya@lantern.test");
        maya = Worker::employee(Employees(f()).create(employee).id);
        jun = Worker::vendor(vendor(str("Jun Park (contractor)")));
        harbor = customer(str("Harbor Coffee"));
        riverbend = customer(str("Riverbend Books"));
        design = item(str("Design"), 12000, account(str("4100")));
        admin = item(str("Admin"), 0, account(str("4100")));
        e1 = add(QDate(2026, 3, 2), maya, harbor, design, 90, true, str("Logo sketches"));
        e2 = add(QDate(2026, 3, 3), maya, harbor, design, 20, true, str("Call"));
        e3 = add(QDate(2026, 3, 3), jun, harbor, design, 150, true, str("Illustration"));
        e4 = add(QDate(2026, 3, 4), maya, riverbend, design, 45, true, QString());
        e5 = add(QDate(2026, 3, 4), maya, 0, admin, 60, false, QString());
    }
    qint64 add(const QDate &date, const Worker &worker, qint64 customer, qint64 item, int minutes, bool billable,
               const QString &notes)
    {
        TimeEntry e;
        e.date = date;
        e.worker = worker;
        e.customerId = customer;
        e.itemId = item;
        e.minutes = minutes;
        e.billable = billable;
        e.notes = notes;
        const Result r = TimeTracking(f()).create(e);
        if (!r.ok) {
            qWarning("time: %s", qPrintable(r.error));
        }
        return r.id;
    }
    Result invoice(qint64 customer, const QList<qint64> &entries, DocState state = DocState::Open)
    {
        BusinessDocument inv;
        inv.partyId = customer;
        inv.date = QDate(2026, 3, 31);
        inv.state = state;
        QString error;
        const std::optional<QList<DocumentLine>> lines = TimeTracking(f()).invoiceLines(customer, entries, &error);
        if (!lines) {
            return Result::failure(ErrorCode::Invalid, error);
        }
        inv.lines = *lines;
        return Sales(f()).createInvoice(inv);
    }
    TimeStatus status(qint64 id) { return TimeTracking(f()).entry(id)->status(); }
};

} // namespace

class TstBooksTime : public QObject {
    Q_OBJECT
private slots:
    void durationsReadAndPrint()
    {
        QCOMPARE(TimeTracking::parseDuration(str("1:30")), std::optional<int>(90));
        QCOMPARE(TimeTracking::parseDuration(str("0:20")), std::optional<int>(20));
        QCOMPARE(TimeTracking::parseDuration(str("1.5")), std::optional<int>(90));
        QCOMPARE(TimeTracking::parseDuration(str("0.25")), std::optional<int>(15));
        QCOMPARE(TimeTracking::parseDuration(str("0.33")), std::optional<int>(20)); // 19.8 minutes, half-up
        QCOMPARE(TimeTracking::parseDuration(str("45m")), std::optional<int>(45));
        QCOMPARE(TimeTracking::parseDuration(str("2h")), std::optional<int>(120));
        for (const char *bad : {"1:5", "1:60", "abc", "0", "-1", "", "1:30:00"}) {
            QVERIFY2(!TimeTracking::parseDuration(str(bad)), bad);
        }
        QCOMPARE(TimeTracking::durationText(90), str("1:30"));
        QCOMPARE(TimeTracking::durationText(365), str("6:05"));
        QCOMPARE(TimeTracking::hours(20), *Decimal::parse(str("0.33")));
        QCOMPARE(TimeTracking::hours(90), *Decimal::parse(str("1.5")));
        QCOMPARE(TimeTracking::hours(50), *Decimal::parse(str("0.83")));
        QCOMPARE(TimeTracking::weekStartOf(QDate(2026, 3, 5)), QDate(2026, 3, 2)); // a Thursday → Monday
    }

    // The employees list (employees.h): minimal, unique names, an optional
    // billing rate that time bills at instead of the item's price.
    void employeesAreAMinimalList()
    {
        Agency c;
        Employees employees(c.f());
        Employee dup;
        dup.name = str("maya okafor");
        QCOMPARE(employees.create(dup).code, ErrorCode::Duplicate);
        QCOMPARE(employees.create(Employee{}).code, ErrorCode::Invalid);
        Employee lee;
        lee.name = str("Lee Brandt");
        lee.defaultRate = usd(9000);
        lee.phone = str("555-0199");
        const Result created = employees.create(lee);
        QVERIFY2(created.ok, qPrintable(created.error));
        QCOMPARE(employees.employee(created.id)->defaultRate->minor(), 9000);
        QCOMPARE(employees.employees().size(), 2);
        QCOMPARE(employees.find(str("LEE BRANDT")), created.id);
        // Lee's hour bills at 90.00, Maya's at the item's 120.00.
        const qint64 leeTime = c.add(QDate(2026, 3, 5), Worker::employee(created.id), c.harbor, c.design, 60, true, str("Proofs"));
        TimeTracking time(c.f());
        QCOMPARE(time.rateFor(*time.entry(leeTime)).minor(), 9000);
        QCOMPARE(time.rateFor(*time.entry(c.e1)).minor(), 12000);
        QCOMPARE(time.invoiceLines(c.harbor, {leeTime})->first().unitPrice.minor(), 9000);
        // Inactive: kept with its time, not offered, no new time.
        QVERIFY(employees.setActive(created.id, false).ok);
        QCOMPARE(employees.employees().size(), 1);
        QCOMPARE(employees.employees(true).size(), 2);
        QCOMPARE(c.add(QDate(2026, 3, 6), Worker::employee(created.id), c.harbor, c.design, 30, true, {}), 0);
        QVERIFY(time.entry(leeTime));
        Employee negative = *employees.employee(created.id);
        negative.defaultRate = usd(-1);
        QCOMPARE(employees.update(negative).code, ErrorCode::Invalid);
        QCOMPARE(Ledger(c.f()).auditTrail(str("employee"), created.id).size(), 2); // create, deactivate
    }

    void entriesAreValidated()
    {
        Agency c;
        QVERIFY(c.e1 && c.e2 && c.e3 && c.e4 && c.e5);
        TimeTracking time(c.f());
        TimeEntry bad = *time.entry(c.e1);
        bad.id = 0;
        bad.customerId = 0; // billable time needs a customer:job
        QCOMPARE(time.create(bad).code, ErrorCode::Invalid);
        bad.customerId = c.harbor;
        bad.minutes = 25 * 60;
        QCOMPARE(time.create(bad).code, ErrorCode::Invalid);
        bad.minutes = 30;
        bad.worker = Worker::vendor(c.harbor); // a customer is not a contractor
        QCOMPARE(time.create(bad).code, ErrorCode::UnknownParty);
        bad.worker = Worker{};                  // nobody
        QCOMPARE(time.create(bad).code, ErrorCode::UnknownParty);
        bad.worker = Worker{c.maya.employeeId, c.jun.vendorId}; // both
        QCOMPARE(time.create(bad).code, ErrorCode::UnknownParty);
        QCOMPARE(time.unbilled(c.harbor).size(), 3);
        QCOMPARE(time.entries({c.maya, 0, {}, {}, {}}).size(), 4);
        QCOMPARE(time.entries({c.jun, 0, {}, {}, {}}).size(), 1);
        QCOMPARE(time.workerName(c.maya), str("Maya Okafor"));
        QCOMPARE(Worker::fromKey(c.jun.key()), c.jun);
        QCOMPARE(c.maya.key(), str("employee:%1").arg(c.maya.employeeId));
        TimeFilter notBillable;
        notBillable.status = TimeStatus::NotBillable;
        QCOMPARE(time.entries(notBillable).size(), 1);
        QVERIFY(time.remove(c.e5).ok);
        QVERIFY(!time.entry(c.e5));
        QCOMPARE(Ledger(c.f()).auditTrail(str("time"), c.e5).size(), 2); // create, delete
    }

    void billableTimeGoesOnAnInvoiceAndComesBack()
    {
        Agency c;
        TimeTracking time(c.f());
        QString error;
        const std::optional<QList<DocumentLine>> lines = time.invoiceLines(c.harbor, {c.e1, c.e2, c.e3}, &error);
        QVERIFY2(lines, qPrintable(error));
        QCOMPARE(lines->at(0).quantity, *Decimal::parse(str("1.5")));
        QCOMPARE(lines->at(1).quantity, *Decimal::parse(str("0.33")));
        QCOMPARE(lines->at(0).unitPrice.minor(), 12000);
        QCOMPARE(lines->at(0).description, str("Logo sketches"));
        QCOMPARE(lines->at(2).timeEntryId, c.e3);
        const Result inv = c.invoice(c.harbor, {c.e1, c.e2, c.e3});
        QVERIFY2(inv.ok, qPrintable(inv.error));
        // 1.5 × 120.00 + 0.33 × 120.00 + 2.5 × 120.00 = 180.00 + 39.60 + 300.00
        QCOMPARE(Documents(c.f()).load(inv.id)->total.minor(), 51960);
        QCOMPARE(c.status(c.e1), TimeStatus::Billed);
        QCOMPARE(time.entry(c.e2)->invoiceId, inv.id);
        QVERIFY(time.unbilled(c.harbor).isEmpty());
        // Billed time cannot change, nor go on another invoice.
        TimeEntry changed = *time.entry(c.e1);
        changed.minutes = 100;
        QCOMPARE(time.update(changed).code, ErrorCode::Invalid);
        QCOMPARE(time.remove(c.e1).code, ErrorCode::Invalid);
        QVERIFY(!time.invoiceLines(c.harbor, {c.e1}, &error));
        BusinessDocument twice = *Documents(c.f()).load(inv.id);
        twice.id = 0;
        twice.number.clear();
        QCOMPARE(Sales(c.f()).createInvoice(twice).code, ErrorCode::Duplicate);
        // Another customer's time is refused on an invoice.
        BusinessDocument wrong = twice;
        wrong.partyId = c.riverbend;
        wrong.lines = {lines->at(0)};
        QCOMPARE(Sales(c.f()).createInvoice(wrong).code, ErrorCode::Invalid);

        // Removing a line un-bills its time.
        BusinessDocument edit = *Documents(c.f()).load(inv.id);
        edit.lines.removeAt(1);
        QVERIFY(Sales(c.f()).updateInvoice(edit, str("the call was free")).ok);
        QCOMPARE(c.status(c.e2), TimeStatus::Unbilled);
        QCOMPARE(Documents(c.f()).load(inv.id)->total.minor(), 48000);
        // Voiding the invoice un-bills the rest.
        QVERIFY(Ledger(c.f()).voidDocument(inv.id, str("re-issue")).ok);
        QCOMPARE(c.status(c.e1), TimeStatus::Unbilled);
        QCOMPARE(c.status(c.e3), TimeStatus::Unbilled);
        QCOMPARE(time.unbilled(c.harbor).size(), 3);
        // A draft invoice holds its time too.
        const Result draft = c.invoice(c.harbor, {c.e3}, DocState::Draft);
        QVERIFY2(draft.ok, qPrintable(draft.error));
        QCOMPARE(c.status(c.e3), TimeStatus::Billed);
        QVERIFY(c.f().integrityProblems().isEmpty());
    }

    void theWeeklyTimesheetReadsAndWritesTheWeek()
    {
        Agency c;
        TimeTracking time(c.f());
        const QDate monday(2026, 3, 2);
        QList<TimesheetRow> rows = time.week(c.maya, monday);
        // Four rows: Harbor "Logo sketches", Harbor "Call", Riverbend, Admin.
        QCOMPARE(rows.size(), 4);
        QCOMPARE(rows.at(0).notes, str("Logo sketches"));
        QCOMPARE(rows.at(0).days.at(0).entryId, c.e1);
        QCOMPARE(rows.at(0).days.at(0).minutes, 90);
        QCOMPARE(rows.at(1).days.at(1).entryId, c.e2);
        QCOMPARE(rows.at(3).billable, false);
        QCOMPARE(rows.at(3).days.at(2).minutes, 60);
        QVERIFY(time.week(c.jun, monday).size() == 1);
        QVERIFY(time.week(Worker{}, monday).isEmpty());
        // 1:30 → 2:00 on Monday, 1:00 more on Thursday, the call deleted.
        rows[0].days[0].minutes = 120;
        rows[0].days[3].minutes = 60;
        rows[1].days[1].minutes = 0;
        QVERIFY(time.saveWeek(c.maya, monday, rows).ok);
        QCOMPARE(time.entry(c.e1)->minutes, 120);
        QVERIFY(!time.entry(c.e2));
        const QList<TimesheetRow> after = time.week(c.maya, monday);
        QCOMPARE(after.size(), 3);
        QCOMPARE(after.at(0).days.at(3).minutes, 60);
        QCOMPARE(time.entry(after.at(0).days.at(3).entryId)->date, QDate(2026, 3, 5));
        QCOMPARE(time.entry(after.at(0).days.at(3).entryId)->notes, str("Logo sketches"));
        // A billed entry changed through the sheet refuses the whole save.
        QVERIFY(c.invoice(c.riverbend, {c.e4}).ok);
        QList<TimesheetRow> blocked = time.week(c.maya, monday);
        QVERIFY(blocked.at(1).days.at(2).billed);
        blocked[0].days[0].minutes = 30;   // would succeed alone
        blocked[1].days[2].minutes = 50;   // billed: refused
        const Result refused = time.saveWeek(c.maya, monday, blocked);
        QVERIFY(!refused.ok);
        QVERIFY2(refused.error.contains(str("billed")), qPrintable(refused.error));
        QCOMPARE(time.entry(c.e1)->minutes, 120); // nothing was written
        // An entry of another week cannot be smuggled in.
        QList<TimesheetRow> foreign = time.week(c.maya, monday);
        QCOMPARE(time.saveWeek(c.maya, monday.addDays(7), foreign).code, ErrorCode::Invalid);
    }

    // Worked by hand (March 2026; Jun's 2:30 billed):
    //  Time by Job — Harbor Coffee: Design 1:30 + 0:20 + 2:30 = 4:20;
    //  Riverbend Books: Design 0:45; No customer:job: Admin 1:00; total 6:05.
    //  Time by Name — Jun Park (contractor): Harbor 2:30; Maya Okafor:
    //  Harbor 1:50, Riverbend 0:45, No customer:job 1:00 = 3:35; total 6:05.
    //  Unbilled Time by Job — Harbor: 1:30 → 180.00, 0:20 → 0.33 h × 120.00
    //  = 39.60, subtotal 1:50 / 219.60; Riverbend: 0:45 → 90.00; total
    //  2:35 / 309.60.
    void timeReportsWorkedByHand()
    {
        Agency c;
        QVERIFY(c.invoice(c.harbor, {c.e3}).ok);
        Reports reports(c.f());
        const QDate from(2026, 3, 1);
        const QDate to(2026, 3, 31);
        const auto minutes = [](const ReportTable &t, const QString &key) {
            const ReportRow *r = t.row(key);
            return r != nullptr ? r->cells.value(t.columnIndex(str("duration"))).value : -1;
        };
        const ReportTable job = reports.timeByJob(from, to);
        QCOMPARE(job.columns.at(1).type, ColumnType::Duration);
        QCOMPARE(minutes(job, str("party:%1:item:%2").arg(c.harbor).arg(c.design)), 260);
        QCOMPARE(minutes(job, str("subtotal:party:%1").arg(c.riverbend)), 45);
        QCOMPARE(minutes(job, str("party:0:item:%1").arg(c.admin)), 60);
        QCOMPARE(minutes(job, str("total")), 365);
        QCOMPARE(job.text(str("total"), str("duration")), str("6:05"));
        QCOMPARE(job.rows.last().key, str("total"));
        QCOMPARE(job.rows.at(job.rows.size() - 2).key, str("subtotal:party:0")); // "No customer:job" last

        const ReportTable name = reports.timeByName(from, to);
        QCOMPARE(name.rows.first().cells.first().text, str("Jun Park (contractor)"));
        QCOMPARE(minutes(name, str("%1:party:%2").arg(c.maya.key()).arg(c.harbor)), 110);
        QCOMPARE(minutes(name, str("subtotal:%1").arg(c.maya.key())), 215);
        QCOMPARE(minutes(name, str("subtotal:%1").arg(c.jun.key())), 150);
        QCOMPARE(minutes(name, str("total")), 365);

        const ReportTable unbilled = reports.unbilledTimeByJob(from, to);
        QCOMPARE(unbilled.money(str("time:%1").arg(c.e1), str("amount")), 18000);
        QCOMPARE(unbilled.money(str("time:%1").arg(c.e2), str("amount")), 3960);
        QCOMPARE(unbilled.money(str("time:%1").arg(c.e2), str("rate")), 12000);
        QVERIFY(unbilled.row(str("time:%1").arg(c.e3)) == nullptr); // billed
        QVERIFY(unbilled.row(str("time:%1").arg(c.e5)) == nullptr); // not billable
        QCOMPARE(unbilled.money(str("subtotal:party:%1").arg(c.harbor), str("amount")), 21960);
        QCOMPARE(minutes(unbilled, str("subtotal:party:%1").arg(c.harbor)), 110);
        QCOMPARE(unbilled.money(str("subtotal:party:%1").arg(c.riverbend), str("amount")), 9000);
        QCOMPARE(minutes(unbilled, str("total")), 155);
        QCOMPARE(unbilled.money(str("total"), str("amount")), 30960);
        // A period without the time shows only the total.
        QCOMPARE(minutes(reports.timeByJob(QDate(2026, 4, 1), QDate(2026, 4, 30)), str("total")), 0);
    }
};

QTEST_GUILESS_MAIN(TstBooksTime)
#include "tst_books_time.moc"
