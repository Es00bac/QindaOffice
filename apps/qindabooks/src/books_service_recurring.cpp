// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: recurring invoices — the schedules (create, edit, pause,
// resume, delete; M47 + M63) and generating what is due.
//
// AGENT-CONTRACT (QML, RecurringDialog.qml / RecurringEditor.qml):
//  * recurringTemplates() rows: id, name, customerId, customerName,
//    frequency (daily | weekly | monthly | yearly), interval, dayOfMonth,
//    start, end, maxOccurrences, remaining (0 = no limit), nextDate,
//    generated, postOpen, active, due, total, totalText, scheduleText.
//  * saveRecurring(map): {id, name, frequency, interval, dayOfMonth,
//    nextDate (or start), end, maxOccurrences (invoices still to come),
//    postOpen, document: {partyId, lines, …}} — id 0 creates; an existing
//    id edits, and a changed schedule re-bases from nextDate
//    (Recurring::update); an id with only `active` pauses or resumes.
#include "books_service.h"

#include "company_file.h"
#include "documents.h"
#include "recurring.h"

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

QString frequencyKey(Frequency f)
{
    switch (f) {
    case Frequency::Daily: return QStringLiteral("daily");
    case Frequency::Weekly: return QStringLiteral("weekly");
    case Frequency::Monthly: return QStringLiteral("monthly");
    case Frequency::Yearly: return QStringLiteral("yearly");
    }
    return QStringLiteral("monthly");
}

Frequency frequencyFrom(const QString &key)
{
    if (key == QLatin1String("daily")) {
        return Frequency::Daily;
    }
    if (key == QLatin1String("weekly")) {
        return Frequency::Weekly;
    }
    return key == QLatin1String("yearly") ? Frequency::Yearly : Frequency::Monthly;
}

// "Every month", "Every 2 weeks", "Every year" — the list's schedule column.
QString scheduleText(const RecurringSchedule &s)
{
    const int n = s.interval;
    switch (s.frequency) {
    case Frequency::Daily: return n == 1 ? BooksService::tr("Every day") : BooksService::tr("Every %1 days").arg(n);
    case Frequency::Weekly: return n == 1 ? BooksService::tr("Every week") : BooksService::tr("Every %1 weeks").arg(n);
    case Frequency::Monthly:
        return n == 1 ? BooksService::tr("Every month") : BooksService::tr("Every %1 months").arg(n);
    case Frequency::Yearly: return n == 1 ? BooksService::tr("Every year") : BooksService::tr("Every %1 years").arg(n);
    }
    return QString();
}

int remainingOf(const RecurringTemplate &t)
{
    return t.schedule.maxOccurrences > 0 ? qMax(0, t.schedule.maxOccurrences - t.generated) : 0;
}

} // namespace

QVariantList BooksService::recurringTemplates() const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    Recurring recurring(*c);
    for (const RecurringTemplate &t : recurring.templates()) {
        const QDate next = Recurring::occurrence(t.schedule, t.generated);
        out << QVariantMap{{QStringLiteral("id"), t.id},
                           {QStringLiteral("name"), t.name},
                           {QStringLiteral("customerId"), t.body.partyId},
                           {QStringLiteral("customerName"), nameOfParty(t.body.partyId)},
                           {QStringLiteral("frequency"), frequencyKey(t.schedule.frequency)},
                           {QStringLiteral("interval"), t.schedule.interval},
                           {QStringLiteral("dayOfMonth"), t.schedule.dayOfMonth},
                           {QStringLiteral("start"), Format::isoDate(t.schedule.start)},
                           {QStringLiteral("end"), Format::isoDate(t.schedule.end)},
                           {QStringLiteral("maxOccurrences"), t.schedule.maxOccurrences},
                           {QStringLiteral("remaining"), remainingOf(t)},
                           {QStringLiteral("nextDate"), Format::isoDate(next)},
                           {QStringLiteral("generated"), t.generated},
                           {QStringLiteral("postOpen"), t.postOpen},
                           {QStringLiteral("active"), t.active},
                           {QStringLiteral("due"), t.active ? recurring.dueDates(t.id, todayDate()).size() : 0},
                           {QStringLiteral("total"), t.body.total.minor()},
                           {QStringLiteral("totalText"), money.text(t.body.total.minor())},
                           {QStringLiteral("scheduleText"), scheduleText(t.schedule)},
                           {QStringLiteral("statusText"), !t.active        ? tr("Paused")
                                                          : next.isValid() ? tr("Next %1").arg(Format::isoDate(next))
                                                                           : tr("Finished")}};
    }
    return out;
}

QVariantMap BooksService::recurringTemplate(qint64 id) const
{
    for (const QVariant &row : recurringTemplates()) {
        QVariantMap map = row.toMap();
        if (Format::id(map.value(QStringLiteral("id"))) == id) {
            const std::optional<RecurringTemplate> t = Recurring(*file()).recurringTemplate(id);
            map.insert(QStringLiteral("document"), t ? document(t->body.id) : QVariantMap{});
            map.insert(QStringLiteral("ok"), true);
            return map;
        }
    }
    return Format::failure(tr("That recurring schedule no longer exists."));
}

QVariantMap BooksService::saveRecurring(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    Recurring recurring(*c);
    const qint64 id = Format::id(map.value(QStringLiteral("id")));
    const bool edits = map.contains(QStringLiteral("document")) || map.contains(QStringLiteral("frequency"))
        || map.contains(QStringLiteral("name"));
    if (id != 0 && !edits) {
        if (!map.contains(QStringLiteral("active"))) {
            return Format::failure(tr("Nothing to change."));
        }
        return setRecurringActive(id, map.value(QStringLiteral("active")).toBool());
    }
    const std::optional<RecurringTemplate> current = id != 0 ? recurring.recurringTemplate(id) : std::nullopt;
    if (id != 0 && !current) {
        return Format::failure(tr("That recurring schedule no longer exists."));
    }
    QVariantMap body = map.value(QStringLiteral("document")).toMap();
    body.insert(QStringLiteral("kind"), QStringLiteral("invoice"));
    body.remove(QStringLiteral("id"));
    BusinessDocument doc;
    QString error;
    if (!documentFromMap(body, &doc, &error)) {
        return Format::failure(error);
    }
    RecurringSchedule schedule;
    schedule.frequency = frequencyFrom(map.value(QStringLiteral("frequency")).toString());
    schedule.interval = qMax(1, map.value(QStringLiteral("interval"), 1).toInt());
    schedule.dayOfMonth = qBound(0, map.value(QStringLiteral("dayOfMonth")).toInt(), 31);
    schedule.start = Format::date(map.value(map.contains(QStringLiteral("nextDate")) ? QStringLiteral("nextDate")
                                                                                     : QStringLiteral("start")));
    if (!schedule.start.isValid()) {
        schedule.start = current ? Recurring::occurrence(current->schedule, current->generated) : todayDate();
    }
    schedule.end = Format::date(map.value(QStringLiteral("end")));
    schedule.maxOccurrences = qMax(0, map.value(QStringLiteral("maxOccurrences")).toInt());
    const QString name = map.value(QStringLiteral("name")).toString().trimmed();
    const bool postOpen = map.value(QStringLiteral("postOpen"), true).toBool();
    if (!current) {
        return Format::result(recurring.create(name, doc, schedule, postOpen));
    }
    // AGENT-NOTE: the dialog shows the NEXT invoice date and the invoices
    // still to come. When those and the rest of the schedule are what the
    // template already says, the stored schedule (its original start and
    // count) is passed back unchanged, so nothing re-bases and a Jan-31
    // monthly schedule keeps its month-end days.
    const QDate next = Recurring::occurrence(current->schedule, current->generated);
    if (schedule.frequency == current->schedule.frequency && schedule.interval == current->schedule.interval
        && schedule.dayOfMonth == current->schedule.dayOfMonth && schedule.end == current->schedule.end
        && schedule.start == next && schedule.maxOccurrences == remainingOf(*current)) {
        schedule = current->schedule;
    }
    QVariantMap out = Format::result(recurring.update(id, name, doc, schedule, postOpen));
    if (out.value(QStringLiteral("ok")).toBool() && map.contains(QStringLiteral("active"))
        && map.value(QStringLiteral("active")).toBool() != current->active) {
        out = setRecurringActive(id, map.value(QStringLiteral("active")).toBool());
    }
    return out;
}

QVariantMap BooksService::setRecurringActive(qint64 id, bool active)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    if (!Recurring(*c).recurringTemplate(id)) {
        return Format::failure(tr("That recurring schedule no longer exists."));
    }
    return Format::result(Recurring(*c).setActive(id, active));
}

QVariantMap BooksService::deleteRecurring(qint64 id)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    return Format::result(Recurring(*c).remove(id));
}

int BooksService::generateDueRecurring()
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return 0;
    }
    QList<qint64> created;
    const Result r = Recurring(*c).generateDue(todayDate(), &created);
    if (!r.ok) {
        emit notify(tr("Recurring invoices"), r.error, QStringLiteral("danger"));
    } else if (!created.isEmpty()) {
        emit notify(tr("Recurring invoices"), tr("Created %n invoice(s).", nullptr, int(created.size())),
                    QStringLiteral("success"));
    }
    return int(created.size());
}

int BooksService::dueRecurringCount() const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return 0;
    }
    Recurring recurring(*c);
    int due = 0;
    for (const RecurringTemplate &t : recurring.templates()) {
        if (t.active) {
            due += int(recurring.dueDates(t.id, todayDate()).size());
        }
    }
    return due;
}

} // namespace QindaOffice::Apps::Books
