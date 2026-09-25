// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Exchange Web Services operations behind EwsCalendarProvider, run on the
// job's worker thread: FindItem (CalendarView) to discover the window,
// GetItem for masters and exception items, CreateItem, UpdateItem with
// ConflictResolution="NeverOverwrite" on the stored ChangeKey, DeleteItem.
// No invitations or cancellations are ever sent (SendToNone): the adapter
// edits the user's calendar, it does not run meetings.
//
// AGENT-NOTE (occurrence addressing): an existing exception is addressed by
// its own ItemId (from the master's ModifiedOccurrences); a plain occurrence
// by OccurrenceItemId RecurringMasterId + InstanceIndex, the 1-based position
// of its original start in the pattern (deleted occurrences still count),
// computed with RecurrenceService from the mapped rule. A raw (unmapped)
// pattern therefore allows occurrence edits only on existing exceptions.

#include "calendar_sync.h"
#include "ews_mapping.h"
#include "ews_soap.h"

#include <QUrl>
#include <list>

namespace QindaOffice::Mail {

struct EwsConfig {
    QUrl url;
    QString userName, serverVersion, impersonate, folderId, defaultZone;
    QString secret, mechanism;      // "basic" | "bearer"
    int timeoutMs = 15000;
    bool allowInsecureAuth = false;
    SyncWindow window;
    int maxViewEntries = 1000;      // FindItem CalendarView bound
};

class EwsBackend : public CalendarSync::Backend {
public:
    explicit EwsBackend(const EwsConfig &config);

    bool list(QMap<QString, CalendarSync::RemoteEvent> *out) override;
    bool fetch(const QStringList &ids, QMap<QString, CalendarSync::RemoteEvent> *out) override;
    CalendarSync::WriteOutcome create(const CalendarSync::LocalItem &item) override;
    CalendarSync::WriteOutcome update(const CalendarSync::LocalItem &item, const QString &version) override;
    CalendarSync::WriteOutcome remove(const CalendarSync::LocalItem &item, const QString &version) override;
    bool listCalendars(QVariantList *out) override;

private:
    // One SOAP call. False (with the transport failed) on HTTP, SOAP-fault or
    // throttling problems; per-item ResponseMessages are the caller's.
    bool call(const QString &operation, const Ews::BodyWriter &body, Ews::Response *response);
    void writeFolder(QXmlStreamWriter &w) const;
    // GetItem by ItemId in batches; successful CalendarItem nodes by id.
    bool getItems(const QStringList &ids, const QStringList &fieldUris, QHash<QString, const XmlNode *> *items,
                  bool strict);
    CalendarSync::WriteOutcome applyOccurrences(const CalendarSync::LocalItem &item, const QString &remoteId,
                                                const QString &version, bool conditional);
    Ews::WriteOptions writeOptions() const;
    CalendarSync::WriteOutcome outcomeOf(const Ews::ResponseMessage &message, const QString &remoteId);

    EwsConfig m_config;
    Ews::Header m_header;
    std::list<Ews::Response> m_responses;   // keeps nodes alive for mapping (std::list: stable addresses)
};

} // namespace QindaOffice::Mail
