// SPDX-License-Identifier: GPL-3.0-or-later
// Pure mapping pieces of mail_exchange: Windows <-> IANA zones (with the
// record's own zone preferred), DST-sensitive wall/instant conversion, the
// shared recurrence pattern (rule vs raw, Exchange's month-end semantics),
// Retry-After parsing, override normalization, and the XML tree's refusals.
#include "exchange_common.h"
#include "recurrence_pattern.h"
#include "windows_zones.h"
#include "xml_tree.h"

#include <QtTest>

using namespace QindaOffice::Mail;

class MappingTest : public QObject {
    Q_OBJECT
private slots:
    void windowsZonesMapBothWays()
    {
        QCOMPARE(Zones::toIana(QStringLiteral("Mountain Standard Time")), QStringLiteral("America/Denver"));
        QCOMPARE(Zones::toIana(QStringLiteral("W. Europe Standard Time")), QStringLiteral("Europe/Berlin"));
        // The record's own zone wins when it shares the Windows zone.
        QCOMPARE(Zones::toIana(QStringLiteral("W. Europe Standard Time"), {QStringLiteral("Europe/Zurich")}), QStringLiteral("Europe/Zurich"));
        QCOMPARE(Zones::toIana(QStringLiteral("W. Europe Standard Time"), {QStringLiteral("America/Denver")}), QStringLiteral("Europe/Berlin"));
        QCOMPARE(Zones::toIana(QStringLiteral("America/Denver")), QStringLiteral("America/Denver"));   // IANA passes through
        QCOMPARE(Zones::toIana(QStringLiteral("tzone://Microsoft/Utc")), QStringLiteral("UTC"));
        QCOMPARE(Zones::toIana(QStringLiteral("UTC"), {QStringLiteral("Etc/UTC")}), QStringLiteral("Etc/UTC"));
        QVERIFY(Zones::toIana(QStringLiteral("tzone://Microsoft/Custom")).isEmpty());
        QVERIFY(Zones::toIana(QStringLiteral("Customized Time Zone")).isEmpty());
        QCOMPARE(Zones::toWindows(QStringLiteral("Europe/Zurich")), QStringLiteral("W. Europe Standard Time"));
        QCOMPARE(Zones::toWindows(QStringLiteral("Asia/Kolkata")), QStringLiteral("India Standard Time"));
        QCOMPARE(Zones::toWindows(QStringLiteral("Australia/Lord_Howe")), QStringLiteral("Lord Howe Standard Time"));
        QCOMPARE(Zones::toWindows(QStringLiteral("UTC")), QStringLiteral("UTC"));
    }

    void wallTimesFollowDaylightSaving()
    {
        // 09:00 in Denver is 16:00Z before 8 March 2026 and 15:00Z after.
        QCOMPARE(wallTime(QStringLiteral("2026-03-07 09:00"), QStringLiteral("America/Denver")).toUTC(),
                 QDateTime(QDate(2026, 3, 7), QTime(16, 0), QTimeZone::UTC));
        QCOMPARE(wallTime(QStringLiteral("2026-03-14T09:00:00"), QStringLiteral("America/Denver")).toUTC(),
                 QDateTime(QDate(2026, 3, 14), QTime(15, 0), QTimeZone::UTC));
        // Europe changes on 29 March: 12:00 Zurich is 11:00Z, then 10:00Z.
        QCOMPARE(wallTime(QStringLiteral("2026-03-28 12:00"), QStringLiteral("Europe/Zurich")).toUTC().time(), QTime(11, 0));
        QCOMPARE(wallTime(QStringLiteral("2026-04-02 12:00"), QStringLiteral("Europe/Zurich")).toUTC().time(), QTime(10, 0));
        // All-day starts at the zone's midnight; Santiago skips local midnight on 6 September 2026.
        QCOMPARE(wallTime(QStringLiteral("2026-03-20"), QStringLiteral("America/Denver")).toUTC(),
                 QDateTime(QDate(2026, 3, 20), QTime(6, 0), QTimeZone::UTC));
        QCOMPARE(wallTime(QStringLiteral("2026-09-06"), QStringLiteral("America/Santiago")).time(), QTime(1, 0));
        const QDateTime instant(QDate(2026, 11, 1), QTime(8, 30), QTimeZone::UTC);     // the fall-back fold
        QCOMPARE(localText(toWall(instant, QStringLiteral("America/Denver")), false), QStringLiteral("2026-11-01 01:30"));
        QCOMPARE(occurrenceKey(toWall(instant, QStringLiteral("America/Denver")), false), QStringLiteral("2026-11-01T01:30:00"));
        QVERIFY(!wallTime(QStringLiteral("2026-03-07 09:00"), QStringLiteral("Mountain Standard Time")).isValid());
    }

    void patternsMapToRulesOrRaw()
    {
        RecurrencePattern weekly;
        weekly.type = QStringLiteral("weekly");
        weekly.days = {6};
        weekly.firstDayOfWeek = QStringLiteral("sunday");
        weekly.range = QStringLiteral("numbered");
        weekly.count = 5;
        MappedRecurrence mapped = patternToRule(weekly, QDate(2026, 3, 7));
        QCOMPARE(mapped.rule.toMap().value(QStringLiteral("weekdays")).toList(), QVariantList{6});
        QVERIFY(mapped.raw.isEmpty());
        weekly.interval = 2;
        weekly.days = {1, 7};                       // Sunday-start fortnights group differently
        mapped = patternToRule(weekly, QDate(2026, 3, 9));
        QVERIFY(mapped.rule.isNull());
        QCOMPARE(mapped.raw, QStringLiteral("FREQ=WEEKLY;INTERVAL=2;BYDAY=MO,SU;WKST=SU;COUNT=5"));

        RecurrencePattern monthEnd;
        monthEnd.type = QStringLiteral("absoluteMonthly");
        monthEnd.dayOfMonth = 31;
        monthEnd.range = QStringLiteral("endDate");
        monthEnd.endDate = QDate(2026, 12, 31);
        mapped = patternToRule(monthEnd, QDate(2026, 1, 31));
        QVERIFY(mapped.rule.isNull());               // Exchange uses the last day of short months
        QCOMPARE(mapped.raw, QStringLiteral("FREQ=MONTHLY;BYMONTHDAY=28,29,30,31;BYSETPOS=-1;UNTIL=20261231"));
        monthEnd.dayOfMonth = 15;
        mapped = patternToRule(monthEnd, QDate(2026, 1, 15));
        QCOMPARE(mapped.rule.toMap().value(QStringLiteral("until")).toString(), QStringLiteral("2026-12-31"));

        RecurrencePattern leap;
        leap.type = QStringLiteral("absoluteYearly");
        leap.dayOfMonth = 29;
        leap.month = 2;
        QVERIFY(patternToRule(leap, QDate(2028, 2, 29)).rule.isNull());
        RecurrencePattern relative;
        relative.type = QStringLiteral("relativeYearly");
        relative.days = dayNumbers(QStringLiteral("Weekday"));
        relative.index = QStringLiteral("last");
        relative.month = 5;
        QCOMPARE(patternToRule(relative, QDate(2026, 5, 29)).raw, QStringLiteral("FREQ=YEARLY;BYMONTH=5;BYDAY=MO,TU,WE,TH,FR;BYSETPOS=-1"));
        RecurrencePattern regeneration;
        regeneration.type = QStringLiteral("DailyRegeneration");
        QCOMPARE(patternToRule(regeneration, QDate(2026, 5, 29)).raw, QStringLiteral("X-UNSUPPORTED-PATTERN=DailyRegeneration"));

        RecurrencePattern out;
        QString error;
        QVERIFY(ruleToPattern({{QStringLiteral("frequency"), QStringLiteral("weekly")}, {QStringLiteral("interval"), 1}},
                              QDate(2026, 3, 7), false, &out, &error));
        QCOMPARE(out.days, QList<int>{6});           // the start's weekday
        QVERIFY(!ruleToPattern({{QStringLiteral("frequency"), QStringLiteral("monthly")}}, QDate(2026, 1, 31), true, &out, &error));
        QVERIFY(error.contains(QStringLiteral("last day")));
        QVERIFY(!ruleToPattern({{QStringLiteral("frequency"), QStringLiteral("yearly")}, {QStringLiteral("interval"), 2}},
                               QDate(2026, 5, 1), false, &out, &error));
        QVERIFY(ruleToPattern({{QStringLiteral("frequency"), QStringLiteral("yearly")}, {QStringLiteral("interval"), 2},
                               {QStringLiteral("endMode"), QStringLiteral("count")}, {QStringLiteral("count"), 3}},
                              QDate(2026, 5, 1), true, &out, &error));
        QCOMPARE(out.range, QStringLiteral("numbered"));
        QCOMPARE(out.month, 5);
    }

    void overridesAndHelpers()
    {
        const QVariantMap master{{QStringLiteral("title"), QStringLiteral("Standup")}, {QStringLiteral("location"), QStringLiteral("Room 1")},
            {QStringLiteral("startLocal"), QStringLiteral("2026-03-07 09:00")}, {QStringLiteral("endLocal"), QStringLiteral("2026-03-07 09:30")},
            {QStringLiteral("zone"), QStringLiteral("America/Denver")}};
        const QDateTime original = wallTime(QStringLiteral("2026-03-14 09:00"), QStringLiteral("America/Denver"));
        // An occurrence that matches the series is not an override.
        QVERIFY(overrideEntry(master, QStringLiteral("2026-03-14T09:00:00"), original, original.addSecs(1800),
                              QStringLiteral("Standup"), QStringLiteral("Room 1"), QString()).isEmpty());
        const QVariantMap moved = overrideEntry(master, QStringLiteral("2026-03-14T09:00:00"), original.addSecs(3600),
                                                original.addSecs(5400), QStringLiteral("Standup"), QStringLiteral("Room 2"), QString());
        QCOMPARE(moved.value(QStringLiteral("startLocal")).toString(), QStringLiteral("2026-03-14 10:00"));
        QCOMPARE(moved.value(QStringLiteral("location")).toString(), QStringLiteral("Room 2"));
        QVERIFY(!moved.contains(QStringLiteral("title")));

        QVariantMap record = master;
        record.insert(QStringLiteral("recurrenceRule"), QVariantMap{{QStringLiteral("frequency"), QStringLiteral("weekly")}});
        record.insert(QStringLiteral("exceptions"), QVariantList{QStringLiteral("2026-03-21T09:00:00")});
        QVariantMap server = record;
        server.insert(QStringLiteral("exceptions"), QVariantList{QStringLiteral("x/2026-03-28T09:00:00")});
        const OccurrenceChanges changes = occurrenceChanges(server, record);
        QCOMPARE(changes.excluded, QStringList{QStringLiteral("2026-03-21T09:00:00")});
        QCOMPARE(changes.reinstated, QStringList{QStringLiteral("2026-03-28T09:00:00")});
        QVERIFY(occurrencesNeedWrite(digestSubset(server), record, seriesChanges(digestSubset(server), record)));

        const QDateTime now(QDate(2026, 9, 25), QTime(12, 0), QTimeZone::UTC);
        QCOMPARE(parseRetryAfter("120", now), 120);
        QCOMPARE(parseRetryAfter("Fri, 25 Sep 2026 12:00:30 GMT", now), 30);
        QCOMPARE(parseRetryAfter("soon", now), -1);
        QCOMPARE(normalizedText(QStringLiteral("a\r\nb\r\n\r\n")), QStringLiteral("a\nb"));
        QCOMPARE(localIdFor(QStringLiteral("ews"), QStringLiteral("work"), QStringLiteral("AAMk=")).size(), 9 + 32);
    }

    void xmlTreeRefusesDtdsAndDepth()
    {
        XmlNode root;
        QString error;
        QVERIFY(!parseXml("<!DOCTYPE x [<!ENTITY a \"aaaa\">]><x>&a;</x>", &root, &error));
        QByteArray deep;
        for (int i = 0; i < 80; ++i) deep += "<a>";
        for (int i = 0; i < 80; ++i) deep += "</a>";
        QVERIFY(!parseXml(deep, &root, &error));
        QVERIFY(parseXml("<t:x xmlns:t=\"urn:t\" a=\"1\"><t:y>v &amp; w</t:y></t:x>", &root, &error));
        QCOMPARE(root.ns, QStringLiteral("urn:t"));
        QCOMPARE(root.childText(u"y"), QStringLiteral("v & w"));
        QCOMPARE(root.attribute(QStringLiteral("a")), QStringLiteral("1"));
    }
};

QTEST_MAIN(MappingTest)
#include "tst_mail_exchange_mapping.moc"
