// SPDX-License-Identifier: GPL-3.0-or-later
// EwsCalendarProvider writes and failures against the in-process EWS
// endpoint: CreateItem (Windows zone, SendToNone) and removeRemote, remote
// deletions (clean and edited), occurrence moves/exclusions by
// InstanceIndex, an occurrence Exchange cannot restore, calendar folders,
// 401 / 503 Retry-After / ErrorServerBusy / refused connection / plain HTTP,
// bearer tokens, and the secret audit over everything the run produced.
#include "ews_fixture.h"

using namespace EwsTest;

class EwsWritesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { LogCapture::install(); }

    void createDeleteAndRemoteRemoval()
    {
        Fixture f;
        QVERIFY(syncNow(f.provider, QStringLiteral("s1")).value(QStringLiteral("ok")).toBool());
        // Zurich shares CLDR's "W. Europe Standard Time" with Berlin: the
        // record keeps its own zone through the write and the re-read.
        QVERIFY(f.store.upsert(QStringLiteral("events"), {{QStringLiteral("id"), QStringLiteral("local-1")},
            {QStringLiteral("title"), QStringLiteral("Lunch & learn <Qt>")}, {QStringLiteral("startLocal"), QStringLiteral("2026-04-02 12:00")},
            {QStringLiteral("endLocal"), QStringLiteral("2026-04-02 13:00")}, {QStringLiteral("zone"), QStringLiteral("Europe/Zurich")},
            {QStringLiteral("allDay"), false}, {QStringLiteral("remoteAccount"), QStringLiteral("work")}}).value(QStringLiteral("ok")).toBool());
        QVariantMap result = syncNow(f.provider, QStringLiteral("s2"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("created")).toInt(), 1);
        const XmlNode create = f.server.lastOperation.value(QStringLiteral("CreateItem"));
        QCOMPARE(create.attribute(QStringLiteral("SendMeetingInvitations")), QStringLiteral("SendToNone"));
        const XmlNode *item = create.find(u"CalendarItem");
        QCOMPARE(item->childText(u"Subject"), QStringLiteral("Lunch & learn <Qt>"));
        QCOMPARE(item->childText(u"Start"), QStringLiteral("2026-04-02T10:00:00Z"));       // CEST, UTC+2
        QCOMPARE(item->child(u"StartTimeZone")->attribute(QStringLiteral("Id")), QStringLiteral("W. Europe Standard Time"));
        QVariantMap created = ExchangeTest::record(f.store.events(), QStringLiteral("local-1"));
        const QString remoteId = created.value(QStringLiteral("remoteId")).toString();
        QVERIFY(f.server.items.contains(remoteId));
        QCOMPARE(created.value(QStringLiteral("zone")).toString(), QStringLiteral("Europe/Zurich"));
        QCOMPARE(created.value(QStringLiteral("remoteChangeKey")).toString(), f.server.changeKey(remoteId));
        QCOMPARE(created.value(QStringLiteral("calendarId")).toString(), QStringLiteral("work"));
        QVERIFY(syncNow(f.provider, QStringLiteral("s3")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.server.count(QStringLiteral("CreateItem")) + f.server.count(QStringLiteral("UpdateItem")), 1);

        // removeRemote: DeleteItem on the stored ChangeKey, then locally.
        const QVariantMap review = f.event(QStringLiteral("Design review"));
        result = changeNow(f.provider, [&] {
            f.provider.removeRemote(QStringLiteral("rm"), QStringLiteral("events"), review.value(QStringLiteral("id")).toString());
        });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("deleted")).toInt(), 1);
        const XmlNode remove = f.server.lastOperation.value(QStringLiteral("DeleteItem"));
        QCOMPARE(remove.attribute(QStringLiteral("SendMeetingCancellations")), QStringLiteral("SendToNone"));
        QCOMPARE(remove.find(u"ItemId")->attribute(QStringLiteral("ChangeKey")), QStringLiteral("CK1"));
        QVERIFY(!f.server.items.contains(QStringLiteral("AAMkReview=")));
        QVERIFY(f.event(QStringLiteral("Design review")).isEmpty());

        // Deleted on the server: removed here; if edited here, a conflict.
        f.server.items.remove(remoteId);
        result = syncNow(f.provider, QStringLiteral("s4"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("deleted")).toInt(), 1);
        QVERIFY(ExchangeTest::record(f.store.events(), QStringLiteral("local-1")).isEmpty());
        QVariantMap offsite = f.event(QStringLiteral("Offsite"));
        f.server.items[QStringLiteral("AAMkOffsite=")].canModify = true;
        f.server.bump(QStringLiteral("AAMkOffsite="));
        QVERIFY(syncNow(f.provider, QStringLiteral("s5")).value(QStringLiteral("ok")).toBool());   // now writable
        offsite = f.event(QStringLiteral("Offsite"));
        offsite.insert(QStringLiteral("location"), QStringLiteral("Lake house"));
        f.store.upsert(QStringLiteral("events"), offsite);
        f.server.items.remove(QStringLiteral("AAMkOffsite="));
        result = syncNow(f.provider, QStringLiteral("s6"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QVERIFY(f.provider.conflicts().first().toMap().value(QStringLiteral("remoteDeleted")).toBool());
        const QString offsiteId = offsite.value(QStringLiteral("id")).toString();
        result = changeNow(f.provider, [&] { f.provider.resolveConflict(QStringLiteral("r"), QStringLiteral("events"), offsiteId, QStringLiteral("local")); });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        const XmlNode again = f.server.lastOperation.value(QStringLiteral("CreateItem"));
        QCOMPARE(again.find(u"CalendarItem")->childText(u"Location"), QStringLiteral("Lake house"));
        QCOMPARE(again.find(u"CalendarItem")->childText(u"IsAllDayEvent"), QStringLiteral("true"));
        QCOMPARE(again.find(u"CalendarItem")->childText(u"Start"), QStringLiteral("2026-03-20T06:00:00Z"));
        QCOMPARE(f.event(QStringLiteral("Offsite")).value(QStringLiteral("endLocal")).toString(), QStringLiteral("2026-03-22"));
        auditSecrets(secrets(), f.store.filePath());
    }

    void occurrenceEditsUseInstanceIndex()
    {
        Fixture f;
        QVERIFY(syncNow(f.provider, QStringLiteral("s1")).value(QStringLiteral("ok")).toBool());
        const QString id = f.event(QStringLiteral("Standup")).value(QStringLiteral("id")).toString();
        // Move only 28 March by an hour: UpdateItem on OccurrenceItemId #4.
        QVERIFY(f.store.shiftEventLocal(id, 60, QStringLiteral("occurrence"), id + QStringLiteral("/2026-03-28T09:00:00"))
                    .value(QStringLiteral("ok")).toBool());
        QVariantMap result = syncNow(f.provider, QStringLiteral("s2"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        const XmlNode *occurrence = f.server.lastOperation.value(QStringLiteral("UpdateItem")).find(u"OccurrenceItemId");
        QVERIFY(occurrence);
        QCOMPARE(occurrence->attribute(QStringLiteral("RecurringMasterId")), QStringLiteral("AAMkStandup="));
        QCOMPARE(occurrence->attribute(QStringLiteral("InstanceIndex")), QStringLiteral("4"));
        const EwsFakeServer::Item &server = f.server.items[QStringLiteral("AAMkStandup=")];
        QCOMPARE(server.modified.size(), 2);
        QCOMPARE(server.modified.last().start, utc("2026-03-28T16:00:00Z"));
        QVariantMap standup = f.event(QStringLiteral("Standup"));
        QCOMPARE(standup.value(QStringLiteral("overrides")).toList().size(), 2);
        QCOMPARE(standup.value(QStringLiteral("overrides")).toList().last().toMap().value(QStringLiteral("startLocal")).toString(),
                 QStringLiteral("2026-03-28 10:00"));
        QCOMPARE(standup.value(QStringLiteral("remoteChangeKey")).toString(), f.server.changeKey(QStringLiteral("AAMkStandup=")));
        const int updates = f.server.count(QStringLiteral("UpdateItem"));
        QVERIFY(syncNow(f.provider, QStringLiteral("s3")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.server.count(QStringLiteral("UpdateItem")), updates);     // stable after the re-read

        // Exclude 4 April: DeleteItem on OccurrenceItemId #5.
        QVERIFY(f.store.setOccurrenceExcluded(id, id + QStringLiteral("/2026-04-04T09:00:00"), true).value(QStringLiteral("ok")).toBool());
        result = syncNow(f.provider, QStringLiteral("s4"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(f.server.lastOperation.value(QStringLiteral("DeleteItem")).find(u"OccurrenceItemId")->attribute(QStringLiteral("InstanceIndex")),
                 QStringLiteral("5"));
        QVERIFY(f.server.items[QStringLiteral("AAMkStandup=")].deleted.contains(utc("2026-04-04T15:00:00Z")));
        standup = f.event(QStringLiteral("Standup"));
        QCOMPARE(standup.value(QStringLiteral("exceptions")).toList(),
                 QVariantList({QStringLiteral("2026-03-21T09:00:00"), QStringLiteral("2026-04-04T09:00:00")}));

        // Bringing back a deleted occurrence is not something Exchange can do.
        QVERIFY(f.store.setOccurrenceExcluded(id, id + QStringLiteral("/2026-03-21T09:00:00"), false).value(QStringLiteral("ok")).toBool());
        result = syncNow(f.provider, QStringLiteral("s5"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QVERIFY(f.provider.conflicts().first().toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("deleted occurrence")));
        result = changeNow(f.provider, [&] { f.provider.resolveConflict(QStringLiteral("r"), QStringLiteral("events"), id, QStringLiteral("remote")); });
        QVERIFY(result.value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.event(QStringLiteral("Standup")).value(QStringLiteral("exceptions")).toList().size(), 2);

        // A second-Tuesday series keeps its pattern: only the title is written.
        QVariantMap board = f.event(QStringLiteral("Board meeting"));
        board.insert(QStringLiteral("title"), QStringLiteral("Board meeting (Q2)"));
        f.store.upsert(QStringLiteral("events"), board);
        result = syncNow(f.provider, QStringLiteral("s6"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QList<const XmlNode *> fields;
        f.server.lastOperation.value(QStringLiteral("UpdateItem")).findAll(u"FieldURI", &fields);
        QCOMPARE(fields.size(), 1);
        QCOMPARE(f.server.items.value(QStringLiteral("AAMkBoard=")).recurrence.find(u"DayOfWeekIndex")->text, QStringLiteral("Second"));
    }

    void failuresAndCredentials()
    {
        Fixture f;
        // Validation failures still arrive through the signal, on a later turn.
        f.provider.clearSecret();
        QSignalSpy spy(&f.provider, &EwsCalendarProvider::syncFinished);
        QVERIFY(!f.provider.sync(QStringLiteral("no-secret")));
        QCOMPARE(spy.count(), 0);
        QVariantMap result = waitFor(spy);
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QCOMPARE(result.value(QStringLiteral("requestId")).toString(), QStringLiteral("no-secret"));

        f.provider.setSecret(QStringLiteral("wrong-") + kPassword, QStringLiteral("basic"));
        result = syncNow(f.provider, QStringLiteral("bad-password"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(!result.value(QStringLiteral("retryable")).toBool());
        QVERIFY(result.value(QStringLiteral("authRejected")).toBool());
        QVERIFY(errorOf(result).contains(QStringLiteral("401")));
        QVERIFY(!transcriptOf(result).contains(QStringLiteral("wrong-")));
        f.provider.setSecret(kPassword, QStringLiteral("basic"));

        f.server.throttle = true;
        result = syncNow(f.provider, QStringLiteral("throttled"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());
        QCOMPARE(result.value(QStringLiteral("retryAfterSeconds")).toInt(), 12);
        f.server.throttle = false;
        f.server.busy = true;
        result = syncNow(f.provider, QStringLiteral("busy"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QCOMPARE(result.value(QStringLiteral("retryAfterSeconds")).toInt(), 30);
        f.server.busy = false;
        QVERIFY(f.store.events().isEmpty());

        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        const quint16 closed = probe.serverPort();
        probe.close();
        f.provider.setProperty("url", QStringLiteral("http://127.0.0.1:%1/EWS/Exchange.asmx").arg(closed));
        result = syncNow(f.provider, QStringLiteral("refused"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());

        const int before = int(f.server.http.requests.size());
        f.provider.setProperty("url", QStringLiteral("http://mail.example.test/EWS/Exchange.asmx"));
        result = syncNow(f.provider, QStringLiteral("plain"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(errorOf(result).contains(QStringLiteral("plain HTTP")));
        QCOMPARE(int(f.server.http.requests.size()), before);

        // Bearer (OAuth) tokens, and the calendar folders.
        f.provider.setProperty("url", f.server.url());
        f.server.http.authorization = "Bearer " + kToken.toUtf8();
        f.provider.setSecret(kToken, QStringLiteral("bearer"));
        result = syncNow(f.provider, QStringLiteral("bearer"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(f.server.http.requests.last().headers.value("x-anchormailbox"), kUser.toUtf8());
        result = calendarsNow(f.provider, QStringLiteral("folders"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        const QVariantList calendars = result.value(QStringLiteral("calendars")).toList();
        QCOMPARE(calendars.size(), 2);
        QVERIFY(calendars.at(0).toMap().value(QStringLiteral("isDefault")).toBool());
        QCOMPARE(calendars.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Team <shared> & co"));
        QVERIFY(calendars.at(1).toMap().value(QStringLiteral("readOnly")).toBool());
        f.provider.setSecret(kToken, QStringLiteral("xoauth2"));
        QVERIFY(errorOf(syncNow(f.provider, QStringLiteral("mechanism"))).contains(QStringLiteral("basic")));
        auditSecrets(secrets(), f.store.filePath());
    }
};

QTEST_MAIN(EwsWritesTest)
#include "tst_mail_exchange_ews_writes.moc"
