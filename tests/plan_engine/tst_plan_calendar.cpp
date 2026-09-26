// SPDX-License-Identifier: GPL-3.0-or-later
// Working-time arithmetic (work_calendar.h) and the text forms of dates,
// durations and money (plan_time.h), against hand-computed answers on the
// standard calendar in March 2026 (Monday the 2nd).

#include "plan_fixture.h"
#include "work_calendar.h"

using namespace PlanTest;

class PlanCalendarTest final : public QObject {
    Q_OBJECT
private slots:
    void standardWeekArithmetic()
    {
        const WorkCalendar c;
        QCOMPARE_AT(c.add(at(2), kDay), at(2, 17));               // a day's work ends the same day
        QCOMPARE_AT(c.add(at(2), kDay + 1), at(3, 8, 1));
        QCOMPARE_AT(c.add(at(2, 10), 4 * kHour), at(2, 15));      // across the lunch break
        QCOMPARE_AT(c.add(at(6, 17), kHour), at(9, 9));           // Friday evening → Monday
        QCOMPARE_AT(c.add(at(2), 5 * kDay), at(6, 17));
        QCOMPARE_AT(c.subtract(at(2, 17), kDay), at(2));
        QCOMPARE_AT(c.subtract(at(9, 9), 2 * kHour), at(6, 16));  // Monday 09:00 back two hours
        QCOMPARE_AT(c.add(at(4, 17), -kDay), at(4));              // negative = subtract
        QCOMPARE_AT(c.snapForward(at(7, 10)), at(9));             // Saturday → Monday
        QCOMPARE_AT(c.snapForward(at(2, 12)), at(2, 13));         // lunch → afternoon
        QCOMPARE_AT(c.snapForward(at(2, 11)), at(2, 11));
        QCOMPARE_AT(c.snapBackward(at(9)), at(6, 17));            // Monday 08:00 → Friday 17:00
        QCOMPARE_AT(c.snapBackward(at(2, 13)), at(2, 12));
        QCOMPARE(c.workBetween(at(6, 12), at(9, 12)), kDay);      // Fri 13–17 + Mon 8–12
        QCOMPARE(c.workBetween(at(9, 12), at(6, 12)), -kDay);
        QCOMPARE(c.workBetween(at(2), at(2)), 0);
        QCOMPARE(c.workBetween(at(2), at(9)), 5 * kDay);
        QCOMPARE(c.minutesOn(QDate(2026, 3, 7)), 0);
        QVERIFY(!c.degenerate());
    }

    void exceptionsAndResourceCalendars()
    {
        Project p = newProject(QDate(2026, 3, 2));
        Calendar &standard = p.calendars[0];
        standard.exceptions.append({QDate(2026, 3, 4), QDate(2026, 3, 4), QStringLiteral("Holiday"), {}});
        standard.exceptions.append({QDate(2026, 3, 7), QDate(2026, 3, 7), QStringLiteral("Workday"), {{8 * 60, 12 * 60}}});
        const WorkCalendar c(p, 1);
        QCOMPARE_AT(c.add(at(3), 2 * kDay), at(5, 17));              // Wednesday is a holiday
        QCOMPARE_AT(c.add(at(6, 13), kDay), at(7, 12));              // the working Saturday morning
        QCOMPARE(c.minutesOn(QDate(2026, 3, 4)), 0);
        QCOMPARE(c.minutesOn(QDate(2026, 3, 7)), 240);

        Calendar vacation;
        vacation.uid = 2;
        vacation.name = QStringLiteral("Ana");
        vacation.baseUid = 1;
        vacation.exceptions.append({QDate(2026, 3, 9), QDate(2026, 3, 13), QStringLiteral("Vacation"), {}});
        vacation.week[6] = DayHours{{9 * 60, 13 * 60}}; // works Sunday mornings
        p.calendars.append(vacation);
        const WorkCalendar ana(p, 2);
        QCOMPARE(ana.minutesOn(QDate(2026, 3, 4)), 0);                 // the base's holiday applies
        QCOMPARE(ana.minutesOn(QDate(2026, 3, 10)), 0);                // her vacation
        QCOMPARE(ana.minutesOn(QDate(2026, 3, 8)), 240);               // her Sunday
        QCOMPARE(ana.minutesOn(QDate(2026, 3, 16)), kDay);             // the base's week
        QCOMPARE_AT(ana.add(at(6, 13), kDay), at(7, 12));              // Fri pm + the base's working Saturday
        QCOMPARE_AT(ana.add(at(6, 13), kDay + 60), at(8, 10));         // … then her Sunday 09:00–10:00
        QCOMPARE_AT(ana.snapForward(at(9)), at(15, 9));                // after the vacation: Sunday 15th

        // Unknown uid → the project calendar; a calendar with no working
        // time at all → the standard week, flagged.
        QCOMPARE(WorkCalendar(p, 99).minutesOn(QDate(2026, 3, 4)), 0);
        Calendar none;
        none.uid = 3;
        for (auto &d : none.week) {
            d = DayHours{};
        }
        p.calendars.append(none);
        const WorkCalendar degenerate(p, 3);
        QVERIFY(degenerate.degenerate());
        QCOMPARE(degenerate.minutesOn(QDate(2026, 3, 2)), kDay);
    }

    void durationsParseAndFormat()
    {
        const DurationFormat f;
        const QLocale c = QLocale::c();
        QCOMPARE(parseDuration(QStringLiteral("5 days"), f)->minutes, 5 * kDay);
        QCOMPARE(parseDuration(QStringLiteral("5"), f)->minutes, 5 * kDay);
        QCOMPARE(parseDuration(QStringLiteral("1.5d"), f)->minutes, 720);
        QCOMPARE(parseDuration(QStringLiteral("4 hrs"), f)->minutes, 240);
        QCOMPARE(parseDuration(QStringLiteral("4h"), f)->unit, DurationUnit::Hours);
        QCOMPARE(parseDuration(QStringLiteral("2w"), f)->minutes, 2400 * 2);
        QCOMPARE(parseDuration(QStringLiteral("30 mins"), f)->minutes, 30);
        QCOMPARE(parseDuration(QStringLiteral("1 mon"), f)->minutes, 20 * kDay);
        QVERIFY(parseDuration(QStringLiteral("3?"), f)->estimated);
        QVERIFY(!parseDuration(QStringLiteral("-1d"), f).has_value());
        QCOMPARE(parseDuration(QStringLiteral("-1d"), f, true)->minutes, -kDay);
        QVERIFY(!parseDuration(QStringLiteral("soon"), f).has_value());
        QVERIFY(!parseDuration(QStringLiteral("5 parsecs"), f).has_value());
        QCOMPARE(formatDuration(5 * kDay, DurationUnit::Days, f, false, c), QStringLiteral("5 days"));
        QCOMPARE(formatDuration(kDay, DurationUnit::Days, f, false, c), QStringLiteral("1 day"));
        QCOMPARE(formatDuration(0, DurationUnit::Days, f, false, c), QStringLiteral("0 days"));
        QCOMPARE(formatDuration(720, DurationUnit::Days, f, true, c), QStringLiteral("1.5 days?"));
        QCOMPARE(formatDuration(90, DurationUnit::Hours, f, false, c), QStringLiteral("1.5 hrs"));
        QCOMPARE(formatSpan(-kDay, f, c), QStringLiteral("-1 day"));
        QCOMPARE(formatSpan(120, f, c), QStringLiteral("2 hrs"));
        QCOMPARE(formatWork(960, c), QStringLiteral("16 hrs"));
        Link lag;
        lag.lag = 2 * kDay;
        QCOMPARE(formatLag(lag, f, c), QStringLiteral("+2 days"));
        lag.lagPercent = -25;
        QCOMPARE(formatLag(lag, f, c), QStringLiteral("-25%"));
    }

    void datesParseAndFormat()
    {
        const QLocale c = QLocale::c();
        QCOMPARE(formatDate(at(2), c), QStringLiteral("Mon 2 Mar 26"));
        QCOMPARE(formatDateTime(at(2, 13, 30), c), QStringLiteral("Mon 2 Mar 26 13:30"));
        QCOMPARE(toIso(at(2, 17)), QStringLiteral("2026-03-02T17:00"));
        QCOMPARE(fromIso(QStringLiteral("2026-03-02"), 480), at(2));
        QCOMPARE(fromIso(QStringLiteral("2026-03-02T17:00:00")), at(2, 17));
        QCOMPARE(fromIso(QStringLiteral("2026-03-02 13:15")), at(2, 13, 15));
        QCOMPARE(fromIso(QStringLiteral("nope")), kNoDate);
        QCOMPARE(*parseDate(QStringLiteral("Mon 2 Mar 26"), 480, c), at(2));
        QCOMPARE(*parseDate(QStringLiteral("2 Mar 2026 17:00"), 480, c), at(2, 17));
        QCOMPARE(*parseDate(QStringLiteral("3/9/2026"), 480, c), at(9));
        QCOMPARE(*parseDate(QStringLiteral("9.3.2026"), 1020, c), at(9, 17));
        QCOMPARE(*parseDate(QStringLiteral("2026-03-09"), 480, c), at(9));
        QVERIFY(!parseDate(QStringLiteral("the ides of March"), 480, c).has_value());
        QVERIFY(!parseDate(QStringLiteral("2 Mar 2026 25:00"), 480, c).has_value());
        QCOMPARE(dateOf(at(2, 23, 59)), QDate(2026, 3, 2));
        QCOMPARE(minuteOfDay(at(2, 13, 30)), 13 * 60 + 30);
        QCOMPARE(dateOf(toMinutes(QDate(1969, 12, 31), 600)), QDate(1969, 12, 31));
    }

    void money()
    {
        const QLocale c = QLocale::c();
        QCOMPARE(formatMoney(123450, QStringLiteral("USD"), c), QStringLiteral("$1,234.50"));
        QCOMPARE(formatMoney(-5, QStringLiteral("USD"), c), QStringLiteral("-$0.05"));
        QCOMPARE(formatMoney(1500, QStringLiteral("JPY"), c), QStringLiteral("JPY 1,500"));
        QCOMPARE(*parseMoney(QStringLiteral("$1,234.50"), QStringLiteral("USD"), c), 123450);
        QCOMPARE(*parseMoney(QStringLiteral("75"), QStringLiteral("USD"), c), 7500);
        QCOMPARE(*parseMoney(QStringLiteral("-0.5"), QStringLiteral("USD"), c), -50);
        QVERIFY(!parseMoney(QStringLiteral("1.234"), QStringLiteral("USD"), c).has_value());
        QVERIFY(!parseMoney(QStringLiteral("ten"), QStringLiteral("USD"), c).has_value());
        QCOMPARE(divRound(5, 2), 3);
        QCOMPARE(divRound(-5, 2), -3);
    }
};

QTEST_GUILESS_MAIN(PlanCalendarTest)

#include "tst_plan_calendar.moc"
