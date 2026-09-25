// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Microsoft Graph v1.0 calendar operations behind GraphCalendarProvider, run
// on the job's worker thread.
//   list:   GET {calendar}/events ($select, $filter: series masters and the
//           singles in the window) + GET {calendar}/calendarView (the window's
//           occurrences and exceptions), both paged through @odata.nextLink
//   fetch:  GET /me/events/{id} (+ /instances for a series master)
//   write:  POST {calendar}/events, PATCH /me/events/{id} with If-Match
//           (412 -> conflict), DELETE with If-Match; occurrences by their
//           instance id (from the window listing, else /instances around the
//           original start)
// AGENT-GUARD: @odata.nextLink is followed only to the base URL's own
// scheme, host and port; a link elsewhere fails the request instead of
// sending the bearer token to another origin. Paging is bounded (maxPages);
// a window with more events fails rather than syncing half of it, because a
// missing event would read as a deleted one.
// AGENT-NOTE: requests send Prefer IdType="ImmutableId" (ids survive folder
// moves) and POST carries transactionId (Graph drops a retried duplicate).
// AGENT-NOTE: unlike EWS (SendToNone) Graph has no "do not notify" switch:
// PATCH/DELETE of a meeting the user organizes sends updates/cancellations
// to its attendees. The host warns before such writes (docs/mail.md).

#include "calendar_sync.h"

#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>

namespace QindaOffice::Mail {

struct GraphConfig {
    QUrl baseUrl;                   // https://graph.microsoft.com/v1.0
    QString calendarId;             // empty = the default calendar
    QString defaultZone, secret;
    int timeoutMs = 15000;
    bool allowInsecureAuth = false;
    SyncWindow window;
    int pageSize = 100, maxPages = 50;
};

class GraphBackend : public CalendarSync::Backend {
public:
    explicit GraphBackend(const GraphConfig &config);

    bool list(QMap<QString, CalendarSync::RemoteEvent> *out) override;
    bool fetch(const QStringList &ids, QMap<QString, CalendarSync::RemoteEvent> *out) override;
    CalendarSync::WriteOutcome create(const CalendarSync::LocalItem &item) override;
    CalendarSync::WriteOutcome update(const CalendarSync::LocalItem &item, const QString &version) override;
    CalendarSync::WriteOutcome remove(const CalendarSync::LocalItem &item, const QString &version) override;
    bool listCalendars(QVariantList *out) override;

private:
    QUrl url(const QString &path, const QUrlQuery &query = {}) const;
    QString calendarPath() const;
    QString eventPath(const QString &id) const;
    // One request with the standard headers; JSON body when `body` is set.
    bool request(const QByteArray &method, const QUrl &target, const QJsonObject *body, const QByteArray &ifMatch,
                 HttpTransport::Response *response);
    // GET a collection and every @odata.nextLink page (bounded, same origin).
    bool getPaged(const QUrl &first, QList<QJsonObject> *out);
    bool calendarReadOnly();
    // Instances of `masterId` between two instants.
    bool instances(const QString &masterId, const QDateTime &from, const QDateTime &to, QList<QJsonObject> *out);
    // Maps a master, first widening its instances when window occurrences are
    // missing (an exception moved out of the window is not a deletion).
    bool mapMaster(const QJsonObject &master, QList<QJsonObject> instances, CalendarSync::RemoteEvent *out);
    CalendarSync::WriteOutcome applyOccurrences(const CalendarSync::LocalItem &item, const QString &remoteId,
                                                const QString &version, bool conditional);
    CalendarSync::WriteOutcome failure(const HttpTransport::Response &response, const QString &what);

    GraphConfig m_config;
    int m_readOnly = -1;            // -1 unknown, 0 editable, 1 read-only (the calendar's canEdit)
};

} // namespace QindaOffice::Mail
