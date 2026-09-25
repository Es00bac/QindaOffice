// SPDX-License-Identifier: GPL-3.0-or-later
#include "calendar_sync.h"

#include <QSet>

namespace QindaOffice::Mail::CalendarSync {
namespace {
using Failure = QindaTK::Protocol::Failure;

struct PendingPush {
    const LocalItem *item = nullptr;
    WriteOutcome outcome;
};

class Engine {
public:
    Engine(Backend &backend, const Job &job) : m_backend(backend), m_job(job) {}

    void sync()
    {
        QHash<QString, QString> zones;
        QHash<QString, const LocalItem *> byRemote;
        for (const LocalItem &item : m_job.items) {
            if (item.remoteId.isEmpty()) continue;
            byRemote.insert(item.remoteId, &item);
            zones.insert(item.remoteId, item.record.value(QStringLiteral("zone")).toString());
        }
        m_backend.setPreferredZones(zones);
        QMap<QString, RemoteEvent> listed;
        if (!m_backend.list(&listed)) return;

        // A synced record the window listing does not show was deleted, or
        // lies outside the window. Ask for the ones that matter: edited here,
        // recurring (a series reaches any window) or expected in the window.
        QStringList verify;
        for (const LocalItem &item : m_job.items)
            if (!item.remoteId.isEmpty() && !listed.contains(item.remoteId)
                && (item.dirty || isRecurring(item.record) || overlapsWindow(item.record, m_job.window)))
                verify.append(item.remoteId);
        QSet<QString> gone;
        if (!verify.isEmpty()) {
            QMap<QString, RemoteEvent> found;
            if (!m_backend.fetch(verify, &found)) return;
            for (const QString &id : std::as_const(verify)) {
                if (found.contains(id)) listed.insert(id, found.value(id));
                else gone.insert(id);
            }
        }
        QStringList needed;
        for (auto it = listed.cbegin(); it != listed.cend(); ++it) {
            const LocalItem *local = byRemote.value(it.key());
            if ((!local || local->version != it->version) && !it->hasFields) needed.append(it.key());
        }
        if (!needed.isEmpty()) {
            QMap<QString, RemoteEvent> full;
            if (!m_backend.fetch(needed, &full)) return;
            for (const QString &id : std::as_const(needed)) {
                if (full.contains(id)) listed.insert(id, full.value(id));
                else if (listed.remove(id) && byRemote.contains(id)) gone.insert(id);
            }
        }

        QSet<QString> settled;        // local ids fetched, conflicted, pushed or removed
        for (auto it = listed.cbegin(); it != listed.cend(); ++it) {
            const LocalItem *local = byRemote.value(it.key());
            // The calendar's rights can change without a new item version
            // (Graph derives readOnly from the calendar): refresh a clean
            // record, and never push an edit the server will refuse.
            const bool rightsChanged = local && it->hasFields
                && it->fields.value(QStringLiteral("readOnly")).toBool() != local->readOnly;
            if (local && local->version == it->version && !rightsChanged) continue;
            if (local && local->dirty && rightsChanged && it->fields.value(QStringLiteral("readOnly")).toBool()) {
                settled.insert(local->id);
                conflictFrom(*local, &*it, false, QStringLiteral("The server marks this event read-only"));
                continue;
            }
            if (local && local->dirty && local->version == it->version) continue;   // pushed below
            if (local && local->dirty) {
                // The server moved on, but may still hold exactly what we last
                // synced (a version bump without content change): then this is
                // a plain local edit, otherwise a real conflict.
                settled.insert(local->id);
                if (eventDigest(it->fields) == local->syncDigest) push(*local, it->version);
                else conflictFrom(*local, &*it, false, {});
                continue;
            }
            if (local) settled.insert(local->id);
            m_fetched.append(QVariantMap{
                {QStringLiteral("id"), local ? local->id : localIdFor(m_job.prefix, m_job.accountId, it.key())},
                {QStringLiteral("remoteId"), it.key()}, {QStringLiteral("version"), it->version},
                {QStringLiteral("fields"), it->fields}});
        }
        for (const QString &id : std::as_const(gone)) {
            const LocalItem *local = byRemote.value(id);
            if (!local) continue;
            settled.insert(local->id);
            if (local->dirty) conflictFrom(*local, nullptr, true, {});
            else m_removed.append(QVariantMap{{QStringLiteral("id"), local->id}});
        }
        for (const LocalItem &item : m_job.items) {
            if (settled.contains(item.id) || failed()) continue;
            if (item.remoteId.isEmpty()) push(item, {});
            else if (item.dirty) push(item, item.version);
        }
    }

    void put()
    {
        push(m_job.target, m_job.create ? QString() : m_job.ifMatch, m_job.create);
    }

    void remove()
    {
        const WriteOutcome outcome = m_backend.remove(m_job.target, m_job.ifMatch);
        if (outcome.kind == WriteOutcome::Done || outcome.kind == WriteOutcome::Gone)
            m_removed.append(QVariantMap{{QStringLiteral("id"), m_job.target.id}});
        else if (outcome.kind == WriteOutcome::Conflict) conflict(m_job.target, outcome.error);
        else if (!failed()) m_errors.append(outcome.error);
    }

    void calendars() { m_backend.listCalendars(&m_calendars); }

    void finishPushes()
    {
        QStringList ids;
        for (const PendingPush &push : std::as_const(m_pending)) {
            ids.append(push.outcome.remoteId);
            m_backend.preferZone(push.outcome.remoteId, push.item->record.value(QStringLiteral("zone")).toString());
        }
        // Adopt what the server now holds (its canonical text, its new version)
        // so the next sync is quiet. If this read fails the push still counts:
        // the identity is kept and the next sync fetches the content.
        QMap<QString, RemoteEvent> current;
        if (!ids.isEmpty()) m_backend.fetch(ids, &current);
        for (const PendingPush &push : std::as_const(m_pending)) {
            const bool created = push.item->remoteId.isEmpty() || push.item->remoteId != push.outcome.remoteId;
            QVariantMap entry{{QStringLiteral("id"), push.item->id}, {QStringLiteral("remoteId"), push.outcome.remoteId},
                              {QStringLiteral("version"), push.outcome.version},
                              {QStringLiteral("digest"), eventDigest(push.item->record)},
                              {QStringLiteral("created"), created}};
            if (current.contains(push.outcome.remoteId)) {
                entry.insert(QStringLiteral("version"), current.value(push.outcome.remoteId).version);
                entry.insert(QStringLiteral("fields"), current.value(push.outcome.remoteId).fields);
            }
            m_pushed.append(entry);
            ++(created ? m_created : m_updated);
        }
    }

    QVariantMap result()
    {
        HttpTransport &transport = m_backend.transport();
        const Failure failure = transport.failure();
        QStringList errors = m_errors + m_backend.errors();
        const QString state = failure != Failure::None ? QindaTK::Protocol::failureState(failure)
            : !errors.isEmpty() ? QStringLiteral("rejected")
            : !m_conflicts.isEmpty() ? QStringLiteral("conflict") : QStringLiteral("accepted");
        QString error = failure != Failure::None ? transport.error() : errors.join(QStringLiteral("; "));
        if (error.isEmpty() && !m_conflicts.isEmpty())
            error = QStringLiteral("%1 event(s) changed both here and on the server").arg(m_conflicts.size());
        QVariantMap out{{QStringLiteral("ok"), state == QLatin1String("accepted")}, {QStringLiteral("state"), state},
            {QStringLiteral("error"), transport.redact(error)}, {QStringLiteral("retryable"), failure == Failure::Offline},
            {QStringLiteral("fetched"), m_fetched}, {QStringLiteral("removed"), m_removed},
            {QStringLiteral("pushed"), m_pushed}, {QStringLiteral("conflicts"), m_conflicts},
            {QStringLiteral("created"), m_created}, {QStringLiteral("updated"), m_updated},
            {QStringLiteral("transcript"), transport.transcript()}};
        if (m_job.mode == Mode::Calendars) out.insert(QStringLiteral("calendars"), m_calendars);
        if (transport.retryAfterSeconds() >= 0) out.insert(QStringLiteral("retryAfterSeconds"), transport.retryAfterSeconds());
        if (transport.authRejected()) out.insert(QStringLiteral("authRejected"), true);
        return out;
    }

private:
    bool failed() { return m_backend.transport().failure() != Failure::None; }

    void conflictFrom(const LocalItem &item, const RemoteEvent *remote, bool gone, const QString &reason)
    {
        QVariantMap entry{{QStringLiteral("collection"), QStringLiteral("events")}, {QStringLiteral("id"), item.id},
                          {QStringLiteral("remoteId"), item.remoteId}, {QStringLiteral("remoteDeleted"), gone}};
        if (!reason.isEmpty()) entry.insert(QStringLiteral("reason"), reason);
        if (!gone && remote) {
            entry.insert(QStringLiteral("remoteVersion"), remote->version);
            entry.insert(QStringLiteral("remoteFields"), remote->fields);
        }
        m_conflicts.append(entry);
    }

    // Reads the server copy so the conflict carries what the user compares.
    void conflict(const LocalItem &item, const QString &reason)
    {
        QMap<QString, RemoteEvent> current;
        if (!m_backend.fetch({item.remoteId}, &current)) return;
        const bool gone = !current.contains(item.remoteId);
        conflictFrom(item, gone ? nullptr : &current[item.remoteId], gone, reason);
    }

    void push(const LocalItem &item, const QString &version, bool recreate = false)
    {
        if (item.readOnly && !item.remoteId.isEmpty() && !recreate) {
            conflict(item, QStringLiteral("The server marks this event read-only"));
            return;
        }
        const bool create = item.remoteId.isEmpty() || recreate;
        const WriteOutcome outcome = create ? m_backend.create(item) : m_backend.update(item, version);
        switch (outcome.kind) {
        case WriteOutcome::Done: m_pending.append({&item, outcome}); break;
        case WriteOutcome::Conflict: conflict(item, outcome.error); break;
        case WriteOutcome::Gone: conflictFrom(item, nullptr, true, outcome.error); break;
        case WriteOutcome::Failed:
            if (!failed()) m_errors.append(QStringLiteral("%1: %2").arg(item.id, outcome.error));
            break;
        }
    }

    Backend &m_backend;
    const Job &m_job;
    QVariantList m_fetched, m_removed, m_pushed, m_conflicts, m_calendars;
    QList<PendingPush> m_pending;
    QStringList m_errors;
    int m_created = 0, m_updated = 0;
};
} // namespace

QVariantMap run(Backend &backend, const Job &job)
{
    Engine engine(backend, job);
    switch (job.mode) {
    case Mode::Sync: engine.sync(); break;
    case Mode::Put: engine.put(); break;
    case Mode::Delete: engine.remove(); break;
    case Mode::Calendars: engine.calendars(); break;
    }
    if (job.mode == Mode::Sync || job.mode == Mode::Put) engine.finishPushes();
    return engine.result();
}

} // namespace QindaOffice::Mail::CalendarSync
