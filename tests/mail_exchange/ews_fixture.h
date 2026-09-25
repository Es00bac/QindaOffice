// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The seeded calendar and provider setup shared by the two EWS test
// executables: a single meeting, a read-only all-day event, a weekly series
// (Denver, across the 8 March 2026 DST change) with one deleted and one moved
// occurrence, and a second-Tuesday series (unmappable -> recurrenceRaw).

#include "ews_calendar_provider.h"
#include "ews_fake_server.h"
#include "exchange_test_support.h"

#include <qindatk/local_productivity_store.h>

#include <QTemporaryDir>

namespace EwsTest {
using namespace QindaOffice::Mail;
using namespace ExchangeTest;
inline const QString kUser = QStringLiteral("ada@example.com");
inline const QString kPassword = QStringLiteral("ews-Pass/word+42");
inline const QString kToken = QStringLiteral("eyJ0eXAi.ews-bearer-Token_9");

inline QDateTime utc(const char *text) { return QDateTime::fromString(QString::fromLatin1(text), Qt::ISODate); }

inline XmlNode recurrence(const QByteArray &inner)
{
    XmlNode node;
    QString error;
    parseXml("<t:Recurrence xmlns:t=\"http://schemas.microsoft.com/exchange/services/2006/types\">" + inner + "</t:Recurrence>",
             &node, &error);
    return node;
}

inline void seed(EwsFakeServer &server)
{
    EwsFakeServer::Item review;
    review.id = QStringLiteral("AAMkReview=");
    review.uid = QStringLiteral("review-uid");
    review.subject = QStringLiteral("Design review");
    review.body = QStringLiteral("Bring the Q1 numbers & the <draft>");
    review.location = QStringLiteral("Room 4");
    review.start = utc("2026-03-10T15:00:00Z");             // 09:00 MDT (DST began 8 March)
    review.end = utc("2026-03-10T16:00:00Z");
    review.organizer = QStringLiteral("Grace Hopper <grace@example.com>");
    review.attendees = {QStringLiteral("Ada Lovelace <ada@example.com>")};
    review.online = true;
    review.joinUrl = QStringLiteral("https://meet.example.com/j/123");
    server.items.insert(review.id, review);

    EwsFakeServer::Item offsite;
    offsite.id = QStringLiteral("AAMkOffsite=");
    offsite.uid = QStringLiteral("offsite-uid");
    offsite.subject = QStringLiteral("Offsite");
    offsite.allDay = true;
    offsite.canModify = false;
    offsite.start = utc("2026-03-20T06:00:00Z");            // local midnight, Denver
    offsite.end = utc("2026-03-22T06:00:00Z");
    server.items.insert(offsite.id, offsite);

    EwsFakeServer::Item standup;
    standup.id = QStringLiteral("AAMkStandup=");
    standup.uid = QStringLiteral("standup-uid");
    standup.subject = QStringLiteral("Standup");
    standup.type = QStringLiteral("RecurringMaster");
    standup.start = utc("2026-03-07T16:00:00Z");            // 09:00 MST
    standup.end = utc("2026-03-07T16:30:00Z");
    standup.recurrence = recurrence("<t:WeeklyRecurrence><t:Interval>1</t:Interval><t:DaysOfWeek>Saturday</t:DaysOfWeek>"
        "<t:FirstDayOfWeek>Sunday</t:FirstDayOfWeek></t:WeeklyRecurrence><t:NumberedRecurrence>"
        "<t:StartDate>2026-03-07-07:00</t:StartDate><t:NumberOfOccurrences>5</t:NumberOfOccurrences></t:NumberedRecurrence>");
    standup.occurrenceStarts = {utc("2026-03-07T16:00:00Z"), utc("2026-03-14T15:00:00Z"), utc("2026-03-21T15:00:00Z"),
                                utc("2026-03-28T15:00:00Z"), utc("2026-04-04T15:00:00Z")};
    standup.deleted = {utc("2026-03-21T15:00:00Z")};
    standup.modified = {{QStringLiteral("exc-standup-14"), utc("2026-03-14T15:00:00Z"), utc("2026-03-14T18:00:00Z"),
                         utc("2026-03-14T18:30:00Z"), QStringLiteral("Standup (moved)"), QString(), QString(), false}};
    server.items.insert(standup.id, standup);

    EwsFakeServer::Item board;
    board.id = QStringLiteral("AAMkBoard=");
    board.uid = QStringLiteral("board-uid");
    board.subject = QStringLiteral("Board meeting");
    board.type = QStringLiteral("RecurringMaster");
    board.legacyZone = true;
    board.start = utc("2026-03-10T18:00:00Z");
    board.end = utc("2026-03-10T19:00:00Z");
    board.recurrence = recurrence("<t:RelativeMonthlyRecurrence><t:Interval>1</t:Interval><t:DaysOfWeek>Tuesday</t:DaysOfWeek>"
        "<t:DayOfWeekIndex>Second</t:DayOfWeekIndex></t:RelativeMonthlyRecurrence>"
        "<t:NoEndRecurrence><t:StartDate>2026-03-10</t:StartDate></t:NoEndRecurrence>");
    server.items.insert(board.id, board);
}

struct Fixture {
    QTemporaryDir directory;
    EwsFakeServer server;
    QindaTK::LocalProductivityStore store;
    EwsCalendarProvider provider;

    Fixture()
    {
        seed(server);
        server.http.authorization = "Basic " + QByteArray((kUser + QLatin1Char(':') + kPassword).toUtf8()).toBase64();
        store.setFilePath(directory.filePath(QStringLiteral("calendar.json")));
        provider.setProperty("url", server.url());
        provider.setProperty("userName", kUser);
        provider.setProperty("accountId", QStringLiteral("work"));
        provider.setProperty("calendarId", QStringLiteral("work"));
        provider.setProperty("defaultZone", QStringLiteral("America/Denver"));
        provider.setProperty("syncPastDays", 1000);
        provider.setProperty("syncFutureDays", 1000);
        provider.setProperty("timeoutMs", 10000);
        provider.setStore(&store);
        provider.setSecret(kPassword, QStringLiteral("basic"));
    }
    QVariantMap event(const QString &title) const { return byTitle(store.events(), title); }
};

inline QList<QByteArray> secrets()
{
    return {kPassword.toUtf8(), QByteArray((kUser + QLatin1Char(':') + kPassword).toUtf8()).toBase64(),
            kPassword.toUtf8().toBase64(), kToken.toUtf8()};
}

} // namespace EwsTest
