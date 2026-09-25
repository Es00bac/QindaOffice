// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Worker-thread two-way calendar sync shared by the EWS and Graph adapters:
// the same decisions QindaTK's CalDavJobs makes (remote change by version,
// local edit by digest, conditional writes, conflicts instead of
// overwrites), over a Backend that speaks one service's API.
//
// AGENT-CONTRACT (hosted_calendar_provider.cpp applies the result map):
//   fetched   [{id, remoteId, version, fields}]      server -> store
//   removed   [{id}]                                 deleted on the server (or by removeRemote)
//   pushed    [{id, remoteId, version, fields?, digest, created}]  store -> server
//   conflicts [{collection, id, remoteId, remoteDeleted, remoteVersion?, remoteFields?, reason?}]
// Every entry is a confirmed server fact, so the provider applies them even
// when a later request of the same job failed (a created event must learn
// its remote id, or the next sync would create it twice).

#include "exchange_common.h"
#include "http_transport.h"

#include <QHash>
#include <QList>
#include <QMap>
#include <QVariantMap>

namespace QindaOffice::Mail::CalendarSync {

struct RemoteEvent {
    QString remoteId, version;      // EWS ItemId/ChangeKey, Graph id/@odata.etag
    bool hasFields = false;         // false: listed by id and version only
    QVariantMap fields;             // digestFields() + remoteExtraFields()
};

struct LocalItem {
    QString id, remoteId, version, syncDigest;
    bool dirty = false, readOnly = false;
    QVariantMap record;
    QVariantMap baseline() const { return record.value(QStringLiteral("remoteBaseline")).toMap(); }
};

struct WriteOutcome {
    enum Kind { Done, Conflict, Gone, Failed };
    Kind kind = Failed;
    QString remoteId, version;      // Done: the item's (new) identity
    QString error;                  // Failed: the problem; Conflict: why (optional)
};

// One service's API. Implementations report whole-exchange failures through
// transport() and per-item problems through errors(); they never touch
// QObjects outside the job thread.
class Backend {
public:
    Backend(int timeoutMs, bool allowInsecureAuth) : m_transport(timeoutMs, allowInsecureAuth) {}
    virtual ~Backend() = default;
    Backend(const Backend &) = delete;
    Backend &operator=(const Backend &) = delete;

    // Every single event and series master the sync window reaches.
    virtual bool list(QMap<QString, RemoteEvent> *out) = 0;
    // Full mappings; an id missing from `out` no longer exists.
    virtual bool fetch(const QStringList &ids, QMap<QString, RemoteEvent> *out) = 0;
    virtual WriteOutcome create(const LocalItem &item) = 0;
    // Conditional on `version`: a newer server version is a Conflict.
    virtual WriteOutcome update(const LocalItem &item, const QString &version) = 0;
    virtual WriteOutcome remove(const LocalItem &item, const QString &version) = 0;
    // [{id, name, color, readOnly, isDefault}]
    virtual bool listCalendars(QVariantList *out) = 0;

    HttpTransport &transport() { return m_transport; }
    QStringList &errors() { return m_errors; }
    // Remote id -> the zone its record already has (Zones::toIana preference).
    void setPreferredZones(const QHash<QString, QString> &zones) { m_zones = zones; }
    // A created item re-read right after the write keeps its record's zone.
    void preferZone(const QString &remoteId, const QString &zone) { m_zones.insert(remoteId, zone); }
    QStringList preferredZones(const QString &remoteId, const QString &defaultZone) const
    {
        return {m_zones.value(remoteId), defaultZone};
    }

private:
    HttpTransport m_transport;
    QStringList m_errors;
    QHash<QString, QString> m_zones;
};

enum class Mode { Sync, Put, Delete, Calendars };

struct Job {
    Mode mode = Mode::Sync;
    QString prefix, accountId;      // local ids of new remote events: localIdFor(prefix, accountId, remoteId)
    SyncWindow window;
    QList<LocalItem> items;         // Sync: this adapter's records
    LocalItem target;               // Put/Delete
    QString ifMatch;                // Put/Delete: the server version the user saw
    bool create = false;            // Put: the server copy was deleted; create it again
};

QVariantMap run(Backend &backend, const Job &job);

} // namespace QindaOffice::Mail::CalendarSync
