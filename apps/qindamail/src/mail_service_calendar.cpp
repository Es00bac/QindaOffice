// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: calendars, the projected events of the visible range, event
// edits (store first, then written through the owning provider).
// iCalendar files and feeds: mail_service_ical.cpp.
//
// AGENT-CONTRACT (CalendarGridView, docs/productivity-store.md "Calendar
// grid requests"): `shift-local` is LocalProductivityStore::shiftEventLocal;
// `resize-local` rewrites endLocal (series), records an override
// (occurrence) or splits the series first (following). Every edit is saved,
// then the calendar's provider syncs, and eventChangeFinished carries that
// sync's state (a local calendar answers `accepted` after the save).
#include "mail_service.h"

#include "provider_calls.h"

#include <qindatk/local_calendar_projection_model.h>
#include <qindatk/local_productivity_store.h>
#include <qindatk/recurrence_service.h>

#include <QTimeZone>
#include <QTimer>
#include <QUuid>


namespace QindaOffice::Apps::Mail {

namespace {

using QindaTK::RecurrenceService;

QString eventOrTaskTitle(const QVariantMap &map)
{
    return map.value(QStringLiteral("title")).toString().trimmed();
}

// Wall-clock arithmetic on a stored local text (no zone: 09:00 + 30 min is
// 09:30 on any day); all-day texts move by whole days only.
QString addLocalMinutes(const QString &text, int minutes)
{
    QDate date;
    QTime time;
    bool dateOnly = false;
    if (!RecurrenceService::parseLocal(text, &date, &time, &dateOnly)) return {};
    if (dateOnly) {
        if (minutes % 1440 != 0) return {};
        return date.addDays(minutes / 1440).toString(Qt::ISODate);
    }
    return QDateTime(date, time, QTimeZone::UTC).addSecs(qint64(minutes) * 60).toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
}

QVariantMap findIn(const QVariantList &rows, const QString &id)
{
    for (const QVariant &value : rows) {
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    }
    return {};
}

// A split tail is a NEW remote item for every adapter: drop the identity the
// store's split kept for Exchange/Graph (it strips the CalDAV fields itself).
void detachTail(QindaTK::LocalProductivityStore *store, const QString &tailId)
{
    QVariantMap tail = findIn(store->events(), tailId);
    if (tail.isEmpty()) return;
    for (const char *field : {"remoteId", "remoteChangeKey", "remoteEtag", "remoteBaseline", "remoteOccurrences",
                              "remoteItemType", "syncDigest", "syncState"})
        tail.remove(QString::fromLatin1(field));
    static_cast<void>(store->upsert(QStringLiteral("events"), tail));
}

} // namespace

QVariantList MailService::calendars() const
{
    QVariantList list;
    for (const AccountRuntime *rt : m_runtimes) {
        for (const CalendarBinding &binding : rt->calendars()) {
            list.append(QVariantMap{{QStringLiteral("id"), binding.id}, {QStringLiteral("accountId"), rt->id()},
                                    {QStringLiteral("name"), binding.name}, {QStringLiteral("color"), binding.color},
                                    {QStringLiteral("visible"), !m_hiddenCalendars.contains(binding.id)},
                                    {QStringLiteral("readOnly"), binding.readOnly},
                                    {QStringLiteral("service"), binding.service}});
        }
    }
    const QString local = QString::fromLatin1(kLocalCalendarId);
    list.append(QVariantMap{{QStringLiteral("id"), local}, {QStringLiteral("accountId"), QString::fromLatin1(kLocalAccountId)},
                            {QStringLiteral("name"), tr("Calendar")}, {QStringLiteral("color"), QString()},
                            {QStringLiteral("visible"), !m_hiddenCalendars.contains(local)},
                            {QStringLiteral("readOnly"), false}, {QStringLiteral("service"), QStringLiteral("local")}});
    return list;
}

QVariantList MailService::writableCalendars() const
{
    QVariantList list;
    for (const QVariant &value : calendars()) {
        if (!value.toMap().value(QStringLiteral("readOnly")).toBool()) list.append(value);
    }
    return list;
}

AccountRuntime *MailService::runtimeForCalendar(const QString &calendarId, const CalendarBinding **binding) const
{
    for (AccountRuntime *rt : m_runtimes) {
        if (const CalendarBinding *found = rt->calendar(calendarId)) {
            if (binding != nullptr) *binding = found;
            return rt;
        }
    }
    return nullptr;
}

bool MailService::calendarReadOnly(const QString &calendarId) const
{
    const CalendarBinding *binding = nullptr;
    return runtimeForCalendar(calendarId, &binding) != nullptr && binding->readOnly;
}

void MailService::setCalendarVisible(const QString &calendarId, bool visible)
{
    if (visible == !m_hiddenCalendars.contains(calendarId)) return;
    if (visible) m_hiddenCalendars.remove(calendarId);
    else m_hiddenCalendars.insert(calendarId);
    QStringList hidden(m_hiddenCalendars.cbegin(), m_hiddenCalendars.cend());
    hidden.sort();
    saveSetting(QStringLiteral("hiddenCalendars"), hidden);
    emit calendarsChanged();
    rebuildEvents();
}

bool MailService::projectRange(const QString &startDate, const QString &endDate)
{
    const QDate start = QDate::fromString(startDate.left(10), Qt::ISODate);
    const QDate end = QDate::fromString(endDate.left(10), Qt::ISODate);
    if (!start.isValid() || !end.isValid() || end <= start) return false;
    m_rangeStart = start.toString(Qt::ISODate);
    m_rangeEnd = end.toString(Qt::ISODate);
    // A day of margin on each side: events are in their own zones, the
    // bounds are UTC (docs/productivity-store.md "Calendar grid requests").
    const QString from = start.addDays(-1).startOfDay().toUTC().toString(Qt::ISODate);
    const QString to = end.addDays(1).startOfDay().toUTC().toString(Qt::ISODate);
    const bool ok = m_projection->project(from, to, 2000);
    rebuildEvents();
    return ok;
}

void MailService::rebuildEvents()
{
    QHash<QString, QVariantMap> calendarInfo;
    for (const QVariant &value : calendars()) {
        calendarInfo.insert(value.toMap().value(QStringLiteral("id")).toString(), value.toMap());
    }
    QVariantList rows;
    for (const QVariant &value : m_projection->occurrences()) {
        QVariantMap row = value.toMap();
        QString calendar = row.value(QStringLiteral("calendarId")).toString();
        if (calendar.isEmpty() || !calendarInfo.contains(calendar)) calendar = QString::fromLatin1(kLocalCalendarId);
        if (m_hiddenCalendars.contains(calendar)) continue;
        // The projection ran a day wider on each side (zones); keep the rows
        // that meet the requested local days [m_rangeStart, m_rangeEnd).
        if (!m_rangeStart.isEmpty()
            && (row.value(QStringLiteral("startLocal")).toString().left(10) >= m_rangeEnd
                || row.value(QStringLiteral("endLocal")).toString() <= m_rangeStart))
            continue;
        const QVariantMap info = calendarInfo.value(calendar);
        row.insert(QStringLiteral("calendarId"), calendar);
        row.insert(QStringLiteral("calendarName"), info.value(QStringLiteral("name")));
        row.insert(QStringLiteral("calendarColor"), info.value(QStringLiteral("color")));
        row.insert(QStringLiteral("readOnly"),
                   row.value(QStringLiteral("readOnly")).toBool() || info.value(QStringLiteral("readOnly")).toBool());
        rows.append(row);
    }
    const QString error = m_projection->error();
    if (rows == m_events && error == m_eventsError) return;
    m_events = rows;
    m_eventsError = error;
    emit eventsChanged();
}

void MailService::finishEventChange(const QString &requestId, const QString &state, const QString &message)
{
    QTimer::singleShot(0, this, [this, requestId, state, message] {
        emit eventChangeFinished(requestId, state, message);
    });
}

void MailService::writeThrough(const QString &collection, const QVariantMap &record, const QString &requestId,
                               bool eventSignal)
{
    const auto finish = [this, requestId, eventSignal](const QString &state, const QString &message) {
        if (eventSignal) {
            finishEventChange(requestId, state, message);
        } else {
            QTimer::singleShot(0, this, [this, requestId, state, message] {
                emit operationFinished(requestId, state, message);
            });
        }
    };
    const CalendarBinding *binding = nullptr;
    AccountRuntime *rt = nullptr;
    if (collection == QLatin1String("events")) {
        rt = runtimeForCalendar(record.value(QStringLiteral("calendarId")).toString(), &binding);
    } else if (AccountRuntime *owner = runtime(record.value(QStringLiteral("remoteAccount")).toString())) {
        for (const CalendarBinding &candidate : owner->calendars()) {
            if (candidate.service == QLatin1String("caldav")) {
                rt = owner;
                binding = &candidate;
                break;
            }
        }
    }
    if (rt == nullptr || binding == nullptr || binding->provider == nullptr) {
        finish(QStringLiteral("accepted"), QString());
        return;
    }
    QObject *provider = binding->provider;
    rt->run(binding->service, provider,
            [provider](const QString &rid) {
                bool started = false;
                QMetaObject::invokeMethod(provider, "sync", Q_RETURN_ARG(bool, started), Q_ARG(QString, rid));
                return started;
            },
            [finish](const QVariantMap &result) {
                finish(result.value(QStringLiteral("state")).toString(), result.value(QStringLiteral("error")).toString());
            });
}

QString MailService::createEvent(const QVariantMap &event)
{
    const QString requestId = ProviderCalls::newRequestId(QStringLiteral("event"));
    const QString calendarId = event.value(QStringLiteral("calendarId")).toString().isEmpty()
        ? QString::fromLatin1(kLocalCalendarId) : event.value(QStringLiteral("calendarId")).toString();
    const CalendarBinding *binding = nullptr;
    AccountRuntime *rt = runtimeForCalendar(calendarId, &binding);
    if (calendarId != QLatin1String(kLocalCalendarId) && rt == nullptr) {
        finishEventChange(requestId, QStringLiteral("rejected"), tr("That calendar is not available"));
        return requestId;
    }
    if (binding != nullptr && binding->readOnly) {
        finishEventChange(requestId, QStringLiteral("rejected"), tr("That calendar is read-only"));
        return requestId;
    }
    const QString zone = event.value(QStringLiteral("zone")).toString().isEmpty()
        ? QString::fromLatin1(QTimeZone::systemTimeZoneId()) : event.value(QStringLiteral("zone")).toString();
    const QString start = RecurrenceService::canonicalLocal(event.value(QStringLiteral("startLocal")).toString());
    const QString end = RecurrenceService::canonicalLocal(event.value(QStringLiteral("endLocal")).toString());
    if (!QTimeZone(zone.toUtf8()).isValid() || RecurrenceService::spanSeconds(start, end, zone) <= 0) {
        finishEventChange(requestId, QStringLiteral("rejected"), tr("The event must end after it starts"));
        return requestId;
    }
    QVariantMap record = event;
    const QString id = event.value(QStringLiteral("id")).toString().isEmpty()
        ? QStringLiteral("event:") + QUuid::createUuid().toString(QUuid::WithoutBraces) : event.value(QStringLiteral("id")).toString();
    record.insert(QStringLiteral("id"), id);
    record.insert(QStringLiteral("title"), eventOrTaskTitle(event).isEmpty() ? tr("New event") : eventOrTaskTitle(event));
    record.insert(QStringLiteral("startLocal"), start);
    record.insert(QStringLiteral("endLocal"), end);
    record.insert(QStringLiteral("zone"), zone);
    record.insert(QStringLiteral("allDay"), start.size() == 10);
    record.insert(QStringLiteral("calendarId"), calendarId);
    if (!record.contains(QStringLiteral("exceptions"))) record.insert(QStringLiteral("exceptions"), QVariantList());
    if (!record.contains(QStringLiteral("overrides"))) record.insert(QStringLiteral("overrides"), QVariantList());
    if (record.value(QStringLiteral("recurrenceRule")).toMap().isEmpty()) record.remove(QStringLiteral("recurrenceRule"));
    if (rt != nullptr) record.insert(QStringLiteral("remoteAccount"), rt->id());
    const QVariantMap upserted = m_store->upsert(QStringLiteral("events"), record);
    QString error = upserted.value(QStringLiteral("error")).toString();
    if (!upserted.value(QStringLiteral("ok")).toBool() || !saveStore(&error)) {
        finishEventChange(requestId, QStringLiteral("rejected"), error);
        return requestId;
    }
    writeThrough(QStringLiteral("events"), record, requestId, true);
    return requestId;
}

QString MailService::updateEvent(const QVariantMap &event)
{
    const QString requestId = ProviderCalls::newRequestId(QStringLiteral("event"));
    const QString id = event.value(QStringLiteral("id")).toString();
    QVariantMap record = findIn(m_store->events(), id);
    if (record.isEmpty()) {
        finishEventChange(requestId, QStringLiteral("rejected"), tr("The event no longer exists"));
        return requestId;
    }
    const QString calendarId = record.value(QStringLiteral("calendarId")).toString();
    if (record.value(QStringLiteral("readOnly")).toBool() || calendarReadOnly(calendarId)) {
        finishEventChange(requestId, QStringLiteral("rejected"), tr("That calendar is read-only"));
        return requestId;
    }
    const QString target = event.value(QStringLiteral("calendarId"), calendarId).toString();
    if (target != calendarId && (runtimeForCalendar(target, nullptr) != nullptr
                                 || runtimeForCalendar(calendarId, nullptr) != nullptr)) {
        finishEventChange(requestId, QStringLiteral("rejected"), tr("An account's event stays in its calendar"));
        return requestId;
    }
    QVariantMap result;
    const QString occurrence = event.value(QStringLiteral("occurrenceId")).toString();
    const QString scope = event.value(QStringLiteral("scope"), QStringLiteral("series")).toString();
    if ((scope != QLatin1String("series") && scope != QLatin1String("occurrence"))
        || (scope == QLatin1String("occurrence") && (occurrence.isEmpty() || eventOccurrence(id, occurrence).isEmpty()))) {
        finishEventChange(requestId, QStringLiteral("rejected"), tr("Choose a valid occurrence or the entire series"));
        return requestId;
    }
    if (!occurrence.isEmpty() && event.value(QStringLiteral("scope")).toString() == QLatin1String("occurrence")) {
        QVariantMap fields;
        for (const char *key : {"title", "description", "location", "startLocal", "endLocal"}) {
            if (event.contains(QString::fromLatin1(key))) fields.insert(QString::fromLatin1(key), event.value(QString::fromLatin1(key)));
        }
        result = m_store->setOccurrenceOverride(id, occurrence, fields);
    } else {
        for (const char *key : {"title", "description", "location", "startLocal", "endLocal", "zone", "calendarId"}) {
            if (event.contains(QString::fromLatin1(key))) record.insert(QString::fromLatin1(key), event.value(QString::fromLatin1(key)));
        }
        const QString start = RecurrenceService::canonicalLocal(record.value(QStringLiteral("startLocal")).toString());
        const QString end = RecurrenceService::canonicalLocal(record.value(QStringLiteral("endLocal")).toString());
        if (RecurrenceService::spanSeconds(start, end, record.value(QStringLiteral("zone")).toString()) <= 0) {
            finishEventChange(requestId, QStringLiteral("rejected"), tr("The event must end after it starts"));
            return requestId;
        }
        record.insert(QStringLiteral("startLocal"), start);
        record.insert(QStringLiteral("endLocal"), end);
        record.insert(QStringLiteral("allDay"), start.size() == 10);
        if (event.contains(QStringLiteral("recurrenceRule"))) {
            // Validate and normalize in a temporary store before changing the
            // live event. An invalid rule must not leave half an edit behind.
            QindaTK::LocalProductivityStore candidate;
            result = candidate.upsert(QStringLiteral("events"), record);
            if (result.value(QStringLiteral("ok")).toBool()) result = candidate.setEventRecurrence(id, event.value(QStringLiteral("recurrenceRule")).toMap());
            if (result.value(QStringLiteral("ok")).toBool()) record = candidate.events().first().toMap();
        }
        if (result.isEmpty() || result.value(QStringLiteral("ok")).toBool()) result = m_store->upsert(QStringLiteral("events"), record);
    }
    QString error = result.value(QStringLiteral("error")).toString();
    if (!result.value(QStringLiteral("ok")).toBool() || !saveStore(&error)) {
        finishEventChange(requestId, QStringLiteral("rejected"), error);
        return requestId;
    }
    writeThrough(QStringLiteral("events"), findIn(m_store->events(), id), requestId, true);
    return requestId;
}

QString MailService::deleteEvent(const QString &eventId, const QString &occurrenceId, const QString &scope)
{
    const QString requestId = ProviderCalls::newRequestId(QStringLiteral("event"));
    const QVariantMap record = findIn(m_store->events(), eventId);
    const QString calendarId = record.value(QStringLiteral("calendarId")).toString();
    if (record.isEmpty() || record.value(QStringLiteral("readOnly")).toBool() || calendarReadOnly(calendarId)) {
        finishEventChange(requestId, QStringLiteral("rejected"),
                          record.isEmpty() ? tr("The event no longer exists") : tr("That calendar is read-only"));
        return requestId;
    }
    const bool recurring = !record.value(QStringLiteral("recurrenceRule")).toMap().isEmpty();
    if ((scope != QLatin1String("series") && scope != QLatin1String("occurrence") && scope != QLatin1String("following"))
        || (scope != QLatin1String("series") && (occurrenceId.isEmpty() || eventOccurrence(eventId, occurrenceId).isEmpty()))) {
        finishEventChange(requestId, QStringLiteral("rejected"), tr("Choose a valid occurrence or the entire series"));
        return requestId;
    }
    const QString local = occurrenceId.mid(eventId.size() + 1);
    const QDate day = QDate::fromString(local.left(10), Qt::ISODate);
    const QDate first = QDate::fromString(record.value(QStringLiteral("startLocal")).toString().left(10), Qt::ISODate);
    QVariantMap result;
    if (recurring && scope == QLatin1String("occurrence") && !occurrenceId.isEmpty()) {
        result = m_store->setOccurrenceExcluded(eventId, occurrenceId, true);
    } else if (recurring && scope == QLatin1String("following") && day.isValid() && day > first) {
        QVariantMap rule = record.value(QStringLiteral("recurrenceRule")).toMap();
        rule.insert(QStringLiteral("endMode"), QStringLiteral("until"));
        rule.insert(QStringLiteral("until"), day.addDays(-1).toString(Qt::ISODate));
        rule.remove(QStringLiteral("count"));
        result = m_store->setEventRecurrence(eventId, rule);
    } else {
        // The whole event: a synced one is deleted on the server first.
        const CalendarBinding *binding = nullptr;
        AccountRuntime *rt = runtimeForCalendar(calendarId, &binding);
        const bool synced = !record.value(QStringLiteral("remoteHref")).toString().isEmpty()
            || !record.value(QStringLiteral("remoteId")).toString().isEmpty();
        if (rt != nullptr && binding->provider != nullptr && synced) {
            QObject *provider = binding->provider;
            rt->run(binding->service, provider,
                    [provider, eventId](const QString &rid) {
                        bool started = false;
                        QMetaObject::invokeMethod(provider, "removeRemote", Q_RETURN_ARG(bool, started), Q_ARG(QString, rid),
                                                  Q_ARG(QString, QStringLiteral("events")), Q_ARG(QString, eventId));
                        return started;
                    },
                    [this, requestId](const QVariantMap &removed) {
                        finishEventChange(requestId, removed.value(QStringLiteral("state")).toString(),
                                          removed.value(QStringLiteral("error")).toString());
                    });
            return requestId;
        }
        result = m_store->remove(QStringLiteral("events"), eventId);
    }
    QString error = result.value(QStringLiteral("error")).toString();
    if (!result.value(QStringLiteral("ok")).toBool() || !saveStore(&error)) {
        finishEventChange(requestId, QStringLiteral("rejected"), error);
        return requestId;
    }
    writeThrough(QStringLiteral("events"), record, requestId, true);
    return requestId;
}

void MailService::applyChange(const QString &requestId, const QString &eventId, const QVariantMap &proposal)
{
    const QVariantMap record = findIn(m_store->events(), eventId);
    if (record.isEmpty() || record.value(QStringLiteral("readOnly")).toBool()
        || calendarReadOnly(record.value(QStringLiteral("calendarId")).toString())) {
        finishEventChange(requestId, QStringLiteral("rejected"),
                          record.isEmpty() ? tr("The event no longer exists") : tr("That calendar is read-only"));
        return;
    }
    const QString kind = proposal.value(QStringLiteral("kind")).toString();
    const int minutes = proposal.value(QStringLiteral("minutes")).toInt();
    const bool recurring = !record.value(QStringLiteral("recurrenceRule")).toMap().isEmpty();
    QString scope = proposal.value(QStringLiteral("scope"), QStringLiteral("series")).toString();
    const QString occurrenceId = proposal.value(QStringLiteral("occurrenceId")).toString();
    if (!recurring || occurrenceId.isEmpty()) scope = QStringLiteral("series");
    QVariantMap result;
    if (kind == QLatin1String("shift-local")) {
        result = m_store->shiftEventLocal(eventId, minutes, scope, occurrenceId);
        if (scope == QLatin1String("following") && result.value(QStringLiteral("ok")).toBool())
            detachTail(m_store, result.value(QStringLiteral("eventId")).toString());
    } else if (kind == QLatin1String("resize-local")) {
        QString target = eventId;
        if (scope == QLatin1String("following")) {
            // Split with a round trip of the store's own "following" move
            // (a day out, the new series a day back), then resize the tail.
            result = m_store->shiftEventLocal(eventId, 1440, QStringLiteral("following"), occurrenceId);
            if (result.value(QStringLiteral("ok")).toBool()) {
                target = result.value(QStringLiteral("eventId")).toString();
                if (target != eventId) detachTail(m_store, target);
                result = m_store->shiftEventLocal(target, -1440, QStringLiteral("series"));
            }
            scope = QStringLiteral("series");
        }
        if (result.isEmpty() || result.value(QStringLiteral("ok")).toBool()) {
            const QVariantMap row = findIn(m_store->events(), target);
            if (scope == QLatin1String("series")) {
                const QString end = addLocalMinutes(row.value(QStringLiteral("endLocal")).toString(), minutes);
                if (end.isEmpty() || RecurrenceService::spanSeconds(row.value(QStringLiteral("startLocal")).toString(), end,
                                                                    row.value(QStringLiteral("zone")).toString()) <= 0) {
                    result = {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("The event must end after it starts")}};
                } else {
                    QVariantMap resized = row;
                    resized.insert(QStringLiteral("endLocal"), end);
                    result = m_store->upsert(QStringLiteral("events"), resized);
                }
            } else {
                // One occurrence: its current times (an earlier override
                // included) from the projection, else from the series.
                QString start = occurrenceId.mid(eventId.size() + 1);
                QString end;
                for (const QVariant &value : m_projection->occurrences()) {
                    const QVariantMap occurrence = value.toMap();
                    if (occurrence.value(QStringLiteral("occurrenceId")).toString() == occurrenceId) {
                        start = occurrence.value(QStringLiteral("startLocal")).toString();
                        end = occurrence.value(QStringLiteral("endLocal")).toString();
                    }
                }
                if (end.isEmpty()) {
                    const qint64 span = RecurrenceService::spanSeconds(row.value(QStringLiteral("startLocal")).toString(),
                                                                       row.value(QStringLiteral("endLocal")).toString(),
                                                                       row.value(QStringLiteral("zone")).toString());
                    end = addLocalMinutes(start, int(span / 60));
                }
                const QString newEnd = addLocalMinutes(end, minutes);
                result = newEnd.isEmpty() || newEnd <= RecurrenceService::canonicalLocal(start)
                    ? QVariantMap{{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("The event must end after it starts")}}
                    : m_store->setOccurrenceOverride(eventId, occurrenceId,
                                                     {{QStringLiteral("startLocal"), RecurrenceService::canonicalLocal(start)},
                                                      {QStringLiteral("endLocal"), newEnd}});
            }
        }
    } else {
        result = {{QStringLiteral("ok"), false}, {QStringLiteral("error"), tr("Unknown change %1").arg(kind)}};
    }
    QString error = result.value(QStringLiteral("error")).toString();
    if (!result.value(QStringLiteral("ok")).toBool() || !saveStore(&error)) {
        finishEventChange(requestId, QStringLiteral("rejected"), error);
        return;
    }
    writeThrough(QStringLiteral("events"), record, requestId, true);
}

QVariantMap MailService::eventById(const QString &eventId) const
{
    QVariantMap record = findIn(m_store->events(), eventId);
    if (record.isEmpty()) return {};
    const QString calendar = record.value(QStringLiteral("calendarId")).toString();
    for (const QVariant &value : calendars()) {
        const QVariantMap info = value.toMap();
        if (info.value(QStringLiteral("id")).toString() != calendar) continue;
        record.insert(QStringLiteral("calendarName"), info.value(QStringLiteral("name")));
        record.insert(QStringLiteral("calendarColor"), info.value(QStringLiteral("color")));
        record.insert(QStringLiteral("readOnly"), record.value(QStringLiteral("readOnly")).toBool()
                                                      || info.value(QStringLiteral("readOnly")).toBool());
    }
    return record;
}

} // namespace QindaOffice::Apps::Mail
