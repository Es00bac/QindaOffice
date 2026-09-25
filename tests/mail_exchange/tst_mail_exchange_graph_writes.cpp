// SPDX-License-Identifier: GPL-3.0-or-later
// GraphCalendarProvider writes and failures against the in-process Graph
// calendar: POST (Windows zone, transactionId), DELETE with If-Match,
// remote deletions, occurrence exclusion and moves through instance ids,
// the calendar list, 401 / 429 Retry-After / refused connection / a
// nextLink to a foreign host / plain HTTP / non-bearer credentials, and the
// secret audit over everything the run produced.
#include "graph_fixture.h"

using namespace GraphTest;

class GraphWritesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { LogCapture::install(); }

    void createDeleteAndRemoteRemoval()
    {
        Fixture f;
        QVERIFY(syncNow(f.provider, QStringLiteral("s1")).value(QStringLiteral("ok")).toBool());
        QVERIFY(f.store.upsert(QStringLiteral("events"), {{QStringLiteral("id"), QStringLiteral("local-1")},
            {QStringLiteral("title"), QStringLiteral("Lunch & learn")}, {QStringLiteral("description"), QStringLiteral("Line one\nLine two")},
            {QStringLiteral("startLocal"), QStringLiteral("2026-04-02 12:00")}, {QStringLiteral("endLocal"), QStringLiteral("2026-04-02 13:00")},
            {QStringLiteral("zone"), QStringLiteral("Europe/Zurich")}, {QStringLiteral("allDay"), false},
            {QStringLiteral("remoteAccount"), QStringLiteral("m365")}}).value(QStringLiteral("ok")).toBool());
        QVariantMap result = syncNow(f.provider, QStringLiteral("s2"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("created")).toInt(), 1);
        QCOMPARE(f.server.posted.size(), 1);
        const QJsonObject posted = f.server.posted.first();
        QCOMPARE(posted.value(QLatin1String("start")).toObject().value(QLatin1String("dateTime")).toString(), QStringLiteral("2026-04-02T12:00:00"));
        QCOMPARE(posted.value(QLatin1String("start")).toObject().value(QLatin1String("timeZone")).toString(), QStringLiteral("W. Europe Standard Time"));
        QCOMPARE(posted.value(QLatin1String("body")).toObject().value(QLatin1String("contentType")).toString(), QStringLiteral("text"));
        QCOMPARE(posted.value(QLatin1String("transactionId")).toString().size(), 32);
        const QVariantMap created = ExchangeTest::record(f.store.events(), QStringLiteral("local-1"));
        const QString remoteId = created.value(QStringLiteral("remoteId")).toString();
        QVERIFY(f.server.events.contains(remoteId));
        QCOMPARE(created.value(QStringLiteral("zone")).toString(), QStringLiteral("Europe/Zurich"));
        QCOMPARE(created.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-04-02 12:00"));
        QCOMPARE(created.value(QStringLiteral("description")).toString(), QStringLiteral("Line one\nLine two"));
        QCOMPARE(created.value(QStringLiteral("remoteEtag")).toString(), f.server.etag(remoteId));
        QVERIFY(syncNow(f.provider, QStringLiteral("s3")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.server.count("POST") + f.server.count("PATCH"), 1);

        const QVariantMap review = f.event(QStringLiteral("Design review"));
        result = changeNow(f.provider, [&] {
            f.provider.removeRemote(QStringLiteral("rm"), QStringLiteral("events"), review.value(QStringLiteral("id")).toString());
        });
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("deleted")).toInt(), 1);
        QCOMPARE(f.server.http.requests.last().method, QByteArray("DELETE"));
        QCOMPARE(f.server.http.requests.last().headers.value("if-match"), QByteArray("W/\"e1\""));
        QVERIFY(!f.server.events.contains(QStringLiteral("ev-review")));
        QVERIFY(f.event(QStringLiteral("Design review")).isEmpty());

        f.server.events.remove(remoteId);
        result = syncNow(f.provider, QStringLiteral("s4"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(result.value(QStringLiteral("deleted")).toInt(), 1);
        QVERIFY(ExchangeTest::record(f.store.events(), QStringLiteral("local-1")).isEmpty());
        // A stale removeRemote (the server moved on) is a conflict, not a delete.
        f.server.bump(QStringLiteral("ev-offsite"));
        const QString offsite = f.event(QStringLiteral("Offsite")).value(QStringLiteral("id")).toString();
        result = changeNow(f.provider, [&] { f.provider.removeRemote(QStringLiteral("rm2"), QStringLiteral("events"), offsite); });
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QVERIFY(f.server.events.contains(QStringLiteral("ev-offsite")));
        auditSecrets(secrets(), f.store.filePath());
    }

    void occurrencesThroughInstanceIds()
    {
        Fixture f;
        QVERIFY(syncNow(f.provider, QStringLiteral("s1")).value(QStringLiteral("ok")).toBool());
        const QString id = f.event(QStringLiteral("Standup")).value(QStringLiteral("id")).toString();
        QVERIFY(f.store.setOccurrenceExcluded(id, id + QStringLiteral("/2026-04-04T09:00:00"), true).value(QStringLiteral("ok")).toBool());
        QVERIFY(f.store.shiftEventLocal(id, 60, QStringLiteral("occurrence"), id + QStringLiteral("/2026-03-07T09:00:00"))
                    .value(QStringLiteral("ok")).toBool());
        QVariantMap result = syncNow(f.provider, QStringLiteral("s2"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        QCOMPARE(f.server.count("DELETE", QStringLiteral("/me/events/occ-04")), 1);
        QCOMPARE(f.server.count("PATCH", QStringLiteral("/me/events/occ-07")), 1);
        const QList<QJsonObject> &instances = f.server.events.value(QStringLiteral("ev-standup")).instances;
        QCOMPARE(instances.size(), 3);
        QCOMPARE(instances.first().value(QLatin1String("type")).toString(), QStringLiteral("exception"));
        QCOMPARE(instances.first().value(QLatin1String("start")).toObject().value(QLatin1String("dateTime")).toString(),
                 QStringLiteral("2026-03-07T17:00:00.0000000"));                 // 10:00 MST
        const QVariantMap standup = f.event(QStringLiteral("Standup"));
        QCOMPARE(standup.value(QStringLiteral("exceptions")).toList(),
                 QVariantList({QStringLiteral("2026-03-21T09:00:00"), QStringLiteral("2026-04-04T09:00:00")}));
        QCOMPARE(standup.value(QStringLiteral("overrides")).toList().first().toMap().value(QStringLiteral("startLocal")).toString(),
                 QStringLiteral("2026-03-07 10:00"));
        QCOMPARE(standup.value(QStringLiteral("remoteEtag")).toString(), f.server.etag(QStringLiteral("ev-standup")));
        const int writes = f.server.count("PATCH") + f.server.count("DELETE");
        QVERIFY(syncNow(f.provider, QStringLiteral("s3")).value(QStringLiteral("ok")).toBool());
        QCOMPARE(f.server.count("PATCH") + f.server.count("DELETE"), writes);

        // Microsoft 365 cannot bring a deleted occurrence back.
        QVERIFY(f.store.setOccurrenceExcluded(id, id + QStringLiteral("/2026-03-21T09:00:00"), false).value(QStringLiteral("ok")).toBool());
        result = syncNow(f.provider, QStringLiteral("s4"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QVERIFY(f.provider.conflicts().first().toMap().value(QStringLiteral("reason")).toString().contains(QStringLiteral("deleted occurrence")));
    }

    void failuresAndCredentials()
    {
        Fixture f;
        f.provider.setSecret(QStringLiteral("expired-") + kToken, QStringLiteral("bearer"));
        QVariantMap result = syncNow(f.provider, QStringLiteral("expired"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(result.value(QStringLiteral("authRejected")).toBool());
        QVERIFY(!result.value(QStringLiteral("retryable")).toBool());
        f.provider.setSecret(kToken, QStringLiteral("bearer"));

        f.server.throttle = true;
        result = syncNow(f.provider, QStringLiteral("throttled"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());
        QCOMPARE(result.value(QStringLiteral("retryAfterSeconds")).toInt(), 7);
        f.server.throttle = false;

        // A nextLink to another origin would carry the token away: refused.
        f.server.foreignNextLink = QStringLiteral("https://attacker.example.net/v1.0/me/calendar/events?$skip=2");
        result = syncNow(f.provider, QStringLiteral("foreign"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(errorOf(result).contains(QStringLiteral("attacker.example.net")));
        QVERIFY(f.store.events().isEmpty());
        f.server.foreignNextLink.clear();

        QTcpServer probe;
        probe.listen(QHostAddress::LocalHost, 0);
        const quint16 closed = probe.serverPort();
        probe.close();
        f.provider.setProperty("baseUrl", QStringLiteral("http://127.0.0.1:%1/v1.0").arg(closed));
        result = syncNow(f.provider, QStringLiteral("refused"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("offline"));
        QVERIFY(result.value(QStringLiteral("retryable")).toBool());

        const int before = int(f.server.http.requests.size());
        f.provider.setProperty("baseUrl", QStringLiteral("http://graph.example.test/v1.0"));
        result = syncNow(f.provider, QStringLiteral("plain"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(errorOf(result).contains(QStringLiteral("plain HTTP")));
        f.provider.setProperty("baseUrl", f.server.base());
        f.provider.setSecret(kToken, QStringLiteral("basic"));
        result = syncNow(f.provider, QStringLiteral("basic"));
        QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("rejected"));
        QVERIFY(errorOf(result).contains(QStringLiteral("bearer")));
        QCOMPARE(int(f.server.http.requests.size()), before);

        f.provider.setSecret(kToken, QStringLiteral("bearer"));
        result = calendarsNow(f.provider, QStringLiteral("calendars"));
        QVERIFY2(result.value(QStringLiteral("ok")).toBool(), qPrintable(errorOf(result)));
        const QVariantList calendars = result.value(QStringLiteral("calendars")).toList();
        QCOMPARE(calendars.size(), 2);
        QCOMPARE(calendars.at(0).toMap().value(QStringLiteral("color")).toString(), QStringLiteral("#1f6feb"));
        QVERIFY(calendars.at(0).toMap().value(QStringLiteral("isDefault")).toBool());
        QCOMPARE(calendars.at(1).toMap().value(QStringLiteral("color")).toString(), QStringLiteral("lightGreen"));
        QVERIFY(calendars.at(1).toMap().value(QStringLiteral("readOnly")).toBool());

        // A read-only calendar marks its events, and local edits become conflicts.
        f.server.canEdit = false;
        QVERIFY(syncNow(f.provider, QStringLiteral("ro")).value(QStringLiteral("ok")).toBool());
        QVariantMap review = f.event(QStringLiteral("Design review"));
        QVERIFY(review.value(QStringLiteral("readOnly")).toBool());
        review.insert(QStringLiteral("title"), QStringLiteral("Mine"));
        f.store.upsert(QStringLiteral("events"), review);
        QCOMPARE(syncNow(f.provider, QStringLiteral("ro-edit")).value(QStringLiteral("state")).toString(), QStringLiteral("conflict"));
        QCOMPARE(f.server.count("PATCH"), 0);
        auditSecrets(secrets(), f.store.filePath());
    }
};

QTEST_MAIN(GraphWritesTest)
#include "tst_mail_exchange_graph_writes.moc"
