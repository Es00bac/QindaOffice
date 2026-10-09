// SPDX-License-Identifier: GPL-3.0-or-later
// Time tracking (time_tracking.h): entries, their billed state (derived
// from live invoice lines), invoice lines for billable time, durations.
// The weekly timesheet is time_sheet.cpp.
#include "time_tracking.h"

#include "company_file.h"
#include "lists.h"
#include "sql_util.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlQuery>

namespace QindaOffice::Books {

namespace {

const QString kColumns =
    QStringLiteral("id, date, employee_id, vendor_id, customer_id, item_id, minutes, billable, notes, class_id");

TimeEntry readEntry(const QSqlQuery &q)
{
    TimeEntry e;
    e.id = q.value(0).toLongLong();
    e.date = Sql::fromIso(q.value(1));
    e.worker = Worker{q.value(2).toLongLong(), q.value(3).toLongLong()};
    e.customerId = q.value(4).toLongLong();
    e.itemId = q.value(5).toLongLong();
    e.minutes = q.value(6).toInt();
    e.billable = q.value(7).toBool();
    e.notes = q.value(8).toString();
    e.classId = q.value(9).toLongLong();
    return e;
}

QString entryJson(const TimeEntry &e)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("date"), Sql::isoDate(e.date)},
                                                       {QStringLiteral("worker"), e.worker.key()},
                                                       {QStringLiteral("customer"), e.customerId},
                                                       {QStringLiteral("item"), e.itemId},
                                                       {QStringLiteral("minutes"), e.minutes},
                                                       {QStringLiteral("billable"), e.billable},
                                                       {QStringLiteral("notes"), e.notes}})
                                 .toJson(QJsonDocument::Compact));
}

// Every time entry a non-void invoice bills → that invoice (time_tracking.h).
QHash<qint64, qint64> billedEntries(CompanyFile &file)
{
    QHash<qint64, qint64> out;
    QSqlQuery q(file.database());
    Sql::run(q, QStringLiteral("SELECT dl.time_entry_id, d.id FROM document_lines dl JOIN documents d ON d.id = dl.doc_id"
                               " WHERE dl.time_entry_id IS NOT NULL AND d.kind = ? AND d.state <> ?"),
             {int(DocKind::Invoice), int(DocState::Void)});
    while (q.next()) {
        out.insert(q.value(0).toLongLong(), q.value(1).toLongLong());
    }
    return out;
}

} // namespace

QString Worker::key() const
{
    return employeeId != 0 ? QStringLiteral("employee:%1").arg(employeeId)
           : vendorId != 0 ? QStringLiteral("vendor:%1").arg(vendorId)
                           : QString();
}

Worker Worker::fromKey(const QString &key)
{
    const qint64 id = key.section(QLatin1Char(':'), 1).toLongLong();
    if (key.startsWith(QLatin1String("employee:"))) {
        return employee(id);
    }
    return key.startsWith(QLatin1String("vendor:")) ? vendor(id) : Worker{};
}

TimeTracking::TimeTracking(CompanyFile &file)
    : m_file(file)
{
}

qint64 TimeTracking::billedBy(qint64 entryId, qint64 exceptInvoice) const
{
    return Sql::scalarInt(m_file.database(),
                          QStringLiteral("SELECT d.id FROM document_lines dl JOIN documents d ON d.id = dl.doc_id"
                                         " WHERE dl.time_entry_id = ? AND d.kind = ? AND d.state <> ? AND d.id <> ?"
                                         " LIMIT 1"),
                          {entryId, int(DocKind::Invoice), int(DocState::Void), exceptInvoice});
}

Result TimeTracking::validate(TimeEntry &entry) const
{
    Lists lists(m_file);
    if (!entry.date.isValid()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("time needs a date"));
    }
    if (!entry.worker.isValid()) {
        return Result::failure(ErrorCode::UnknownParty, QStringLiteral("choose who did the work (an employee or a vendor)"));
    }
    if (entry.worker.employeeId != 0) {
        const std::optional<Employee> employee = Employees(m_file).employee(entry.worker.employeeId);
        if (!employee) {
            return Result::failure(ErrorCode::UnknownParty, QStringLiteral("no such employee"));
        }
        if (!employee->active && entry.id == 0) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("%1 is inactive").arg(employee->name));
        }
    } else {
        const std::optional<Party> vendor = lists.party(entry.worker.vendorId);
        if (!vendor || vendor->kind != PartyKind::Vendor) {
            return Result::failure(ErrorCode::UnknownParty, QStringLiteral("a contractor is one of your vendors"));
        }
    }
    if (entry.customerId != 0) {
        const std::optional<Party> customer = lists.party(entry.customerId);
        if (!customer || customer->kind != PartyKind::Customer) {
            return Result::failure(ErrorCode::UnknownParty, QStringLiteral("the customer:job is not a customer"));
        }
    }
    if (entry.itemId != 0 && !lists.item(entry.itemId)) {
        return Result::failure(ErrorCode::UnknownItem, QStringLiteral("unknown service item"));
    }
    if (entry.billable && (entry.customerId == 0 || entry.itemId == 0)) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("billable time needs a customer:job and a service item"));
    }
    if (entry.minutes <= 0 || entry.minutes > 24 * 60) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a duration is more than 0:00 and at most 24:00"));
    }
    entry.notes = entry.notes.trimmed();
    return Result::success();
}

Result TimeTracking::writeEntry(TimeEntry &entry, bool isNew)
{
    if (const Result valid = validate(entry); !valid.ok) {
        return valid;
    }
    const std::optional<TimeEntry> before = isNew ? std::nullopt : this->entry(entry.id);
    SqlTransaction txn(m_file);
    QString error;
    const QVariantList values{Sql::isoDate(entry.date), Sql::idOrNull(entry.worker.employeeId),
                              Sql::idOrNull(entry.worker.vendorId), Sql::idOrNull(entry.customerId),
                              Sql::idOrNull(entry.itemId), entry.minutes, entry.billable ? 1 : 0, entry.notes,
                              Sql::idOrNull(entry.classId), CompanyFile::nowStamp()};
    if (isNew) {
        QVariantList binds = values;
        binds << CompanyFile::nowStamp();
        entry.id = Sql::insert(m_file.database(),
                               QStringLiteral("INSERT INTO time_entries(date, employee_id, vendor_id, customer_id, item_id,"
                                              " minutes, billable, notes, class_id, updated_at, created_at)"
                                              " VALUES(?,?,?,?,?,?,?,?,?,?,?)"),
                               binds, &error);
        if (entry.id == 0) {
            return Result::failure(ErrorCode::Storage, error);
        }
    } else {
        QVariantList binds = values;
        binds << entry.id;
        if (!Sql::exec(m_file.database(),
                       QStringLiteral("UPDATE time_entries SET date = ?, employee_id = ?, vendor_id = ?, customer_id = ?,"
                                      " item_id = ?, minutes = ?, billable = ?, notes = ?, class_id = ?, updated_at = ?"
                                      " WHERE id = ?"),
                       binds, &error)) {
            return Result::failure(ErrorCode::Storage, error);
        }
    }
    m_file.audit(isNew ? QStringLiteral("create") : QStringLiteral("edit"), QStringLiteral("time"), entry.id,
                 before ? entryJson(*before) : QString(), entryJson(entry));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(entry.id);
}

Result TimeTracking::create(TimeEntry entry)
{
    entry.id = 0;
    return writeEntry(entry, true);
}

Result TimeTracking::update(const TimeEntry &entry)
{
    const std::optional<TimeEntry> before = this->entry(entry.id);
    if (!before) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such time entry"));
    }
    if (before->invoiceId != 0) {
        return Result::failure(ErrorCode::Invalid,
                               QStringLiteral("this time is billed on an invoice; remove it from the invoice first"));
    }
    TimeEntry changed = entry;
    return writeEntry(changed, false);
}

Result TimeTracking::remove(qint64 id)
{
    const std::optional<TimeEntry> before = entry(id);
    if (!before) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such time entry"));
    }
    if (before->invoiceId != 0) {
        return Result::failure(ErrorCode::Invalid,
                               QStringLiteral("this time is billed on an invoice; remove it from the invoice first"));
    }
    SqlTransaction txn(m_file);
    QString error;
    if (!Sql::exec(m_file.database(), QStringLiteral("DELETE FROM time_entries WHERE id = ?"), {id}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(QStringLiteral("delete"), QStringLiteral("time"), id, entryJson(*before), {});
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(id);
}

std::optional<TimeEntry> TimeTracking::entry(qint64 id) const
{
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM time_entries WHERE id = ?"), {id});
    if (!q.next()) {
        return std::nullopt;
    }
    TimeEntry e = readEntry(q);
    q.finish();
    e.invoiceId = billedBy(e.id);
    return e;
}

QList<TimeEntry> TimeTracking::entries(const TimeFilter &filter) const
{
    QStringList where;
    QVariantList binds;
    if (filter.worker.employeeId != 0) {
        where << QStringLiteral("employee_id = ?");
        binds << filter.worker.employeeId;
    }
    if (filter.worker.vendorId != 0) {
        where << QStringLiteral("vendor_id = ?");
        binds << filter.worker.vendorId;
    }
    if (filter.customerId != 0) {
        where << QStringLiteral("customer_id = ?");
        binds << filter.customerId;
    }
    if (filter.from.isValid()) {
        where << QStringLiteral("date >= ?");
        binds << Sql::isoDate(filter.from);
    }
    if (filter.to.isValid()) {
        where << QStringLiteral("date <= ?");
        binds << Sql::isoDate(filter.to);
    }
    const QHash<qint64, qint64> billed = billedEntries(m_file);
    QList<TimeEntry> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM time_entries")
                    + (where.isEmpty() ? QString() : QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND ")))
                    + QStringLiteral(" ORDER BY date, id"),
             binds);
    while (q.next()) {
        TimeEntry e = readEntry(q);
        e.invoiceId = billed.value(e.id);
        if (!filter.status || e.status() == *filter.status) {
            out << e;
        }
    }
    return out;
}

QList<TimeEntry> TimeTracking::unbilled(qint64 customerId) const
{
    TimeFilter filter;
    filter.customerId = customerId;
    filter.status = TimeStatus::Unbilled;
    return customerId == 0 ? QList<TimeEntry>() : entries(filter);
}

std::optional<QList<DocumentLine>> TimeTracking::invoiceLines(qint64 customerId, const QList<qint64> &entryIds,
                                                              QString *error) const
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return std::nullopt;
    };
    Lists lists(m_file);
    QList<DocumentLine> out;
    for (qint64 id : entryIds) {
        const std::optional<TimeEntry> e = entry(id);
        if (!e || e->customerId != customerId || e->status() != TimeStatus::Unbilled) {
            return fail(QStringLiteral("only this customer's unbilled billable time can go on the invoice"));
        }
        const std::optional<Item> item = lists.item(e->itemId);
        if (!item) {
            return fail(QStringLiteral("the time entry has no service item"));
        }
        DocumentLine line;
        line.itemId = item->id;
        line.accountId = item->incomeAccountId;
        line.description = !e->notes.isEmpty()           ? e->notes
                           : !item->description.isEmpty() ? item->description
                                                          : item->name;
        line.quantity = hours(e->minutes);
        line.unitPrice = rateFor(*e);
        line.amount = m_file.money(0);
        line.classId = e->classId;
        line.timeEntryId = e->id;
        line.taxableFromItem = true;
        out << line;
    }
    return out;
}

Result TimeTracking::checkInvoiceLines(const BusinessDocument &invoice) const
{
    QSet<qint64> seen;
    for (const DocumentLine &line : invoice.lines) {
        if (line.timeEntryId == 0) {
            continue;
        }
        if (seen.contains(line.timeEntryId)) {
            return Result::failure(ErrorCode::Duplicate, QStringLiteral("the same time is on the invoice twice"));
        }
        seen.insert(line.timeEntryId);
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT customer_id, billable FROM time_entries WHERE id = ?"), {line.timeEntryId});
        if (!q.next()) {
            return Result::failure(ErrorCode::NotFound, QStringLiteral("a line bills time that no longer exists"));
        }
        const bool sameCustomer = q.value(0).toLongLong() == invoice.partyId;
        const bool billable = q.value(1).toBool();
        q.finish();
        if (!sameCustomer || !billable) {
            return Result::failure(ErrorCode::Invalid,
                                   QStringLiteral("\"%1\" bills time that is not this customer's billable time")
                                       .arg(line.description));
        }
        if (billedBy(line.timeEntryId, invoice.id) != 0) {
            return Result::failure(ErrorCode::Duplicate,
                                   QStringLiteral("\"%1\": that time is already billed on another invoice").arg(line.description));
        }
    }
    return Result::success();
}

Money TimeTracking::rateFor(const TimeEntry &entry) const
{
    if (entry.worker.employeeId != 0) {
        const std::optional<Employee> employee = Employees(m_file).employee(entry.worker.employeeId);
        if (employee && employee->defaultRate) {
            return *employee->defaultRate;
        }
    }
    const std::optional<Item> item = Lists(m_file).item(entry.itemId);
    return item ? item->price : m_file.money(0);
}

QString TimeTracking::workerName(const Worker &worker) const
{
    if (worker.employeeId != 0) {
        const std::optional<Employee> employee = Employees(m_file).employee(worker.employeeId);
        return employee ? employee->name : QString();
    }
    const std::optional<Party> vendor = Lists(m_file).party(worker.vendorId);
    return vendor ? vendor->displayName : QString();
}

Decimal TimeTracking::hours(int minutes)
{
    return Decimal(scaleExact(minutes, 100, 60, RoundingMode::HalfUp).value_or(0), 2).normalized();
}

QString TimeTracking::durationText(qint64 minutes)
{
    const qint64 whole = minutes < 0 ? -minutes : minutes;
    return QStringLiteral("%1%2:%3")
        .arg(minutes < 0 ? QStringLiteral("-") : QString())
        .arg(whole / 60)
        .arg(whole % 60, 2, 10, QLatin1Char('0'));
}

std::optional<int> TimeTracking::parseDuration(const QString &input)
{
    QString text = input.trimmed().toLower();
    if (text.isEmpty()) {
        return std::nullopt;
    }
    qint64 minutes = 0;
    if (text.contains(QLatin1Char(':'))) {
        const QStringList parts = text.split(QLatin1Char(':'));
        bool okH = false;
        bool okM = false;
        const qint64 h = parts.value(0).isEmpty() ? 0 : parts.value(0).toLongLong(&okH);
        const qint64 m = parts.value(1).toLongLong(&okM);
        if (parts.size() != 2 || (!okH && !parts.value(0).isEmpty()) || !okM || h < 0 || m < 0 || m > 59
            || parts.value(1).size() != 2) {
            return std::nullopt;
        }
        minutes = h * 60 + m;
    } else if (text.endsWith(QLatin1Char('m'))) {
        bool ok = false;
        minutes = text.chopped(1).trimmed().toLongLong(&ok);
        if (!ok) {
            return std::nullopt;
        }
    } else {
        if (text.endsWith(QLatin1Char('h'))) {
            text.chop(1);
        }
        const std::optional<Decimal> hoursValue = Decimal::parse(text);
        if (!hoursValue) {
            return std::nullopt;
        }
        const std::optional<qint64> m =
            scaleExact(hoursValue->mantissa(), 60, pow10i(hoursValue->scale()), RoundingMode::HalfUp);
        if (!m) {
            return std::nullopt;
        }
        minutes = *m;
    }
    if (minutes <= 0 || minutes > 100000) {
        return std::nullopt;
    }
    return int(minutes);
}

QDate TimeTracking::weekStartOf(const QDate &date)
{
    return date.isValid() ? date.addDays(1 - date.dayOfWeek()) : QDate();
}

} // namespace QindaOffice::Books
