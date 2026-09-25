// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Microsoft Graph v1.0 event JSON <-> store event record (docs/mail.md has
// the field table).
//
// AGENT-NOTE (time zones): every request carries
// `Prefer: outlook.timezone="UTC"`, so start/end/originalStart arrive as UTC
// instants; the record's zone comes from originalStartTimeZone (a Windows
// name, an IANA id, or "tzone://Microsoft/Utc"), falling back to the
// series' recurrenceTimeZone and then the provider's defaultZone. Writes
// send the local wall time with the Windows zone name, which every Exchange
// Online mailbox accepts. An all-day event whose UTC form is not local
// midnight is read from its date (Graph keeps all-day events floating).
// AGENT-NOTE (occurrences): Graph has no DeletedOccurrences. Inside the
// window, an occurrence the rule produces but the instance listing lacks
// was deleted (-> exceptions); cancelled instances too. Exceptions
// (type "exception") become overrides. Outside the window nothing is known,
// which docs/mail.md states as a limit.

#include "calendar_sync.h"
#include "exchange_common.h"

#include <QJsonArray>
#include <QJsonObject>

namespace QindaOffice::Mail::Graph {

QString eventSelect();              // $select for series masters and single events
QString instanceSelect();           // $select for calendarView / instances rows

struct MapContext {
    QString defaultZone;
    QStringList preferredZones;
    bool readOnly = false;          // the calendar's canEdit is false
    SyncWindow window;
};

// The instant of a dateTimeTimeZone object ({dateTime, timeZone}).
QDateTime instantOf(const QJsonObject &dateTimeTimeZone);
// Original-start keys the rule produces inside the window that `instances`
// do not show (candidates for deleted occurrences).
QStringList missingOccurrences(const QJsonObject &master, const QList<QJsonObject> &instances,
                               const MapContext &context);
// Occurrence key (original local start in `zone`) of a calendarView/instances row.
QString originalKey(const QJsonObject &instance, const QString &zone, bool allDay);
CalendarSync::RemoteEvent mapEvent(const QJsonObject &event, const QList<QJsonObject> &instances,
                                   const MapContext &context);

// JSON for POST (all fields) or PATCH (`changes` only). False with a
// user-facing error when Graph cannot hold the record.
bool eventBody(const QVariantMap &record, const SeriesChanges &changes, bool create, QJsonObject *body, QString *error);
// PATCH body giving occurrence `key` the override's (or the series') values.
bool occurrenceBody(const QVariantMap &master, const QString &key, const QVariantMap &entry, QJsonObject *body,
                    QString *error);

} // namespace QindaOffice::Mail::Graph
