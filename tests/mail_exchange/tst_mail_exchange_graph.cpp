// SPDX-License-Identifier: GPL-3.0-or-later
// GraphCalendarProvider against an in-process Microsoft Graph calendar on
// 127.0.0.1 (graph_fake_server.h): mapping of singles, all-day events, a
// weekly series with a deleted occurrence and exceptions (one moved outside
// the sync window) and an unmappable relative-monthly pattern; paging
// through @odata.nextLink; idempotent sync; If-Match writes, conflicts, a
// 412 during resolution, and both resolutions.
#include "graph_fixture.h"

#include <qindatk/local_calendar_projection_model.h>
#include <qindatk/recurrence_service.h>

using namespace GraphTest;

class GraphProviderTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { LogCapture::install(); }

    void initialSyncPagesAndMapsEveryShape()
    {
        Fixture f;
        QVariantMap result = syncNow(f.provider, QStringLiteral("sync-1"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("fetched")).toInt(), 4);
        QCOMPARE(f.store.events().size(), 4);

        // Every collection came in pages of two through @odata.nextLink.
        int skipped = 0;
        for (const FakeHttpServer::Request &request : f.server.http.requests) {
            skipped += request.query.hasQueryItem(QStringLiteral("$skip"));
            QVERIFY(request.headers.value("prefer").contains("outlook.timezone=\"UTC\""));
            QVERIFY(request.headers.value("prefer").contains("IdType=\"ImmutableId\""));
        }
        QVERIFY(skipped >= 3);
        const FakeHttpServer::Request *listing = nullptr;
        for (const FakeHttpServer::Request &request : f.server.http.requests)
            if (request.path.endsWith(QLatin1String("/me/calendar/events"))) { listing = &request; break; }
        QVERIFY(listing);
        QVERIFY(listing->query.queryItemValue(QStringLiteral("$filter"), QUrl::FullyDecoded).startsWith(QStringLiteral("type eq 'seriesMaster' or (start/dateTime lt '")));
        QVERIFY(listing->query.queryItemValue(QStringLiteral("$select")).contains(QStringLiteral("originalStartTimeZone")));

        const QVariantMap review = f.event(QStringLiteral("Design review"));
        QVERIFY(review.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("graph:m365:")));
        QCOMPARE(review.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-10 09:00"));
        QCOMPARE(review.value(QStringLiteral("endLocal")).toString(), QStringLiteral("2026-03-10 10:00"));
        QCOMPARE(review.value(QStringLiteral("zone")).toString(), QStringLiteral("America/Denver"));
        QCOMPARE(review.value(QStringLiteral("description")).toString(), QStringLiteral("Bring the Q1 numbers & the <draft>"));
        QCOMPARE(review.value(QStringLiteral("remoteId")).toString(), QStringLiteral("ev-review"));
        QCOMPARE(review.value(QStringLiteral("remoteEtag")).toString(), QStringLiteral("W/\"e1\""));
        QCOMPARE(review.value(QStringLiteral("remoteUid")).toString(), QStringLiteral("review-uid"));
        QCOMPARE(review.value(QStringLiteral("organizer")).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Grace Hopper"));
        QCOMPARE(review.value(QStringLiteral("attendees")).toList().first().toMap().value(QStringLiteral("response")).toString(), QStringLiteral("accepted"));
        QCOMPARE(review.value(QStringLiteral("onlineMeetingUrl")).toString(), QStringLiteral("https://teams.example.com/l/meetup/1"));

        const QVariantMap offsite = f.event(QStringLiteral("Offsite"));
        QVERIFY(offsite.value(QStringLiteral("allDay")).toBool());
        QCOMPARE(offsite.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-20"));
        QCOMPARE(offsite.value(QStringLiteral("endLocal")).toString(), QStringLiteral("2026-03-22"));

        const QVariantMap standup = f.event(QStringLiteral("Standup"));
        QCOMPARE(standup.value(QStringLiteral("recurrenceRule")).toMap().value(QStringLiteral("weekdays")).toList(), QVariantList{6});
        QCOMPARE(standup.value(QStringLiteral("recurrenceRule")).toMap().value(QStringLiteral("count")).toInt(), 5);
        // 21 March has no instance: deleted. 28 March has one, far outside the window.
        QCOMPARE(standup.value(QStringLiteral("exceptions")).toList(), QVariantList{QStringLiteral("2026-03-21T09:00:00")});
        const QVariantList overrides = standup.value(QStringLiteral("overrides")).toList();
        QCOMPARE(overrides.size(), 2);
        QCOMPARE(overrides.at(0).toMap().value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-14 12:00"));
        QCOMPARE(overrides.at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Standup (moved)"));
        QCOMPARE(overrides.at(1).toMap().value(QStringLiteral("recurrenceId")).toString(), QStringLiteral("2026-03-28T09:00:00"));
        QCOMPARE(overrides.at(1).toMap().value(QStringLiteral("startLocal")).toString(), QStringLiteral("2030-01-05 08:00"));   // MST
        QVERIFY(!overrides.at(1).toMap().contains(QStringLiteral("title")));
        QCOMPARE(standup.value(QStringLiteral("remoteOccurrences")).toMap().value(QStringLiteral("2026-04-04T09:00:00")).toString(),
                 QStringLiteral("occ-04"));

        const QVariantMap board = f.event(QStringLiteral("Board meeting"));
        QVERIFY(board.value(QStringLiteral("recurrenceRule")).isNull());
        QCOMPARE(board.value(QStringLiteral("recurrenceRaw")).toString(), QStringLiteral("FREQ=MONTHLY;BYDAY=TU;BYSETPOS=2"));
        QCOMPARE(board.value(QStringLiteral("zone")).toString(), QStringLiteral("America/Denver"));
        QCOMPARE(board.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-10 12:00"));

        QindaTK::RecurrenceService recurrence;
        QindaTK::LocalCalendarProjectionModel model;
        model.setStore(&f.store);
        model.setRecurrenceService(&recurrence);
        QVERIFY2(model.project(QStringLiteral("2026-03-01T00:00:00Z"), QStringLiteral("2026-04-15T00:00:00Z")), qPrintable(model.error()));
        QStringList starts;
        for (const QVariant &row : model.occurrences())
            if (row.toMap().value(QStringLiteral("eventId")).toString() == standup.value(QStringLiteral("id")).toString())
                starts.append(row.toMap().value(QStringLiteral("startUtc")).toString());
        QCOMPARE(starts, QStringList({QStringLiteral("2026-03-07T16:00:00.000Z"), QStringLiteral("2026-03-14T18:00:00.000Z"),
                                      QStringLiteral("2026-04-04T15:00:00.000Z")}));

        result = syncNow(f.provider, QStringLiteral("sync-2"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("fetched")).toInt(), 0);
        QCOMPARE(f.server.count("PATCH") + f.server.count("POST") + f.server.count("DELETE"), 0);
        QVERIFY(transcriptOf(result).contains(QStringLiteral("[credentials redacted]")));

        QindaTK::LocalProductivityStore reloaded;                 // a restart
        QVERIFY(reloaded.loadFrom(f.store.filePath()));
        GraphCalendarProvider again;
        for (const char *name : {"baseUrl", "accountId", "calendarId", "defaultZone", "syncPastDays", "syncFutureDays"})
            again.setProperty(name, f.provider.property(name));
        again.setStore(&reloaded);
        again.setSecret(kToken, QStringLiteral("bearer"));
        result = syncNow(again, QStringLiteral("sync-3"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("fetched")).toInt() + result.value(QStringLiteral("updated")).toInt(), 0);
        QCOMPARE(f.server.count("PATCH") + f.server.count("POST") + f.server.count("DELETE"), 0);
    }

    void ifMatchWritesConflictsAndResolution()
    {
        Fixture f;
        QVERIFY(syncNow(f.provider, QStringLiteral("s1")).value(QStringLiteral("ok")).toBool());
        QVariantMap review = f.event(QStringLiteral("Design review"));
        const QString id = review.value(QStringLiteral("id")).toString();
        review.insert(QStringLiteral("title"), QStringLiteral("Design review (v2)"));
        f.store.upsert(QStringLiteral("events"), review);
        QVariantMap result = syncNow(f.provider, QStringLiteral("s2"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("updated")).toInt(), 1);
        const FakeHttpServer::Request *patch = nullptr;
        for (const FakeHttpServer::Request &request : f.server.http.requests)
            if (request.method == "PATCH") patch = &request;
        QVERIFY(patch);
        QCOMPARE(patch->path, QStringLiteral("/v1.0/me/events/ev-review"));
        QCOMPARE(patch->headers.value("if-match"), QByteArray("W/\"e1\""));
        QCOMPARE(QJsonDocument::fromJson(patch->body).object().keys(), QStringList{QStringLiteral("subject")});
        QCOMPARE(f.server.events.value(QStringLiteral("ev-review")).json.value(QLatin1String("subject")).toString(), QStringLiteral("Design review (v2)"));
        QCOMPARE(ExchangeTest::record(f.store.events(), id).value(QStringLiteral("remoteEtag")).toString(), f.server.etag(QStringLiteral("ev-review")));
        QVERIFY(syncNow(f.provider, QStringLiteral("s3")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.server.count("PATCH"), 1);

        // Both sides edited: a conflict, the server copy untouched.
        f.server.events[QStringLiteral("ev-review")].json.insert(QStringLiteral("subject"), QStringLiteral("Server title"));
        f.server.bump(QStringLiteral("ev-review"));
        review = ExchangeTest::record(f.store.events(), id);
        review.insert(QStringLiteral("title"), QStringLiteral("Local title"));
        f.store.upsert(QStringLiteral("events"), review);
        result = syncNow(f.provider, QStringLiteral("s4"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QCOMPARE(f.server.count("PATCH"), 1);
        QCOMPARE(f.provider.conflicts().size(), 1);

        // The server moves again before the user decides: If-Match fails (412)
        // and the resolution reports a fresh conflict instead of overwriting.
        f.server.events[QStringLiteral("ev-review")].json.insert(QStringLiteral("subject"), QStringLiteral("Server title 2"));
        f.server.bump(QStringLiteral("ev-review"));
        result = changeNow(f.provider, [&] { f.provider.resolveConflict(QStringLiteral("keep-1"), QStringLiteral("events"), id, QStringLiteral("local")); });
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QCOMPARE(f.server.events.value(QStringLiteral("ev-review")).json.value(QLatin1String("subject")).toString(), QStringLiteral("Server title 2"));
        QCOMPARE(f.provider.conflicts().first().toMap().value(QStringLiteral("remoteVersion")).toString(), f.server.etag(QStringLiteral("ev-review")));
        result = changeNow(f.provider, [&] { f.provider.resolveConflict(QStringLiteral("keep-2"), QStringLiteral("events"), id, QStringLiteral("local")); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(f.server.events.value(QStringLiteral("ev-review")).json.value(QLatin1String("subject")).toString(), QStringLiteral("Local title"));
        QVERIFY(f.provider.conflicts().isEmpty());

        f.server.events[QStringLiteral("ev-review")].json.insert(QStringLiteral("subject"), QStringLiteral("Server again"));
        f.server.bump(QStringLiteral("ev-review"));
        review = ExchangeTest::record(f.store.events(), id);
        review.insert(QStringLiteral("location"), QStringLiteral("Room 9"));
        f.store.upsert(QStringLiteral("events"), review);
        QCOMPARE(syncNow(f.provider, QStringLiteral("s5")).value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        result = changeNow(f.provider, [&] { f.provider.resolveConflict(QStringLiteral("take"), QStringLiteral("events"), id, QStringLiteral("remote")); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(ExchangeTest::record(f.store.events(), id).value(QStringLiteral("title")).toString(), QStringLiteral("Server again"));
        QCOMPARE(ExchangeTest::record(f.store.events(), id).value(QStringLiteral("location")).toString(), QStringLiteral("Room 4"));
        const int patches = f.server.count("PATCH");
        QVERIFY(syncNow(f.provider, QStringLiteral("s6")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.server.count("PATCH"), patches);
        auditSecrets(secrets(), f.store.filePath());
    }
};

QTEST_MAIN(GraphProviderTest)
#include "tst_mail_exchange_graph.moc"
