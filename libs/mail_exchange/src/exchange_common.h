// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared record, time and result helpers for the Exchange Web Services and
// Microsoft Graph calendar adapters (docs/mail.md, D-044).
//
// AGENT-CONTRACT (QindaTK ical_codec.cpp eventFields(), CalDavProvider,
// LocalCalendarProjectionModel): adapter records carry exactly the event
// fields the toolkit's CalDAV provider writes, in the same text forms:
//   startLocal/endLocal  "yyyy-MM-dd HH:mm[:ss]" wall time, "yyyy-MM-dd" when allDay
//                        (all-day end exclusive)
//   zone                 IANA id
//   exceptions           sorted "yyyy-MM-ddTHH:mm:ss" original starts ("yyyy-MM-dd" all-day)
//   overrides            sorted {recurrenceId, startLocal, endLocal[, title, description, location]}
//   recurrenceRule       RecurrenceService rule or null; recurrenceRaw an RRULE otherwise
// A local edit is a changed QindaTK::ICal::digest("VEVENT", record) against
// `syncDigest`, so store operations need no dirty flag (QindaTK D-188).

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QindaOffice::Mail {

// The digest fields (the toolkit's VEVENT field list).
const QStringList &digestFields();
// Adapter-owned fields outside the digest: remote identity and display-only data.
const QStringList &remoteExtraFields();

QString eventDigest(const QVariantMap &record);
QVariantMap digestSubset(const QVariantMap &record);

// Local wall text in the store's forms (see the contract above).
QString localText(const QDateTime &wall, bool allDay);
QString occurrenceKey(const QDateTime &wall, bool allDay);
// Stored local text as a zoned QDateTime (a fold resolves to the earlier
// offset, as RecurrenceService does); invalid for bad text or zone.
QDateTime wallTime(const QString &text, const QString &zone);
QDateTime toWall(const QDateTime &instant, const QString &zone);
// Canonical occurrence key of any accepted local text ("" when invalid).
QString canonicalKey(const QString &text);

struct SyncWindow {
    QDateTime startUtc, endUtc;
};
// True when a single event overlaps the window. Recurring records are
// handled by the caller (their series may reach any window).
bool overlapsWindow(const QVariantMap &record, const SyncWindow &window);
bool isRecurring(const QVariantMap &record);

// Stable local id "<prefix>:<accountId>:<32 hex of SHA-256(remoteId)>".
// AGENT-NOTE: remote ids are long opaque strings that may change when an
// item moves folders; hashing keeps store ids short, and a changed remote id
// becomes remove + add instead of two remote items sharing one local id.
QString localIdFor(const QString &prefix, const QString &accountId, const QString &remoteId);

// A record's instants. All-day events start and end at local midnight of
// their zone (end exclusive). False with a user-facing error when the text
// or zone is invalid.
struct EventTimes {
    QDateTime start, end;
    bool allDay = false;
    QString zone;
};
bool eventTimes(const QVariantMap &record, EventTimes *out, QString *error);
// Occurrence `key` (its original local start) of `master`, moved and
// resized by the override `entry` when it has startLocal/endLocal.
bool occurrenceTimes(const QVariantMap &master, const QString &key, const QVariantMap &entry,
                     EventTimes *out, QString *error);
// The override entry for occurrence `key` of the mapped `master` that runs
// start..end with these texts (null QString = not reported by the server);
// empty when it matches the series, so a restored occurrence reads as none.
QVariantMap overrideEntry(const QVariantMap &master, const QString &key, const QDateTime &start,
                          const QDateTime &end, const QString &title, const QString &location,
                          const QString &description);
// Server text bodies end in CRLF runs; the record keeps LF and no trailing blanks.
QString normalizedText(const QString &text);

// Result maps (QindaTK docs/productivity-providers.md "Common result contract").
QVariantMap rejection(const QString &error);
// Retry-After as delta-seconds or an HTTP date; -1 when absent or unreadable.
int parseRetryAfter(const QByteArray &value, const QDateTime &nowUtc);

// Series-level edits between the last synced mapping (`baseline`) and the
// record. AGENT-NOTE: writes send only what changed so a server-side HTML
// body, attendees or categories the adapters do not map are never rewritten.
struct SeriesChanges {
    bool title = false, description = false, location = false, times = false, recurrence = false;
    bool any() const { return title || description || location || times || recurrence; }
};
SeriesChanges seriesChanges(const QVariantMap &baseline, const QVariantMap &record);

// Occurrence-level differences between a server mapping and the record.
struct OccurrenceChanges {
    QStringList excluded;           // excluded locally, still an occurrence on the server
    QStringList reinstated;         // excluded on the server, not locally (servers cannot undo)
    QVariantList overrides;         // override entries new or changed locally
    QVariantList restored;          // server overrides the record no longer has: {recurrenceId}
    bool any() const { return !excluded.isEmpty() || !reinstated.isEmpty() || !overrides.isEmpty() || !restored.isEmpty(); }
};
OccurrenceChanges occurrenceChanges(const QVariantMap &server, const QVariantMap &record);
// True when a write of `record` must also touch occurrences: its exceptions
// or overrides differ from the baseline, or the series moved or changed its
// rule while it has some (servers drop exceptions when a series changes).
bool occurrencesNeedWrite(const QVariantMap &baseline, const QVariantMap &record, const SeriesChanges &changes);

} // namespace QindaOffice::Mail
