// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Common QObject surface of the Exchange Web Services and Microsoft Graph
// calendar adapters: a host adapter in the sense of QindaTK D-189 item 3,
// with the invokables, signals and record fields of QindaTK::CalDavProvider
// so the calendar views, the store and the host treat every calendar alike.
//
// AGENT-CONTRACT (QindaTK docs/productivity-providers.md, docs/mail.md):
// - One request in, one signal out: sync -> syncFinished; removeRemote and
//   resolveConflict -> changeFinished; listCalendars -> calendarsFinished.
//   Validation failures arrive through the same signal on the next event-loop
//   turn. result: requestId, ok, state (accepted|offline|rejected|conflict),
//   error, retryable, transcript, fetched, created, updated, deleted,
//   conflicts (counts), conflictCount, [retryAfterSeconds], [authRejected].
// - A record belongs to this adapter when remoteAccount == accountId and its
//   calendarId is empty or equals calendarId (several calendars of one
//   account need distinct calendarIds). A record with no remoteId is created
//   on the server; a synced one carries remoteId, remoteChangeKey (EWS) or
//   remoteEtag (Graph), remoteBaseline, syncDigest and syncState.
// AGENT-GUARD: the secret lives only in m_secret: no property returns it, and
// results, transcripts, errors and store records are redacted (tests assert
// it against results, captured log output and the saved store file).

#include "calendar_sync.h"

#include <qindatk/local_productivity_store.h>

#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
#include <functional>
#include <memory>

namespace QindaOffice::Mail {

class HostedCalendarProvider : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QindaTK::LocalProductivityStore *store READ store WRITE setStore NOTIFY configChanged)
    Q_PROPERTY(QString accountId MEMBER m_accountId NOTIFY configChanged)
    Q_PROPERTY(QString calendarId MEMBER m_calendarId NOTIFY configChanged)
    Q_PROPERTY(QString defaultZone MEMBER m_defaultZone NOTIFY configChanged)
    Q_PROPERTY(int timeoutMs MEMBER m_timeoutMs NOTIFY configChanged)
    Q_PROPERTY(int syncPastDays MEMBER m_syncPastDays NOTIFY configChanged)
    Q_PROPERTY(int syncFutureDays MEMBER m_syncFutureDays NOTIFY configChanged)
    Q_PROPERTY(bool autoSave MEMBER m_autoSave NOTIFY configChanged)
    Q_PROPERTY(bool allowInsecureAuth MEMBER m_allowInsecureAuth NOTIFY configChanged)
    Q_PROPERTY(bool hasSecret READ hasSecret NOTIFY secretChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QVariantList conflicts READ conflicts NOTIFY conflictsChanged)
public:
    explicit HostedCalendarProvider(QObject *parent = nullptr, const QString &accountId = QStringLiteral("exchange"));
    ~HostedCalendarProvider() override;

    QindaTK::LocalProductivityStore *store() const { return m_store; }
    void setStore(QindaTK::LocalProductivityStore *store);
    bool hasSecret() const { return !m_secret.isEmpty(); }
    bool busy() const { return m_pending > 0; }
    QString state() const { return m_state; }
    QString lastError() const { return m_lastError; }
    QVariantList conflicts() const;

    // Keeps the password or token in memory only. EWS: "basic" (password,
    // with userName) or "bearer"; Graph: "bearer".
    Q_INVOKABLE void setSecret(const QString &secret, const QString &mechanism = QStringLiteral("bearer"));
    Q_INVOKABLE void clearSecret();
    // Pulls the sync window, then pushes local edits and new records.
    Q_INVOKABLE bool sync(const QString &requestId);
    // Deletes a synced event on the server (conditionally) and then locally.
    Q_INVOKABLE bool removeRemote(const QString &requestId, const QString &collection, const QString &recordId);
    // "remote" adopts the server version; "local" writes the local one over
    // the version the conflict recorded (a newer server edit is a new conflict).
    Q_INVOKABLE bool resolveConflict(const QString &requestId, const QString &collection,
                                     const QString &recordId, const QString &choice);
    // The account's calendars: result.calendars [{id, name, color, readOnly, isDefault}].
    Q_INVOKABLE bool listCalendars(const QString &requestId);

signals:
    void configChanged();
    void secretChanged();
    void stateChanged();
    void conflictsChanged();
    void syncFinished(const QString &requestId, const QVariantMap &result);
    void changeFinished(const QString &requestId, const QVariantMap &result);
    void calendarsFinished(const QString &requestId, const QVariantMap &result);

protected:
    using BackendFactory = std::function<std::unique_ptr<CalendarSync::Backend>()>;
    struct Credentials {
        QString secret, mechanism;
    };

    // Local id prefix ("ews" / "graph") and the record field holding the
    // server version ("remoteChangeKey" / "remoteEtag").
    virtual QString idPrefix() const = 0;
    virtual QString versionField() const = 0;
    // Checks the service configuration and the credential mechanism;
    // returns a user-facing error or an empty string.
    virtual QString validate() const = 0;
    // A self-contained factory (a copy of the configuration and credentials)
    // that the worker thread calls; it must not capture `this`.
    virtual BackendFactory backendFactory(const SyncWindow &window) const = 0;

    Credentials credentials() const { return {m_secret, m_mechanism}; }
    QString defaultZoneId() const { return m_defaultZone; }
    int timeout() const { return m_timeoutMs; }
    bool insecureAllowed() const { return m_allowInsecureAuth; }

private:
    enum class Signal { Sync, Change, Calendars };
    bool owns(const QVariantMap &row) const;
    QVariantMap findRecord(const QString &id) const;
    CalendarSync::LocalItem localItem(const QVariantMap &row) const;
    QVariantMap merged(QVariantMap row, const QString &id, const QString &remoteId, const QString &version,
                       const QVariantMap &fields) const;
    bool start(Signal signal, const QString &requestId, CalendarSync::Job job);
    void apply(const QVariantMap &result);
    QVariantMap persist(QVariantMap result);
    void deliver(Signal signal, const QString &requestId, QVariantMap result);

    QPointer<QindaTK::LocalProductivityStore> m_store;
    QString m_accountId, m_calendarId;                  // set by the constructor
    QString m_defaultZone = QStringLiteral("UTC");
    int m_timeoutMs = 15000, m_syncPastDays = 30, m_syncFutureDays = 365;
    bool m_autoSave = true, m_allowInsecureAuth = false;
    QString m_secret, m_mechanism = QStringLiteral("bearer");
    QMap<QString, QVariantMap> m_conflicts;     // record id -> conflict entry
    int m_pending = 0;
    QString m_state = QStringLiteral("idle"), m_lastError;
};

} // namespace QindaOffice::Mail
