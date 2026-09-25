// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The seeded Microsoft 365 calendar shared by the two Graph test
// executables. It mirrors the EWS fixture: a single meeting, an all-day
// event, a weekly Denver series across the 8 March 2026 DST change (21 March
// deleted - absent from the instances, 14 March moved within the window, 28
// March moved to January 2030, outside the sync window) and a second-Tuesday
// series whose zone is given as an IANA id.

#include "exchange_test_support.h"
#include "graph_calendar_provider.h"
#include "graph_fake_server.h"

#include <qindatk/local_productivity_store.h>

#include <QTemporaryDir>

namespace GraphTest {
using namespace QindaOffice::Mail;
using namespace ExchangeTest;

inline const QString kToken = QStringLiteral("EwBwA8l6BAAU.graph-Access_Token-77");

inline QDateTime utc(const char *text) { return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate); }

inline QJsonObject instance(const QString &id, const char *type, const char *original, const char *start, int minutes,
                            const QString &subject = QStringLiteral("Standup"))
{
    return {{QStringLiteral("id"), id}, {QStringLiteral("type"), QString::fromLatin1(type)},
            {QStringLiteral("seriesMasterId"), QStringLiteral("ev-standup")},
            {QStringLiteral("originalStart"), QString::fromLatin1(original)},
            {QStringLiteral("start"), GraphFakeServer::utcTime(utc(start))},
            {QStringLiteral("end"), GraphFakeServer::utcTime(utc(start).addSecs(60 * minutes))},
            {QStringLiteral("subject"), subject}, {QStringLiteral("isCancelled"), false},
            {QStringLiteral("location"), QJsonObject{{QStringLiteral("displayName"), QString()}}}};
}

inline void seed(GraphFakeServer &server)
{
    const QString denver = QStringLiteral("Mountain Standard Time");
    GraphFakeServer::Event review;
    review.json = {{QStringLiteral("type"), QStringLiteral("singleInstance")}, {QStringLiteral("subject"), QStringLiteral("Design review")},
        {QStringLiteral("body"), QJsonObject{{QStringLiteral("contentType"), QStringLiteral("text")},
                                             {QStringLiteral("content"), QStringLiteral("Bring the Q1 numbers & the <draft>\r\n")}}},
        {QStringLiteral("location"), QJsonObject{{QStringLiteral("displayName"), QStringLiteral("Room 4")}}},
        {QStringLiteral("start"), GraphFakeServer::utcTime(utc("2026-03-10T15:00:00Z"))},
        {QStringLiteral("end"), GraphFakeServer::utcTime(utc("2026-03-10T16:00:00Z"))},
        {QStringLiteral("isAllDay"), false}, {QStringLiteral("originalStartTimeZone"), denver},
        {QStringLiteral("iCalUId"), QStringLiteral("review-uid")}, {QStringLiteral("isOnlineMeeting"), true},
        {QStringLiteral("onlineMeeting"), QJsonObject{{QStringLiteral("joinUrl"), QStringLiteral("https://teams.example.com/l/meetup/1")}}},
        {QStringLiteral("organizer"), QJsonObject{{QStringLiteral("emailAddress"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Grace Hopper")},
                                                                                         {QStringLiteral("address"), QStringLiteral("grace@example.com")}}}}},
        {QStringLiteral("attendees"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("required")},
            {QStringLiteral("status"), QJsonObject{{QStringLiteral("response"), QStringLiteral("accepted")}}},
            {QStringLiteral("emailAddress"), QJsonObject{{QStringLiteral("name"), QStringLiteral("Ada Lovelace")},
                                                         {QStringLiteral("address"), QStringLiteral("ada@example.com")}}}}}}};
    server.events.insert(QStringLiteral("ev-review"), review);

    GraphFakeServer::Event offsite;
    offsite.json = {{QStringLiteral("type"), QStringLiteral("singleInstance")}, {QStringLiteral("subject"), QStringLiteral("Offsite")},
        {QStringLiteral("start"), GraphFakeServer::utcTime(utc("2026-03-20T06:00:00Z"))},
        {QStringLiteral("end"), GraphFakeServer::utcTime(utc("2026-03-22T06:00:00Z"))},
        {QStringLiteral("isAllDay"), true}, {QStringLiteral("originalStartTimeZone"), denver}};
    server.events.insert(QStringLiteral("ev-offsite"), offsite);

    GraphFakeServer::Event standup;
    standup.json = {{QStringLiteral("type"), QStringLiteral("seriesMaster")}, {QStringLiteral("subject"), QStringLiteral("Standup")},
        {QStringLiteral("start"), GraphFakeServer::utcTime(utc("2026-03-07T16:00:00Z"))},
        {QStringLiteral("end"), GraphFakeServer::utcTime(utc("2026-03-07T16:30:00Z"))},
        {QStringLiteral("isAllDay"), false}, {QStringLiteral("originalStartTimeZone"), denver},
        {QStringLiteral("recurrence"), QJsonObject{
            {QStringLiteral("pattern"), QJsonObject{{QStringLiteral("type"), QStringLiteral("weekly")}, {QStringLiteral("interval"), 1},
                {QStringLiteral("daysOfWeek"), QJsonArray{QStringLiteral("saturday")}}, {QStringLiteral("firstDayOfWeek"), QStringLiteral("sunday")}}},
            {QStringLiteral("range"), QJsonObject{{QStringLiteral("type"), QStringLiteral("numbered")}, {QStringLiteral("startDate"), QStringLiteral("2026-03-07")},
                {QStringLiteral("numberOfOccurrences"), 5}, {QStringLiteral("recurrenceTimeZone"), denver}}}}}};
    standup.instances = {instance(QStringLiteral("occ-07"), "occurrence", "2026-03-07T16:00:00Z", "2026-03-07T16:00:00Z", 30),
        instance(QStringLiteral("exc-14"), "exception", "2026-03-14T15:00:00.0000000Z", "2026-03-14T18:00:00Z", 30, QStringLiteral("Standup (moved)")),
        instance(QStringLiteral("exc-28"), "exception", "2026-03-28T15:00:00Z", "2030-01-05T15:00:00Z", 30),
        instance(QStringLiteral("occ-04"), "occurrence", "2026-04-04T15:00:00Z", "2026-04-04T15:00:00Z", 30)};
    server.events.insert(QStringLiteral("ev-standup"), standup);

    GraphFakeServer::Event board;
    board.json = {{QStringLiteral("type"), QStringLiteral("seriesMaster")}, {QStringLiteral("subject"), QStringLiteral("Board meeting")},
        {QStringLiteral("start"), GraphFakeServer::utcTime(utc("2026-03-10T18:00:00Z"))},
        {QStringLiteral("end"), GraphFakeServer::utcTime(utc("2026-03-10T19:00:00Z"))},
        {QStringLiteral("isAllDay"), false}, {QStringLiteral("originalStartTimeZone"), QStringLiteral("America/Denver")},
        {QStringLiteral("recurrence"), QJsonObject{
            {QStringLiteral("pattern"), QJsonObject{{QStringLiteral("type"), QStringLiteral("relativeMonthly")}, {QStringLiteral("interval"), 1},
                {QStringLiteral("daysOfWeek"), QJsonArray{QStringLiteral("tuesday")}}, {QStringLiteral("index"), QStringLiteral("second")}}},
            {QStringLiteral("range"), QJsonObject{{QStringLiteral("type"), QStringLiteral("noEnd")}, {QStringLiteral("startDate"), QStringLiteral("2026-03-10")}}}}}};
    server.events.insert(QStringLiteral("ev-board"), board);
}

struct Fixture {
    QTemporaryDir directory;
    GraphFakeServer server;
    QindaTK::LocalProductivityStore store;
    GraphCalendarProvider provider;

    Fixture()
    {
        seed(server);
        server.http.authorization = "Bearer " + kToken.toUtf8();
        store.setFilePath(directory.filePath(QStringLiteral("calendar.json")));
        provider.setProperty("baseUrl", server.base());
        provider.setProperty("accountId", QStringLiteral("m365"));
        provider.setProperty("calendarId", QStringLiteral("m365"));
        provider.setProperty("defaultZone", QStringLiteral("America/Denver"));
        provider.setProperty("syncPastDays", 1000);
        provider.setProperty("syncFutureDays", 1000);
        provider.setProperty("timeoutMs", 10000);
        provider.setStore(&store);
        provider.setSecret(kToken, QStringLiteral("bearer"));
    }
    QVariantMap event(const QString &title) const { return byTitle(store.events(), title); }
};

inline QList<QByteArray> secrets() { return {kToken.toUtf8(), kToken.toUtf8().toBase64()}; }

} // namespace GraphTest
