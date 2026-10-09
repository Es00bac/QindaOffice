// SPDX-License-Identifier: GPL-3.0-or-later
// Deposit and filing due dates (payroll_calendar.h) against Publication 15
// (2026) section 11 — its own examples first — with the 2026 legal
// holidays from the data file.
#include "payroll_calendar.h"
#include "payroll_tables.h"

#include <QTest>

using namespace QindaOffice::Books;
using namespace QindaOffice::Books::Payroll;

class TstPayrollCalendar : public QObject {
    Q_OBJECT
    PayrollTables t;
    BusinessCalendar federal;
    BusinessCalendar colorado;

private slots:
    void initTestCase()
    {
        QString error;
        const std::optional<PayrollTables> tables = PayrollTables::forYear(2026, &error);
        QVERIFY2(tables.has_value(), qPrintable(error));
        t = *tables;
        federal = BusinessCalendar(t.federalHolidays());
        colorado = BusinessCalendar(t.coloradoHolidays());
    }

    // Pub 15 (2026) p. 34: "Green, Inc.'s tax liability for the May 29,
    // 2026 (Friday), payday must be deposited by June 3, 2026 (Wednesday)."
    void publicationSemiweeklyExample()
    {
        const DuePeriod p = federalDepositPeriod(QDate(2026, 5, 29), DepositSchedule::Semiweekly, federal);
        QCOMPARE(p.start, QDate(2026, 5, 27));
        QCOMPARE(p.end, QDate(2026, 5, 29));
        QCOMPARE(p.due, QDate(2026, 6, 3));
    }

    // Pub 15: a Friday payday whose following Monday is a legal holiday is
    // due Thursday — Friday Jan 16, 2026; Monday Jan 19 is Martin Luther
    // King Jr.'s Birthday; due Thursday Jan 22.
    void semiweeklyHolidayAddsADay()
    {
        QCOMPARE(federalDepositPeriod(QDate(2026, 1, 16), DepositSchedule::Semiweekly, federal).due, QDate(2026, 1, 22));
        // Saturday–Tuesday paydays are due the following Friday: Tuesday
        // March 10, 2026 → Friday March 13.
        const DuePeriod p = federalDepositPeriod(QDate(2026, 3, 10), DepositSchedule::Semiweekly, federal);
        QCOMPARE(p.start, QDate(2026, 3, 7));
        QCOMPARE(p.due, QDate(2026, 3, 13));
        // Tuesday Dec 29, 2026: Wed 30, Thu 31, Fri Jan 1 (New Year's Day)
        // → the third business day is Monday Jan 4, 2027.
        QCOMPARE(federalDepositPeriod(QDate(2026, 12, 29), DepositSchedule::Semiweekly, federal).due, QDate(2027, 1, 4));
    }

    // A semiweekly period never crosses a quarter end: Wednesday Sep 30,
    // 2026 starts a Wednesday–Friday period; the Q3 part is Sep 30 alone,
    // the Q4 part Oct 1–2; both due Wednesday Oct 7.
    void semiweeklySplitsAtTheQuarter()
    {
        const DuePeriod q3 = federalDepositPeriod(QDate(2026, 9, 30), DepositSchedule::Semiweekly, federal);
        QCOMPARE(q3.start, QDate(2026, 9, 30));
        QCOMPARE(q3.end, QDate(2026, 9, 30));
        QCOMPARE(q3.due, QDate(2026, 10, 7));
        const DuePeriod q4 = federalDepositPeriod(QDate(2026, 10, 2), DepositSchedule::Semiweekly, federal);
        QCOMPARE(q4.start, QDate(2026, 10, 1));
        QCOMPARE(q4.due, QDate(2026, 10, 7));
    }

    // Monthly: the 15th of the next month, moved past weekends and holidays.
    void monthly()
    {
        QCOMPARE(federalDepositPeriod(QDate(2026, 9, 15), DepositSchedule::Monthly, federal).due, QDate(2026, 10, 15));
        // Feb 15, 2026 is a Sunday and Feb 16 Washington's Birthday → Tue Feb 17.
        QCOMPARE(federalDepositPeriod(QDate(2026, 1, 30), DepositSchedule::Monthly, federal).due, QDate(2026, 2, 17));
        // Mar 15, 2026 is a Sunday → Monday Mar 16.
        const DuePeriod feb = federalDepositPeriod(QDate(2026, 2, 27), DepositSchedule::Monthly, federal);
        QCOMPARE(feb.statutory, QDate(2026, 3, 15));
        QCOMPARE(feb.due, QDate(2026, 3, 16));
        QCOMPARE(federalDepositPeriod(QDate(2026, 12, 31), DepositSchedule::Monthly, federal).due, QDate(2027, 1, 15));
    }

    // $100,000 next-day rule: Thursday July 2, 2026 → Friday July 3 is
    // Independence Day (observed) → Monday July 6.
    void nextDay()
    {
        QCOMPARE(nextDayDepositDue(QDate(2026, 7, 2), federal), QDate(2026, 7, 6));
        QCOMPARE(nextDayDepositDue(QDate(2026, 7, 7), federal), QDate(2026, 7, 8));
    }

    // Form 941: last day of the month after the quarter. Oct 31, 2026 is
    // a Saturday → Monday Nov 2; Jan 31, 2027 a Sunday → Monday Feb 1.
    void returnsDue()
    {
        QCOMPARE(quarterlyReturnPeriod(2026, 1, federal).due, QDate(2026, 4, 30));
        QCOMPARE(quarterlyReturnPeriod(2026, 2, federal).due, QDate(2026, 7, 31));
        QCOMPARE(quarterlyReturnPeriod(2026, 3, federal).due, QDate(2026, 11, 2));
        QCOMPARE(quarterlyReturnPeriod(2026, 4, federal).due, QDate(2027, 2, 1));
        QCOMPARE(annualReturnPeriod(2026, federal).due, QDate(2027, 2, 1));
        QCOMPARE(quarterLabel(2026, 3), QStringLiteral("Q3 2026"));
    }

    void coloradoWithholdingPeriods()
    {
        // Monthly: the 15th of the next month.
        QCOMPARE(coloradoWithholdingPeriod(QDate(2026, 9, 30), ColoradoFrequency::Monthly, colorado).due,
                 QDate(2026, 10, 15));
        // Quarterly: the last day of the month after the quarter (Oct 31,
        // 2026 is a Saturday → Monday Nov 2).
        QCOMPARE(coloradoWithholdingPeriod(QDate(2026, 8, 14), ColoradoFrequency::Quarterly, colorado).due,
                 QDate(2026, 11, 2));
        // Weekly, the guide's own example: wages paid Tuesday the 27th fall
        // in the period ending Friday the 30th, due Wednesday the 4th —
        // January 2026 has that calendar (Jan 24 a Saturday).
        const DuePeriod week = coloradoWithholdingPeriod(QDate(2026, 1, 27), ColoradoFrequency::Weekly, colorado);
        QCOMPARE(week.start, QDate(2026, 1, 24));
        QCOMPARE(week.end, QDate(2026, 1, 30));
        QCOMPARE(week.due, QDate(2026, 2, 4));
        // The year's last weekly period ends December 31 (a Thursday in
        // 2026); the next one starts January 1.
        const DuePeriod last = coloradoWithholdingPeriod(QDate(2026, 12, 29), ColoradoFrequency::Weekly, colorado);
        QCOMPARE(last.start, QDate(2026, 12, 26));
        QCOMPARE(last.end, QDate(2026, 12, 31));
        QCOMPARE(coloradoWithholdingPeriod(QDate(2027, 1, 1), ColoradoFrequency::Weekly, colorado).start,
                 QDate(2027, 1, 1));
        // Frequency by the annual liability.
        QCOMPARE(coloradoFrequencyFor(parseUsd(QStringLiteral("6999.99")), t.colorado()), ColoradoFrequency::Quarterly);
        QCOMPARE(coloradoFrequencyFor(t.colorado().withholdingMonthlyFrom, t.colorado()), ColoradoFrequency::Monthly);
        // Rule 39-22-604-1(7): monthly "at least $7,000, but not more than
        // $50,000", weekly "more than $50,000" (the January 2026 guide says
        // "$50,000 or more"; the rule, newer and binding, decides $50,000).
        QCOMPARE(coloradoFrequencyFor(t.colorado().withholdingWeeklyAbove, t.colorado()), ColoradoFrequency::Monthly);
        QCOMPARE(coloradoFrequencyFor(t.colorado().withholdingWeeklyAbove + parseUsd(QStringLiteral("0.01")), t.colorado()),
                 ColoradoFrequency::Weekly);
    }
};

QTEST_GUILESS_MAIN(TstPayrollCalendar)
#include "tst_payroll_calendar.moc"
