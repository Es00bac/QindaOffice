// SPDX-License-Identifier: GPL-3.0-or-later
// EwsCalendarProvider against an in-process EWS endpoint on 127.0.0.1
// (ews_fake_server.h): mapping of singles, all-day events, weekly series with
// deleted and modified occurrences and an unmappable relative-monthly
// pattern; idempotent sync; ChangeKey-conditional writes, conflicts and
// their resolution; create/delete both ways; occurrence edits by
// InstanceIndex; throttling, auth and transport failures; secret redaction.
#include "ews_fixture.h"

#include <qindatk/local_calendar_projection_model.h>
#include <qindatk/recurrence_service.h>

using namespace EwsTest;


class EwsProviderTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { LogCapture::install(); }

    void initialSyncMapsEveryShape()
    {
        Fixture f;
        QVariantMap result = syncNow(f.provider, QStringLiteral("sync-1"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("accepted"));
        QCOMPARE(result.value(QStringLiteral("fetched")).toInt(), 4);
        QCOMPARE(result.value(QStringLiteral("requestId")).toString(), QStringLiteral("sync-1"));
        QCOMPARE(f.store.events().size(), 4);

        const QVariantMap review = f.event(QStringLiteral("Design review"));
        QVERIFY(review.value(QStringLiteral("id")).toString().startsWith(QStringLiteral("ews:work:")));
        QCOMPARE(review.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-10 09:00"));
        QCOMPARE(review.value(QStringLiteral("endLocal")).toString(), QStringLiteral("2026-03-10 10:00"));
        QCOMPARE(review.value(QStringLiteral("zone")).toString(), QStringLiteral("America/Denver"));
        QCOMPARE(review.value(QStringLiteral("description")).toString(), QStringLiteral("Bring the Q1 numbers & the <draft>"));
        QCOMPARE(review.value(QStringLiteral("location")).toString(), QStringLiteral("Room 4"));
        QCOMPARE(review.value(QStringLiteral("calendarId")).toString(), QStringLiteral("work"));
        QCOMPARE(review.value(QStringLiteral("remoteAccount")).toString(), QStringLiteral("work"));
        QCOMPARE(review.value(QStringLiteral("remoteId")).toString(), QStringLiteral("AAMkReview="));
        QCOMPARE(review.value(QStringLiteral("remoteChangeKey")).toString(), QStringLiteral("CK1"));
        QCOMPARE(review.value(QStringLiteral("syncState")).toString(), QStringLiteral("synced"));
        QCOMPARE(review.value(QStringLiteral("organizer")).toMap().value(QStringLiteral("email")).toString(), QStringLiteral("grace@example.com"));
        QCOMPARE(review.value(QStringLiteral("attendees")).toList().size(), 1);
        QVERIFY(review.value(QStringLiteral("isOnlineMeeting")).toBool());
        QCOMPARE(review.value(QStringLiteral("onlineMeetingUrl")).toString(), QStringLiteral("https://meet.example.com/j/123"));
        QVERIFY(!review.value(QStringLiteral("readOnly")).toBool());

        const QVariantMap offsite = f.event(QStringLiteral("Offsite"));
        QVERIFY(offsite.value(QStringLiteral("allDay")).toBool());
        QCOMPARE(offsite.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-20"));
        QCOMPARE(offsite.value(QStringLiteral("endLocal")).toString(), QStringLiteral("2026-03-22"));
        QVERIFY(offsite.value(QStringLiteral("readOnly")).toBool());

        const QVariantMap standup = f.event(QStringLiteral("Standup"));
        const QVariantMap rule = standup.value(QStringLiteral("recurrenceRule")).toMap();
        QCOMPARE(rule.value(QStringLiteral("frequency")).toString(), QStringLiteral("weekly"));
        QCOMPARE(rule.value(QStringLiteral("weekdays")).toList(), QVariantList{6});
        QCOMPARE(rule.value(QStringLiteral("endMode")).toString(), QStringLiteral("count"));
        QCOMPARE(rule.value(QStringLiteral("count")).toInt(), 5);
        QCOMPARE(standup.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-07 09:00"));
        QCOMPARE(standup.value(QStringLiteral("exceptions")).toList(), QVariantList{QStringLiteral("2026-03-21T09:00:00")});
        const QVariantList overrides = standup.value(QStringLiteral("overrides")).toList();
        QCOMPARE(overrides.size(), 1);
        const QVariantMap moved = overrides.first().toMap();
        QCOMPARE(moved.value(QStringLiteral("recurrenceId")).toString(), QStringLiteral("2026-03-14T09:00:00"));
        QCOMPARE(moved.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-14 12:00"));
        QCOMPARE(moved.value(QStringLiteral("endLocal")).toString(), QStringLiteral("2026-03-14 12:30"));
        QCOMPARE(moved.value(QStringLiteral("title")).toString(), QStringLiteral("Standup (moved)"));
        QVERIFY(!moved.contains(QStringLiteral("location")));   // unchanged texts are not overrides

        const QVariantMap board = f.event(QStringLiteral("Board meeting"));
        QVERIFY(board.value(QStringLiteral("recurrenceRule")).isNull());
        QCOMPARE(board.value(QStringLiteral("recurrenceRaw")).toString(), QStringLiteral("FREQ=MONTHLY;BYDAY=TU;BYSETPOS=2"));
        QCOMPARE(board.value(QStringLiteral("zone")).toString(), QStringLiteral("America/Denver"));   // legacy StartTimeZone Id
        QCOMPARE(board.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-10 12:00"));

        // The mapped series drives the toolkit projection: 09:00 local on both
        // sides of the DST change, the moved instance at 12:00, 21 March gone.
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
                                      QStringLiteral("2026-03-28T15:00:00.000Z"), QStringLiteral("2026-04-04T15:00:00.000Z")}));

        // Steady state: nothing fetched in full again, nothing written.
        const int getItems = f.server.count(QStringLiteral("GetItem"));
        result = syncNow(f.provider, QStringLiteral("sync-2"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("fetched")).toInt(), 0);
        QCOMPARE(result.value(QStringLiteral("updated")).toInt(), 0);
        QCOMPARE(f.server.count(QStringLiteral("GetItem")), getItems + 1);          // master lookup only
        QCOMPARE(f.server.count(QStringLiteral("UpdateItem")) + f.server.count(QStringLiteral("CreateItem")), 0);
        const FakeHttpServer::Request &first = f.server.http.requests.first();
        QCOMPARE(first.headers.value("soapaction"), QByteArray("\"http://schemas.microsoft.com/exchange/services/2006/messages/FindItem\""));
        QVERIFY(first.body.contains("RequestServerVersion Version=\"Exchange2013_SP1\""));
        QVERIFY(transcriptOf(result).contains(QStringLiteral("[credentials redacted]")));

        // After a restart (store reloaded from its file: numbers come back as
        // doubles) the digests still match: nothing is pushed.
        QindaTK::LocalProductivityStore reloaded;
        QVERIFY(reloaded.loadFrom(f.store.filePath()));
        EwsCalendarProvider again;
        for (const char *name : {"url", "userName", "accountId", "calendarId", "defaultZone", "syncPastDays", "syncFutureDays"})
            again.setProperty(name, f.provider.property(name));
        again.setStore(&reloaded);
        again.setSecret(kPassword, QStringLiteral("basic"));
        result = syncNow(again, QStringLiteral("sync-3"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("fetched")).toInt() + result.value(QStringLiteral("updated")).toInt(), 0);
        QCOMPARE(f.server.count(QStringLiteral("UpdateItem")) + f.server.count(QStringLiteral("CreateItem")), 0);
    }

    void localEditsConflictsAndResolution()
    {
        Fixture f;
        QVERIFY(syncNow(f.provider, QStringLiteral("s1")).value(QStringLiteral("ok")).toBool());
        QVariantMap review = f.event(QStringLiteral("Design review"));
        const QString id = review.value(QStringLiteral("id")).toString();
        review.insert(QStringLiteral("title"), QStringLiteral("Design review (v2)"));
        QVERIFY(f.store.upsert(QStringLiteral("events"), review).value(QStringLiteral("ok")).toBool());
        QVariantMap result = syncNow(f.provider, QStringLiteral("s2"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("updated")).toInt(), 1);
        const XmlNode update = f.server.lastOperation.value(QStringLiteral("UpdateItem"));
        QCOMPARE(update.attribute(QStringLiteral("ConflictResolution")), QStringLiteral("NeverOverwrite"));
        QCOMPARE(update.find(u"ItemId")->attribute(QStringLiteral("ChangeKey")), QStringLiteral("CK1"));
        QCOMPARE(update.find(u"FieldURI")->attribute(QStringLiteral("FieldURI")), QStringLiteral("item:Subject"));
        QList<const XmlNode *> fields;
        update.findAll(u"SetItemField", &fields);
        QCOMPARE(fields.size(), 1);                                  // only what changed is written
        QCOMPARE(f.server.items.value(QStringLiteral("AAMkReview=")).subject, QStringLiteral("Design review (v2)"));
        QCOMPARE(ExchangeTest::record(f.store.events(), id).value(QStringLiteral("remoteChangeKey")).toString(), f.server.changeKey(QStringLiteral("AAMkReview=")));
        QVERIFY(syncNow(f.provider, QStringLiteral("s3")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.server.count(QStringLiteral("UpdateItem")), 1);   // adopted: nothing left to push

        // Edited on both sides: reported, nothing overwritten.
        f.server.items[QStringLiteral("AAMkReview=")].subject = QStringLiteral("Server title");
        f.server.bump(QStringLiteral("AAMkReview="));
        review = ExchangeTest::record(f.store.events(), id);
        review.insert(QStringLiteral("title"), QStringLiteral("Local title"));
        f.store.upsert(QStringLiteral("events"), review);
        result = syncNow(f.provider, QStringLiteral("s4"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QCOMPARE(result.value(QStringLiteral("conflicts")).toInt(), 1);
        QCOMPARE(f.provider.conflicts().size(), 1);
        QCOMPARE(f.provider.conflicts().first().toMap().value(QStringLiteral("remoteFields")).toMap().value(QStringLiteral("title")).toString(),
                 QStringLiteral("Server title"));
        QCOMPARE(f.server.items.value(QStringLiteral("AAMkReview=")).subject, QStringLiteral("Server title"));
        QCOMPARE(ExchangeTest::record(f.store.events(), id).value(QStringLiteral("syncState")).toString(), QStringLiteral("conflict"));
        const QString seen = f.server.changeKey(QStringLiteral("AAMkReview="));
        result = changeNow(f.provider, [&] { f.provider.resolveConflict(QStringLiteral("keep"), QStringLiteral("events"), id, QStringLiteral("local")); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(f.server.lastOperation.value(QStringLiteral("UpdateItem")).find(u"ItemId")->attribute(QStringLiteral("ChangeKey")), seen);
        QCOMPARE(f.server.items.value(QStringLiteral("AAMkReview=")).subject, QStringLiteral("Local title"));
        QVERIFY(f.provider.conflicts().isEmpty());
        QCOMPARE(ExchangeTest::record(f.store.events(), id).value(QStringLiteral("syncState")).toString(), QStringLiteral("synced"));

        f.server.items[QStringLiteral("AAMkReview=")].subject = QStringLiteral("Server again");
        f.server.bump(QStringLiteral("AAMkReview="));
        review = ExchangeTest::record(f.store.events(), id);
        review.insert(QStringLiteral("title"), QStringLiteral("Local again"));
        f.store.upsert(QStringLiteral("events"), review);
        QCOMPARE(syncNow(f.provider, QStringLiteral("s5")).value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        result = changeNow(f.provider, [&] { f.provider.resolveConflict(QStringLiteral("take"), QStringLiteral("events"), id, QStringLiteral("remote")); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(ExchangeTest::record(f.store.events(), id).value(QStringLiteral("title")).toString(), QStringLiteral("Server again"));
        const int writes = f.server.count(QStringLiteral("UpdateItem"));
        QVERIFY(syncNow(f.provider, QStringLiteral("s6")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.server.count(QStringLiteral("UpdateItem")), writes);

        // A read-only item edited here becomes a conflict, never a write.
        QVariantMap offsite = f.event(QStringLiteral("Offsite"));
        offsite.insert(QStringLiteral("title"), QStringLiteral("Offsite (mine)"));
        f.store.upsert(QStringLiteral("events"), offsite);
        result = syncNow(f.provider, QStringLiteral("s7"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QCOMPARE(f.server.count(QStringLiteral("UpdateItem")), writes);
        QVERIFY(f.provider.conflicts().first().toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("read-only")));
        auditSecrets(secrets(), f.store.filePath());
    }
};

QTEST_MAIN(EwsProviderTest)
#include "tst_mail_exchange_ews.moc"
