// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: employees and time (M68, D-077; employees.h,
// time_tracking.h) — the employee list, single time activities, the weekly
// timesheet, and billable time offered to the invoice editor.
#include "books_service.h"

#include "company_file.h"
#include "documents.h"
#include "employees.h"
#include "lists.h"
#include "time_tracking.h"

#include <QCoreApplication>

#include <algorithm>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

QString statusKey(TimeStatus s)
{
    return s == TimeStatus::Billed ? QStringLiteral("billed")
           : s == TimeStatus::Unbilled ? QStringLiteral("unbilled")
                                       : QStringLiteral("notBillable");
}

QVariantMap employeeMap(const Employee &e, const MoneyFormat &money)
{
    return {{QStringLiteral("id"), e.id},
            {QStringLiteral("name"), e.name},
            {QStringLiteral("email"), e.email},
            {QStringLiteral("phone"), e.phone},
            {QStringLiteral("address"), e.address},
            {QStringLiteral("notes"), e.notes},
            {QStringLiteral("rate"), e.defaultRate ? money.plain(e.defaultRate->minor()) : QString()},
            {QStringLiteral("rateText"), e.defaultRate ? money.text(e.defaultRate->minor()) : QString()},
            {QStringLiteral("active"), e.active}};
}

QVariantMap entryMap(CompanyFile &file, const TimeEntry &e)
{
    Lists lists(file);
    const std::optional<Item> item = lists.item(e.itemId);
    const std::optional<Party> customer = lists.party(e.customerId);
    const std::optional<BusinessDocument> invoice =
        e.invoiceId != 0 ? Documents(file).load(e.invoiceId) : std::nullopt;
    const TimeStatus s = e.status();
    return {{QStringLiteral("id"), e.id},
            {QStringLiteral("date"), Format::isoDate(e.date)},
            {QStringLiteral("worker"), e.worker.key()},
            {QStringLiteral("workerName"), TimeTracking(file).workerName(e.worker)},
            {QStringLiteral("customerId"), e.customerId},
            {QStringLiteral("customerName"), customer ? customer->displayName : QString()},
            {QStringLiteral("itemId"), e.itemId},
            {QStringLiteral("itemName"), item ? item->name : QString()},
            {QStringLiteral("minutes"), e.minutes},
            {QStringLiteral("duration"), TimeTracking::durationText(e.minutes)},
            {QStringLiteral("billable"), e.billable},
            {QStringLiteral("status"), statusKey(s)},
            {QStringLiteral("statusLabel"), s == TimeStatus::Billed     ? QCoreApplication::translate("QindaBooks", "Billed")
                                            : s == TimeStatus::Unbilled ? QCoreApplication::translate("QindaBooks", "Unbilled")
                                                                        : QCoreApplication::translate("QindaBooks", "Not billable")},
            {QStringLiteral("notes"), e.notes},
            {QStringLiteral("classId"), e.classId},
            {QStringLiteral("invoiceId"), e.invoiceId},
            {QStringLiteral("invoiceNumber"), invoice ? invoice->number : QString()}};
}

} // namespace

// ---- employees ----------------------------------------------------------------

QVariantList BooksService::employees(bool includeInactive) const
{
    QVariantList out;
    if (file() == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    for (const Employee &e : Employees(*file()).employees(includeInactive)) {
        out << employeeMap(e, money);
    }
    return out;
}

QVariantMap BooksService::employee(qint64 id) const
{
    if (file() == nullptr) {
        return {};
    }
    const std::optional<Employee> e = Employees(*file()).employee(id);
    return e ? employeeMap(*e, moneyFormat()) : QVariantMap();
}

QVariantMap BooksService::saveEmployee(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    Employees list(*c);
    const qint64 id = Format::id(map.value(QStringLiteral("id")));
    Employee e;
    if (id != 0) {
        const std::optional<Employee> existing = list.employee(id);
        if (!existing) {
            return Format::failure(tr("That employee no longer exists."));
        }
        e = *existing;
    }
    for (const auto &[key, field] : {std::pair{"name", &e.name}, std::pair{"email", &e.email}, std::pair{"phone", &e.phone},
                                     std::pair{"address", &e.address}, std::pair{"notes", &e.notes}}) {
        if (map.contains(QString::fromLatin1(key))) {
            *field = map.value(QString::fromLatin1(key)).toString();
        }
    }
    if (map.contains(QStringLiteral("rate"))) {
        const QString text = map.value(QStringLiteral("rate")).toString().trimmed();
        if (text.isEmpty()) {
            e.defaultRate.reset();
        } else {
            QString error;
            const std::optional<qint64> minor = moneyFormat().parse(text, &error);
            if (!minor) {
                return Format::failure(error);
            }
            e.defaultRate = c->money(*minor);
        }
    }
    if (map.contains(QStringLiteral("active"))) {
        e.active = map.value(QStringLiteral("active")).toBool();
    }
    return Format::result(id != 0 ? list.update(e) : list.create(e));
}

QVariantMap BooksService::setEmployeeActive(qint64 id, bool active)
{
    return file() != nullptr ? Format::result(Employees(*file()).setActive(id, active)) : noCompany();
}

QVariantList BooksService::workers() const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    for (const Employee &e : Employees(*c).employees()) {
        out << QVariantMap{{QStringLiteral("key"), Worker::employee(e.id).key()},
                           {QStringLiteral("name"), e.name},
                           {QStringLiteral("kindLabel"), tr("Employee")}};
    }
    for (const Party &v : Lists(*c).parties(PartyKind::Vendor)) {
        out << QVariantMap{{QStringLiteral("key"), Worker::vendor(v.id).key()},
                           {QStringLiteral("name"), v.displayName},
                           {QStringLiteral("kindLabel"), tr("Vendor")}};
    }
    return out;
}

// ---- time entries -------------------------------------------------------------

QVariantList BooksService::timeEntries(const QVariantMap &filter) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    TimeFilter f;
    f.worker = Worker::fromKey(filter.value(QStringLiteral("worker")).toString());
    f.customerId = Format::id(filter.value(QStringLiteral("customerId")));
    f.from = Format::date(filter.value(QStringLiteral("from")));
    f.to = Format::date(filter.value(QStringLiteral("to")));
    const QString status = filter.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("unbilled")) {
        f.status = TimeStatus::Unbilled;
    } else if (status == QLatin1String("billed")) {
        f.status = TimeStatus::Billed;
    } else if (status == QLatin1String("notBillable")) {
        f.status = TimeStatus::NotBillable;
    }
    QList<TimeEntry> entries = TimeTracking(*c).entries(f);
    std::reverse(entries.begin(), entries.end()); // newest first, as the lists show documents
    for (const TimeEntry &e : std::as_const(entries)) {
        out << entryMap(*c, e);
    }
    return out;
}

QVariantMap BooksService::timeEntry(qint64 id) const
{
    CompanyFile *c = file();
    const std::optional<TimeEntry> e = c != nullptr ? TimeTracking(*c).entry(id) : std::nullopt;
    return e ? entryMap(*c, *e) : QVariantMap();
}

QVariantMap BooksService::saveTimeEntry(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    TimeEntry e;
    e.id = Format::id(map.value(QStringLiteral("id")));
    e.date = Format::date(map.value(QStringLiteral("date")));
    if (!e.date.isValid()) {
        e.date = todayDate();
    }
    e.worker = Worker::fromKey(map.value(QStringLiteral("worker")).toString());
    e.customerId = Format::id(map.value(QStringLiteral("customerId")));
    e.itemId = Format::id(map.value(QStringLiteral("itemId")));
    e.billable = map.value(QStringLiteral("billable")).toBool();
    e.notes = map.value(QStringLiteral("notes")).toString();
    e.classId = Format::id(map.value(QStringLiteral("classId")));
    const QString duration = map.value(QStringLiteral("duration")).toString();
    const std::optional<int> minutes = TimeTracking::parseDuration(duration);
    if (!minutes) {
        return Format::failure(tr("\"%1\" is not a duration: type 1:30, 1.5 or 90m.").arg(duration));
    }
    e.minutes = *minutes;
    TimeTracking time(*c);
    return Format::result(e.id != 0 ? time.update(e) : time.create(e));
}

QVariantMap BooksService::deleteTimeEntry(qint64 id)
{
    return file() != nullptr ? Format::result(TimeTracking(*file()).remove(id)) : noCompany();
}

// ---- the weekly timesheet ---------------------------------------------------------

QVariantMap BooksService::timesheet(const QString &worker, const QString &dateIso) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    QDate date = Format::date(dateIso);
    if (!date.isValid()) {
        date = todayDate();
    }
    const QDate start = TimeTracking::weekStartOf(date);
    const Worker who = Worker::fromKey(worker);
    QVariantList days;
    for (int d = 0; d < 7; ++d) {
        days << QVariantMap{{QStringLiteral("date"), Format::isoDate(start.addDays(d))},
                            {QStringLiteral("label"), moneyFormat().locale.toString(start.addDays(d), QStringLiteral("ddd d"))}};
    }
    QList<qint64> dayTotals(7, 0);
    qint64 total = 0;
    QVariantList rows;
    for (const TimesheetRow &row : TimeTracking(*c).week(who, start)) {
        QVariantList cells;
        qint64 rowTotal = 0;
        for (int d = 0; d < 7; ++d) {
            const TimesheetCell &cell = row.days.at(d);
            cells << QVariantMap{{QStringLiteral("entryId"), cell.entryId},
                                 {QStringLiteral("duration"), cell.minutes > 0 ? TimeTracking::durationText(cell.minutes) : QString()},
                                 {QStringLiteral("billed"), cell.billed}};
            rowTotal += cell.minutes;
            dayTotals[d] += cell.minutes;
        }
        total += rowTotal;
        rows << QVariantMap{{QStringLiteral("customerId"), row.customerId},
                            {QStringLiteral("itemId"), row.itemId},
                            {QStringLiteral("billable"), row.billable},
                            {QStringLiteral("notes"), row.notes},
                            {QStringLiteral("classId"), row.classId},
                            {QStringLiteral("cells"), cells},
                            {QStringLiteral("totalText"), TimeTracking::durationText(rowTotal)}};
    }
    QVariantList totals;
    for (const qint64 m : dayTotals) {
        totals << TimeTracking::durationText(m);
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("worker"), who.key());
    out.insert(QStringLiteral("weekStart"), Format::isoDate(start));
    out.insert(QStringLiteral("days"), days);
    out.insert(QStringLiteral("rows"), rows);
    out.insert(QStringLiteral("dayTotals"), totals);
    out.insert(QStringLiteral("totalText"), TimeTracking::durationText(total));
    return out;
}

QVariantMap BooksService::saveTimesheet(const QVariantMap &sheet)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const Worker who = Worker::fromKey(sheet.value(QStringLiteral("worker")).toString());
    const QDate start = TimeTracking::weekStartOf(Format::date(sheet.value(QStringLiteral("weekStart"))));
    QList<TimesheetRow> rows;
    int rowNo = 0;
    for (const QVariant &v : sheet.value(QStringLiteral("rows")).toList()) {
        ++rowNo;
        const QVariantMap r = v.toMap();
        TimesheetRow row;
        row.customerId = Format::id(r.value(QStringLiteral("customerId")));
        row.itemId = Format::id(r.value(QStringLiteral("itemId")));
        row.billable = r.value(QStringLiteral("billable")).toBool();
        row.notes = r.value(QStringLiteral("notes")).toString();
        row.classId = Format::id(r.value(QStringLiteral("classId")));
        const QVariantList cells = r.value(QStringLiteral("cells")).toList();
        for (int d = 0; d < 7; ++d) {
            const QVariantMap cell = cells.value(d).toMap();
            const QString text = cell.value(QStringLiteral("duration")).toString().trimmed();
            int minutes = 0;
            if (!text.isEmpty() && text != QLatin1String("0") && text != QLatin1String("0:00")) {
                const std::optional<int> parsed = TimeTracking::parseDuration(text);
                if (!parsed) {
                    return Format::failure(tr("Row %1: \"%2\" is not a duration.").arg(rowNo).arg(text));
                }
                minutes = *parsed;
            }
            row.days << TimesheetCell{Format::id(cell.value(QStringLiteral("entryId"))), minutes, false};
        }
        rows << row;
    }
    return Format::result(TimeTracking(*c).saveWeek(who, start, rows));
}

// ---- billable time onto invoices -----------------------------------------------------

QVariantList BooksService::unbilledTime(qint64 customerId) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    TimeTracking time(*c);
    Lists lists(*c);
    const MoneyFormat money = moneyFormat();
    for (const TimeEntry &e : time.unbilled(customerId)) {
        const std::optional<Item> item = lists.item(e.itemId);
        const Money rate = time.rateFor(e);
        const qint64 amount = rate.multiplied(TimeTracking::hours(e.minutes), RoundingMode::HalfUp).minor();
        out << QVariantMap{{QStringLiteral("id"), e.id},
                           {QStringLiteral("date"), Format::isoDate(e.date)},
                           {QStringLiteral("workerName"), time.workerName(e.worker)},
                           {QStringLiteral("itemName"), item ? item->name : QString()},
                           {QStringLiteral("notes"), e.notes},
                           {QStringLiteral("duration"), TimeTracking::durationText(e.minutes)},
                           {QStringLiteral("hours"), TimeTracking::hours(e.minutes).toString()},
                           {QStringLiteral("rateText"), money.text(rate.minor())},
                           {QStringLiteral("amount"), amount},
                           {QStringLiteral("amountText"), money.text(amount)}};
    }
    return out;
}

QVariantMap BooksService::timeInvoiceLines(qint64 customerId, const QVariantList &entryIds) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    QList<qint64> ids;
    for (const QVariant &v : entryIds) {
        ids << Format::id(v);
    }
    if (ids.isEmpty()) {
        return Format::failure(tr("Choose the time to bill."));
    }
    QString error;
    const std::optional<QList<DocumentLine>> lines = TimeTracking(*c).invoiceLines(customerId, ids, &error);
    if (!lines) {
        return Format::failure(error);
    }
    Lists lists(*c);
    const MoneyFormat money = moneyFormat();
    QVariantList out;
    for (const DocumentLine &l : *lines) {
        const std::optional<Item> item = lists.item(l.itemId);
        out << QVariantMap{{QStringLiteral("itemId"), l.itemId},
                           {QStringLiteral("accountId"), l.accountId},
                           {QStringLiteral("description"), l.description},
                           {QStringLiteral("quantity"), l.quantity.toString()},
                           {QStringLiteral("rate"), money.plain(l.unitPrice.minor())},
                           {QStringLiteral("taxable"), item && item->taxable},
                           {QStringLiteral("classId"), l.classId},
                           {QStringLiteral("timeEntryId"), l.timeEntryId}};
    }
    QVariantMap result = Format::success();
    result.insert(QStringLiteral("lines"), out);
    return result;
}

} // namespace QindaOffice::Apps::Books
