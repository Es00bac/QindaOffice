// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// An in-process Microsoft Graph v1.0 calendar for the Graph adapter tests,
// shaped after the public Graph reference (event, calendarView, instances,
// calendar resources; @odata.etag, @odata.nextLink, If-Match -> 412,
// Retry-After on 429). It answers in UTC, as `Prefer: outlook.timezone="UTC"`
// asks, and pages every collection `pageSize` rows at a time through
// @odata.nextLink ($skip), so paging is always exercised.
// A series master lists its instances explicitly (occurrences and
// exceptions); a deleted occurrence is simply absent, as in Graph.

#include "fake_http_server.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>

class GraphFakeServer {
public:
    struct Event {
        QJsonObject json;                   // without id / @odata.etag
        int etag = 1;
        QList<QJsonObject> instances;       // series masters only
    };

    GraphFakeServer();
    FakeHttpServer http;
    QMap<QString, Event> events;
    bool canEdit = true;
    bool throttle = false;                  // 429 + Retry-After: 7
    int pageSize = 2;
    QString foreignNextLink;                // when set, the first page links there
    QList<QJsonObject> posted;              // POST bodies as received

    QString base() const { return http.origin() + QStringLiteral("/v1.0"); }
    QString etag(const QString &id) const { return QStringLiteral("W/\"e%1\"").arg(events.value(id).etag); }
    void bump(const QString &id) { ++events[id].etag; }
    int count(const QByteArray &method, const QString &path = {}) const
    {
        return http.count(method, path.isEmpty() ? QString() : QStringLiteral("/v1.0") + path);
    }

    // {dateTime, timeZone} in UTC from an ISO instant, as Graph answers with Prefer UTC.
    static QJsonObject utcTime(const QDateTime &instant);

private:
    FakeHttpServer::Reply handle(const FakeHttpServer::Request &request);
    FakeHttpServer::Reply page(const FakeHttpServer::Request &request, const QList<QJsonObject> &rows);
    FakeHttpServer::Reply patch(const QString &id, const FakeHttpServer::Request &request);
    FakeHttpServer::Reply remove(const QString &id, const FakeHttpServer::Request &request);
    QJsonObject full(const QString &id) const;
    // Master id and index of instance `id`; empty when not an instance.
    QString masterOf(const QString &id, int *index) const;
    int m_nextId = 1;
};
