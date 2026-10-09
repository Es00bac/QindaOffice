// SPDX-License-Identifier: GPL-3.0-or-later
#include "hosted_calendar_provider.h"

#include <qindatk/protocol_client.h>

#include <QTimeZone>

namespace QindaOffice::Mail {
namespace {
const QString kEvents = QStringLiteral("events");

QString text(const QVariantMap &map, const char *key) { return map.value(QString::fromLatin1(key)).toString(); }
} // namespace

HostedCalendarProvider::HostedCalendarProvider(QObject *parent, const QString &accountId)
    : QObject(parent), m_accountId(accountId), m_calendarId(accountId)
{
}

HostedCalendarProvider::~HostedCalendarProvider()
{
    m_secret.fill(QLatin1Char('\0'));
}

void HostedCalendarProvider::setStore(QindaTK::LocalProductivityStore *store)
{
    if (m_store == store) return;
    m_store = store;
    emit configChanged();
}

void HostedCalendarProvider::setSecret(const QString &secret, const QString &mechanism)
{
    m_secret.fill(QLatin1Char('\0'));
    m_secret = secret;
    m_mechanism = mechanism.trimmed().toLower();
    emit secretChanged();
}

void HostedCalendarProvider::clearSecret()
{
    m_secret.fill(QLatin1Char('\0'));
    m_secret.clear();
    emit secretChanged();
}

QVariantList HostedCalendarProvider::conflicts() const
{
    QVariantList out;
    for (const QVariantMap &entry : m_conflicts) out.append(entry);
    return out;
}

bool HostedCalendarProvider::owns(const QVariantMap &row) const
{
    const QString calendar = text(row, "calendarId");
    return text(row, "remoteAccount") == m_accountId && (calendar.isEmpty() || calendar == m_calendarId);
}

QVariantMap HostedCalendarProvider::findRecord(const QString &id) const
{
    if (!m_store) return {};
    for (const QVariant &value : m_store->events()) {
        const QVariantMap row = value.toMap();
        if (text(row, "id") == id) return row;
    }
    return {};
}

CalendarSync::LocalItem HostedCalendarProvider::localItem(const QVariantMap &row) const
{
    CalendarSync::LocalItem item;
    item.id = text(row, "id");
    item.remoteId = text(row, "remoteId");
    item.version = row.value(versionField()).toString();
    item.syncDigest = text(row, "syncDigest");
    item.readOnly = row.value(QStringLiteral("readOnly")).toBool();
    item.dirty = item.remoteId.isEmpty() || eventDigest(row) != item.syncDigest;
    item.record = row;
    return item;
}

QVariantMap HostedCalendarProvider::merged(QVariantMap row, const QString &id, const QString &remoteId,
                                           const QString &version, const QVariantMap &fields) const
{
    for (const QString &key : digestFields() + remoteExtraFields()) {
        // AGENT-CONTRACT (M81, QindaMail's MailOrganizer): categories set here
        // and not yet on the server (`categoriesPending`) survive the pull;
        // the host writes them after the sync and clears the mark.
        if (key == QLatin1String("categories") && row.value(QStringLiteral("categoriesPending")).toBool()) continue;
        row.insert(key, fields.value(key));
    }
    row.insert(QStringLiteral("id"), id);
    row.insert(QStringLiteral("eventId"), id);
    row.insert(QStringLiteral("remoteAccount"), m_accountId);
    row.insert(QStringLiteral("remoteId"), remoteId);
    row.insert(versionField(), version);
    row.insert(QStringLiteral("syncState"), QStringLiteral("synced"));
    if (text(row, "calendarId").isEmpty()) row.insert(QStringLiteral("calendarId"), m_calendarId);
    row.insert(QStringLiteral("remoteBaseline"), digestSubset(fields));
    row.insert(QStringLiteral("syncDigest"), eventDigest(row));
    return row;
}

bool HostedCalendarProvider::sync(const QString &requestId)
{
    CalendarSync::Job job;
    job.mode = CalendarSync::Mode::Sync;
    if (m_store)
        for (const QVariant &value : m_store->events())
            if (owns(value.toMap())) job.items.append(localItem(value.toMap()));
    return start(Signal::Sync, requestId, job);
}

bool HostedCalendarProvider::removeRemote(const QString &requestId, const QString &collection, const QString &recordId)
{
    const QVariantMap row = findRecord(recordId);
    if (collection != kEvents || !owns(row) || text(row, "remoteId").isEmpty()) {
        deliver(Signal::Change, requestId, rejection(QStringLiteral("Event %1 is not synced with this calendar").arg(recordId)));
        return false;
    }
    CalendarSync::Job job;
    job.mode = CalendarSync::Mode::Delete;
    job.target = localItem(row);
    job.ifMatch = job.target.version;
    return start(Signal::Change, requestId, job);
}

bool HostedCalendarProvider::resolveConflict(const QString &requestId, const QString &collection,
                                             const QString &recordId, const QString &choice)
{
    const QVariantMap entry = m_conflicts.value(recordId);
    const QVariantMap row = findRecord(recordId);
    if (collection != kEvents || entry.isEmpty() || row.isEmpty()) {
        deliver(Signal::Change, requestId, rejection(QStringLiteral("No conflict is recorded for %1").arg(recordId)));
        return false;
    }
    if (choice == QLatin1String("remote")) {
        if (entry.value(QStringLiteral("remoteDeleted")).toBool()) m_store->remove(kEvents, recordId);
        else m_store->upsert(kEvents, merged(row, recordId, text(entry, "remoteId"), text(entry, "remoteVersion"),
                                             entry.value(QStringLiteral("remoteFields")).toMap()));
        m_conflicts.remove(recordId);
        emit conflictsChanged();
        deliver(Signal::Change, requestId, persist({{QStringLiteral("ok"), true}, {QStringLiteral("state"), QStringLiteral("accepted")},
                                                    {QStringLiteral("resolved"), choice}}));
        return true;
    }
    if (choice != QLatin1String("local")) {
        deliver(Signal::Change, requestId, rejection(QStringLiteral("Choose \"local\" or \"remote\"")));
        return false;
    }
    CalendarSync::Job job;
    job.mode = CalendarSync::Mode::Put;
    job.target = localItem(row);
    job.ifMatch = text(entry, "remoteVersion");
    job.create = entry.value(QStringLiteral("remoteDeleted")).toBool();
    return start(Signal::Change, requestId, job);
}

bool HostedCalendarProvider::listCalendars(const QString &requestId)
{
    CalendarSync::Job job;
    job.mode = CalendarSync::Mode::Calendars;
    return start(Signal::Calendars, requestId, job);
}

bool HostedCalendarProvider::start(Signal signal, const QString &requestId, CalendarSync::Job job)
{
    QString error = validate();
    if (error.isEmpty() && !m_store && signal != Signal::Calendars)
        error = QStringLiteral("No local store is configured for calendar sync");
    if (error.isEmpty() && (m_syncPastDays < 0 || m_syncFutureDays < 1 || m_syncPastDays + m_syncFutureDays > 3660))
        error = QStringLiteral("The sync window must reach forward and span at most ten years");
    if (!error.isEmpty()) {
        deliver(signal, requestId, rejection(error));
        return false;
    }
    // Whole UTC days, so every sync of one day asks for the same window.
    const QDate today = QDateTime::currentDateTimeUtc().date();
    job.window = {today.addDays(-m_syncPastDays).startOfDay(QTimeZone::UTC),
                  today.addDays(m_syncFutureDays + 1).startOfDay(QTimeZone::UTC)};
    job.prefix = idPrefix();
    job.accountId = m_accountId;
    const BackendFactory factory = backendFactory(job.window);
    ++m_pending;
    m_state = QStringLiteral("working");
    emit stateChanged();
    QindaTK::Protocol::startWorker(this, [factory, job] {
            const std::unique_ptr<CalendarSync::Backend> backend = factory();
            return CalendarSync::run(*backend, job);
        },
        [this, signal, requestId](const QVariantMap &result) {
            QVariantMap completed = result;
            if (m_store && signal != Signal::Calendars) {
                apply(result);
                completed = persist(completed);
            }
            --m_pending;
            deliver(signal, requestId, completed);
        });
    return true;
}

void HostedCalendarProvider::apply(const QVariantMap &result)
{
    const qsizetype before = m_conflicts.size();
    for (const QVariant &value : result.value(QStringLiteral("fetched")).toList()) {
        const QVariantMap entry = value.toMap();
        const QString id = text(entry, "id");
        m_store->upsert(kEvents, merged(findRecord(id), id, text(entry, "remoteId"), text(entry, "version"),
                                        entry.value(QStringLiteral("fields")).toMap()));
        m_conflicts.remove(id);
    }
    for (const QVariant &value : result.value(QStringLiteral("removed")).toList()) {
        const QString id = text(value.toMap(), "id");
        m_store->remove(kEvents, id);
        m_conflicts.remove(id);
    }
    for (const QVariant &value : result.value(QStringLiteral("pushed")).toList()) {
        const QVariantMap entry = value.toMap();
        const QString id = text(entry, "id");
        const QVariantMap row = findRecord(id);
        if (row.isEmpty()) continue;
        const QVariantMap fields = entry.value(QStringLiteral("fields")).toMap();
        const bool unchanged = eventDigest(row) == text(entry, "digest");
        if (!fields.isEmpty() && unchanged) {
            m_store->upsert(kEvents, merged(row, id, text(entry, "remoteId"), text(entry, "version"), fields));
        } else {
            // AGENT-NOTE: edited again during the upload (or the re-read
            // failed): keep the local content, take the new identity, and
            // leave the record dirty exactly when it differs from the server.
            QVariantMap kept = row;
            kept.insert(QStringLiteral("remoteAccount"), m_accountId);
            kept.insert(QStringLiteral("remoteId"), text(entry, "remoteId"));
            kept.insert(versionField(), text(entry, "version"));
            kept.insert(QStringLiteral("syncState"), QStringLiteral("synced"));
            if (text(kept, "calendarId").isEmpty()) kept.insert(QStringLiteral("calendarId"), m_calendarId);
            kept.insert(QStringLiteral("remoteBaseline"), !fields.isEmpty() ? digestSubset(fields)
                                                          : unchanged ? digestSubset(row) : QVariantMap());
            kept.insert(QStringLiteral("syncDigest"), !fields.isEmpty() ? eventDigest(fields) : text(entry, "digest"));
            m_store->upsert(kEvents, kept);
        }
        m_conflicts.remove(id);
    }
    for (const QVariant &value : result.value(QStringLiteral("conflicts")).toList()) {
        const QVariantMap entry = value.toMap();
        QVariantMap row = findRecord(text(entry, "id"));
        if (row.isEmpty()) continue;
        row.insert(QStringLiteral("syncState"), QStringLiteral("conflict"));
        m_store->upsert(kEvents, row);
        m_conflicts.insert(text(entry, "id"), entry);
    }
    if (before != m_conflicts.size() || !result.value(QStringLiteral("conflicts")).toList().isEmpty())
        emit conflictsChanged();
}

QVariantMap HostedCalendarProvider::persist(QVariantMap result)
{
    if (!m_autoSave || !m_store || m_store->save()) return result;
    result.insert(QStringLiteral("ok"), false);
    result.insert(QStringLiteral("state"), m_store->status() == QLatin1String("conflict")
                                               ? QStringLiteral("conflict") : QStringLiteral("rejected"));
    result.insert(QStringLiteral("error"), m_store->lastError());
    return result;
}

void HostedCalendarProvider::deliver(Signal signal, const QString &requestId, QVariantMap result)
{
    // Counts replace the job's record lists (docs/mail.md result table).
    result.insert(QStringLiteral("fetched"), result.value(QStringLiteral("fetched")).toList().size());
    result.insert(QStringLiteral("deleted"), result.take(QStringLiteral("removed")).toList().size());
    result.insert(QStringLiteral("conflicts"), result.value(QStringLiteral("conflicts")).toList().size());
    result.remove(QStringLiteral("pushed"));
    result.insert(QStringLiteral("created"), result.value(QStringLiteral("created")).toInt());
    result.insert(QStringLiteral("updated"), result.value(QStringLiteral("updated")).toInt());
    result.insert(QStringLiteral("requestId"), requestId);
    result.insert(QStringLiteral("conflictCount"), m_conflicts.size());
    const QStringList secrets{m_secret};
    result.insert(QStringLiteral("error"), QindaTK::Protocol::redact(text(result, "error"), secrets));
    QStringList transcript;
    for (const QString &line : result.value(QStringLiteral("transcript")).toStringList())
        transcript.append(QindaTK::Protocol::redact(line, secrets));
    result.insert(QStringLiteral("transcript"), transcript);
    m_state = text(result, "state");
    m_lastError = text(result, "error");
    emit stateChanged();
    // Synchronous rejections are delivered on the next event-loop turn, like
    // network results, so callers can connect after calling.
    QMetaObject::invokeMethod(this, [this, signal, requestId, result] {
        if (signal == Signal::Sync) emit syncFinished(requestId, result);
        else if (signal == Signal::Change) emit changeFinished(requestId, result);
        else emit calendarsFinished(requestId, result);
    }, Qt::QueuedConnection);
}

} // namespace QindaOffice::Mail
