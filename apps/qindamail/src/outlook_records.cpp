// SPDX-License-Identifier: GPL-3.0-or-later
// Outlook items → QindaMail records (outlook_records.h).

#include "outlook_records.h"

#include "contact_store.h"
#include "recurrence_pattern.h"
#include "windows_zones.h"

#include <QCoreApplication>
#include <QTimeZone>
#include <QUuid>

namespace QindaOffice::Apps::Mail::OutlookRecords {

using namespace QindaOffice::Mail;

namespace {

// A fixed namespace for version-5 UUIDs of imported Outlook items.
const QUuid kNamespace{0x6a1f2c3e, 0x8d4b, 0x5e0f, 0x9a, 0x7c, 0x21, 0x3b, 0x44, 0x5d, 0x6e, 0x7f};

QString localText(const QDateTime &utc, const QTimeZone &zone)
{
    return utc.toTimeZone(zone).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
}

} // namespace

QString itemKey(const QByteArray &storeKey, quint32 nid)
{
    return QStringLiteral("pst:%1:%2").arg(QString::fromLatin1(storeKey.toHex()), QString::number(nid, 16));
}

QString uidFor(const QString &key)
{
    return QUuid::createUuidV5(kNamespace, key).toString(QUuid::WithoutBraces);
}

bool eventRecord(const OutlookAppointment &a, const QString &id, QVariantMap *out)
{
    if (!a.start.isValid()) return false;
    QString zoneId = Zones::toIana(a.windowsZone);
    if (zoneId.isEmpty() || !QTimeZone(zoneId.toUtf8()).isValid()) zoneId = QString::fromLatin1(QTimeZone::systemTimeZoneId());
    const QTimeZone zone(zoneId.toUtf8());
    const QDateTime start = a.start;
    QDateTime end = a.end.isValid() && a.end > start ? a.end : start.addSecs(a.allDay ? 86400 : 1800);
    QVariantMap r{{QStringLiteral("id"), id},
                  {QStringLiteral("title"), a.subject.isEmpty() ? QCoreApplication::translate("OutlookRecords", "(no title)")
                                                                : a.subject},
                  {QStringLiteral("description"), a.body},
                  {QStringLiteral("location"), a.location},
                  {QStringLiteral("zone"), zoneId},
                  {QStringLiteral("allDay"), a.allDay},
                  {QStringLiteral("calendarId"), QStringLiteral("local")},
                  {QStringLiteral("exceptions"), QVariantList()},
                  {QStringLiteral("overrides"), QVariantList()},
                  {QStringLiteral("importedFrom"), QStringLiteral("outlook")}};
    QDate firstDate = start.toTimeZone(zone).date();
    if (a.allDay) {
        // Outlook keeps an all-day event as local midnight to midnight.
        QDate last = end.toTimeZone(zone).date();
        if (last <= firstDate) last = firstDate.addDays(1);
        r.insert(QStringLiteral("startLocal"), firstDate.toString(Qt::ISODate));
        r.insert(QStringLiteral("endLocal"), last.toString(Qt::ISODate));
    } else {
        r.insert(QStringLiteral("startLocal"), localText(start, zone));
        r.insert(QStringLiteral("endLocal"), localText(end, zone));
    }
    if (a.recurring) {
        const OutlookRecurrence &o = a.recurrence;
        RecurrencePattern p;
        p.type = o.type;
        p.interval = o.interval;
        p.days = o.days;
        p.dayOfMonth = o.dayOfMonth;
        p.month = o.month;
        p.index = o.index;
        p.firstDayOfWeek = o.firstDayOfWeek;
        p.range = o.range;
        p.startDate = firstDate;
        p.endDate = o.endDate;
        p.count = o.count;
        const MappedRecurrence mapped = patternToRule(p, firstDate);
        if (mapped.rule.typeId() == QMetaType::QVariantMap) r.insert(QStringLiteral("recurrenceRule"), mapped.rule);
        else if (!mapped.raw.isEmpty()) r.insert(QStringLiteral("recurrenceRaw"), mapped.raw);
        QVariantList exceptions;
        const QTime startTime = start.toTimeZone(zone).time();
        for (const QDate &date : o.deletedDates) {
            exceptions.append(a.allDay ? date.toString(Qt::ISODate)
                                       : QDateTime(date, startTime).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")));
        }
        r.insert(QStringLiteral("exceptions"), exceptions);
    }
    if (!a.uid.isEmpty()) r.insert(QStringLiteral("meetingUid"), a.uid);
    if (!a.organizer.isEmpty()) r.insert(QStringLiteral("organizer"), a.organizer);
    if (!a.attendees.isEmpty()) r.insert(QStringLiteral("attendees"), a.attendees);
    if (!a.categories.isEmpty()) r.insert(QStringLiteral("categories"), a.categories);
    *out = r;
    return true;
}

QString taskStatus(const OutlookTask &task)
{
    if (task.complete || task.status == 2) return QStringLiteral("done");
    if (task.status == 1 || (task.percentComplete > 0 && task.percentComplete < 1)) return QStringLiteral("doing");
    return QStringLiteral("todo");
}

QVariantMap taskRecord(const OutlookTask &task, const QString &id, int order)
{
    QVariantMap r{{QStringLiteral("id"), id},
                  {QStringLiteral("title"), task.subject.isEmpty() ? QCoreApplication::translate("OutlookRecords", "(no title)")
                                                                   : task.subject},
                  {QStringLiteral("status"), taskStatus(task)},
                  {QStringLiteral("order"), order},
                  {QStringLiteral("priority"), task.importance >= 2 ? QStringLiteral("High")
                                               : task.importance == 0 ? QStringLiteral("Low") : QStringLiteral("Normal")},
                  {QStringLiteral("assignee"), QString()},
                  {QStringLiteral("dueDate"), task.due.isValid() ? task.due.toString(Qt::ISODate) : QString()},
                  {QStringLiteral("description"), task.body},
                  {QStringLiteral("importedFrom"), QStringLiteral("outlook")}};
    if (task.start.isValid()) r.insert(QStringLiteral("startDate"), task.start.toString(Qt::ISODate));
    if (task.completed.isValid()) r.insert(QStringLiteral("completedDate"), task.completed.toString(Qt::ISODate));
    if (task.percentComplete > 0) r.insert(QStringLiteral("percentComplete"), qRound(task.percentComplete * 100));
    if (!task.categories.isEmpty()) r.insert(QStringLiteral("categories"), task.categories);
    return r;
}

QList<ContactMember> groupMembers(const QList<DistListMember> &members, const QByteArray &storeKey,
                                  const ContactStore *contacts)
{
    QList<ContactMember> out;
    for (const DistListMember &member : members) {
        ContactMember m;
        if (member.contactNid != 0 && !storeKey.isEmpty() && member.storeUid == storeKey) {
            m = ContactMember::forCard(uidFor(itemKey(storeKey, member.contactNid)));
        } else if (!member.address.isEmpty()) {
            const QVariantMap known = contacts != nullptr ? contacts->contactForAddress(member.address) : QVariantMap();
            const QString uid = known.value(QStringLiteral("uid")).toString();
            m = known.isEmpty() ? ContactMember::forAddress(member.address, member.name)
                                : ContactMember::forCard(uid.isEmpty() ? known.value(QStringLiteral("id")).toString() : uid);
        }
        if (!m.uri.isEmpty() && !out.contains(m)) out.append(m);
    }
    return out;
}

} // namespace QindaOffice::Apps::Mail::OutlookRecords
