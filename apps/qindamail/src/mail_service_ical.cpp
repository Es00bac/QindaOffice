// SPDX-License-Identifier: GPL-3.0-or-later
// MailService: iCalendar files (import into any writable calendar, export
// of one calendar), feed subscriptions (an `ics` account per feed) and the
// recurrence editor's preview.
#include "mail_service.h"
#include "ics_tasks.h"

#include "provider_calls.h"

#include <qindatk/ics_calendar_provider.h>
#include <qindatk/local_productivity_store.h>
#include <qindatk/recurrence_service.h>

#include <QFile>
#include <QFileInfo>
#include <QTimeZone>
#include <QUrl>

#include <memory>

namespace QindaOffice::Apps::Mail {

namespace {

using QindaTK::RecurrenceService;

QVariantMap findIn(const QVariantList &rows, const QString &id)
{
    for (const QVariant &value : rows) {
        if (value.toMap().value(QStringLiteral("id")).toString() == id) return value.toMap();
    }
    return {};
}

} // namespace

QString MailService::importIcs(const QUrl &url, const QString &calendarId)
{
    // M56 (D-061): VTODOs become tasks on the board (the calendar import
    // below takes only the events — QindaNote's action items carry both).
    importIcsTasks(url.isLocalFile() ? url.toLocalFile() : url.toString());
    const QString calendar = calendarId.isEmpty() ? QString::fromLatin1(kLocalCalendarId) : calendarId;
    const QString batch = beginBatch(QStringLiteral("import"), 1);
    if (calendarReadOnly(calendar)) {
        batchDone(batch, ProviderCalls::result({}, QStringLiteral("rejected"), tr("That calendar is read-only")));
        return batch;
    }
    const QString rid = ProviderCalls::newRequestId(QStringLiteral("ics"));
    m_calls->expect(rid, [this, batch, calendar](const QVariantMap &imported) {
        AccountRuntime *rt = runtimeForCalendar(calendar, nullptr);
        if (!imported.value(QStringLiteral("ok")).toBool() || rt == nullptr) {
            batchDone(batch, imported);
            return;
        }
        // Into an account's calendar: the events become that account's and
        // are written to its server.
        QVariantMap last;
        for (const QString &id : imported.value(QStringLiteral("eventIds")).toStringList()) {
            QVariantMap row = findIn(m_store->events(), id);
            if (row.isEmpty()) continue;
            row.insert(QStringLiteral("remoteAccount"), rt->id());
            static_cast<void>(m_store->upsert(QStringLiteral("events"), row));
            last = row;
        }
        static_cast<void>(saveStore());
        const QString sync = ProviderCalls::newRequestId(QStringLiteral("importsync"));
        auto connection = std::make_shared<QMetaObject::Connection>();
        *connection = connect(this, &MailService::eventChangeFinished, this,
                              [this, batch, sync, connection](const QString &id, const QString &state,
                                                              const QString &message) {
                                  if (id != sync) return;
                                  disconnect(*connection);
                                  batchDone(batch, ProviderCalls::result({}, state, message));
                              });
        writeThrough(QStringLiteral("events"), last.isEmpty() ? QVariantMap{{QStringLiteral("calendarId"), calendar}} : last,
                     sync, true);
    });
    static_cast<void>(m_ics->importFile(rid, url.isLocalFile() ? url.toLocalFile() : url.toString(), calendar));
    return batch;
}

int MailService::importIcsTasks(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return 0;
    }
    int added = 0;
    for (const IcsTask &task : parseIcsTasks(file.read(4 * 1024 * 1024))) {
        const QString id = addTask(task.title);
        if (id.isEmpty()) {
            continue;
        }
        QVariantMap update{{QStringLiteral("id"), id}};
        if (task.due.isValid()) {
            update.insert(QStringLiteral("dueDate"), task.due.toString(Qt::ISODate));
        }
        if (!task.description.isEmpty()) {
            update.insert(QStringLiteral("description"), task.description);
        }
        if (!task.priority.isEmpty()) {
            update.insert(QStringLiteral("priority"), task.priority);
        }
        if (task.done) {
            update.insert(QStringLiteral("status"), QStringLiteral("done"));
        }
        if (update.size() > 1) {
            static_cast<void>(updateTask(update));
        }
        ++added;
    }
    if (added > 0) {
        emit notify(tr("%n task(s) added", "", added), QFileInfo(path).fileName(), QStringLiteral("info"));
    }
    return added;
}

QString MailService::exportIcs(const QUrl &url, const QString &calendarId)
{
    const QString batch = beginBatch(QStringLiteral("export"), 1);
    const QString rid = ProviderCalls::newRequestId(QStringLiteral("ics"));
    m_calls->expect(rid, [this, batch](const QVariantMap &result) { batchDone(batch, result); });
    static_cast<void>(m_ics->exportFile(rid, url.isLocalFile() ? url.toLocalFile() : url.toString(),
                                        calendarId.isEmpty() ? QString::fromLatin1(kLocalCalendarId) : calendarId));
    return batch;
}

QString MailService::subscribeFeed(const QString &url, const QString &name, const QString &color)
{
    const QString display = name.trimmed().isEmpty() ? QUrl(url).host() : name.trimmed();
    const QVariantMap account{
        {QStringLiteral("kind"), QStringLiteral("ics")},
        {QStringLiteral("displayName"), display.isEmpty() ? tr("Subscribed calendar") : display},
        {QStringLiteral("syncIntervalMinutes"), 60},
        {QStringLiteral("calendar"),
         QVariantMap{{QStringLiteral("protocol"), QStringLiteral("ics")},
                     {QStringLiteral("url"), url.trimmed()},
                     {QStringLiteral("authMechanism"), QStringLiteral("basic")},
                     {QStringLiteral("calendars"),
                      QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("feed")},
                                               {QStringLiteral("name"), display},
                                               {QStringLiteral("color"), color},
                                               {QStringLiteral("enabled"), true}}}}}},
    };
    const QVariantMap added = m_accountStore->add(account);
    if (!added.value(QStringLiteral("ok")).toBool()) {
        const QString batch = beginBatch(QStringLiteral("subscribe"), 1);
        batchDone(batch, ProviderCalls::result({}, QStringLiteral("rejected"), added.value(QStringLiteral("error")).toString()));
        return batch;
    }
    return syncAccount(added.value(QStringLiteral("id")).toString());
}

QVariantList MailService::recurrencePreview(const QString &eventId, const QVariantMap &rule)
{
    const QVariantMap record = findIn(m_store->events(), eventId);
    if (record.isEmpty()) return {};
    const QString start = record.value(QStringLiteral("startLocal")).toString();
    const QString zone = record.value(QStringLiteral("zone")).toString();
    const qint64 span = RecurrenceService::spanSeconds(start, record.value(QStringLiteral("endLocal")).toString(), zone);
    QDate date;
    QTime time;
    bool dateOnly = false;
    if (span < 0 || !RecurrenceService::parseLocal(start, &date, &time, &dateOnly)) return {};
    const QDateTime from = date.startOfDay(QTimeZone(zone.toUtf8())).toUTC();
    return m_recurrence->expand(eventId, start, int(span), zone, rule, from.toString(Qt::ISODate),
                                from.addYears(5).toString(Qt::ISODate), 12);
}

} // namespace QindaOffice::Apps::Mail
