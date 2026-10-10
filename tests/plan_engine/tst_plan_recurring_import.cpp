// SPDX-License-Identifier: GPL-3.0-or-later
// M83 (D-092), worked by hand on the standard calendar (Monday 2 March
// 2026): recurring tasks and their patterns, recurring calendar exceptions
// and work weeks made and changed here (Project's XML elements stay the
// truth), and the Excel/CSV task import with its column mapping.

#include "calendar_edit.h"
#include "mspdi.h"
#include "plan_edit.h"
#include "plan_file.h"
#include "plan_fixture.h"
#include "plan_import.h"
#include "recurring_tasks.h"
#include "work_calendar.h"

using namespace PlanTest;

namespace {

const QLocale kC = QLocale::c();

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

TaskRecurrence weekly(int days, int every, int count)
{
    TaskRecurrence r;
    r.pattern.kind = RecurringException::Kind::Weekly;
    r.pattern.daysOfWeek = days;
    r.pattern.every = every;
    r.pattern.from = QDate(2026, 3, 2);
    r.count = count;
    r.duration = kHour;
    r.startMinute = 9 * 60;
    return r;
}

QStringList names(const Project &p)
{
    QStringList out;
    for (const Task &t : p.tasks) {
        out.append(t.name);
    }
    return out;
}

} // namespace

class PlanRecurringImportTest final : public QObject {
    Q_OBJECT
private slots:
    void recurringTasks()
    {
        Builder b;
        b.task(s("Kickoff"), kDay);
        Project &p = b.run();
        QString error;
        const int at0 = insertRecurringTask(p, 1, s("Stand-up"), weekly(0b0000101, 1, 4), &error); // Mon, Wed
        QVERIFY2(at0 == 1, qPrintable(error));
        schedule(p);
        QCOMPARE(names(p), QStringList({s("Kickoff"), s("Stand-up"), s("Stand-up 1"), s("Stand-up 2"), s("Stand-up 3"),
                                        s("Stand-up 4")}));
        const Task &summary = p.tasks.at(1);
        QVERIFY(summary.summary && summary.recurring && summary.recurrence);
        QCOMPARE_AT(p.tasks.at(2).start, at(2, 9));
        QCOMPARE_AT(p.tasks.at(2).finish, at(2, 10));
        QCOMPARE_AT(p.tasks.at(3).start, at(4, 9));
        QCOMPARE_AT(p.tasks.at(5).start, at(11, 9));
        QCOMPARE_AT(summary.finish, at(11, 10));
        QVERIFY(taskFieldText(p, 1, s("indicators"), kC).contains(s("recurring")));
        QVERIFY(recurrenceText(*summary.recurrence, kC).startsWith(s("Every week on Monday, Wednesday")));
        // Every two weeks, three times: the 7-day blocks from the first day.
        QVERIFY(setRecurrence(p, 1, weekly(0b0000101, 2, 3)).ok);
        schedule(p);
        QCOMPARE(p.tasks.size(), 5);
        QCOMPARE_AT(p.tasks.at(4).start, at(16, 9));
        // Every second working day; the last Friday of the month.
        TaskRecurrence work;
        work.pattern.kind = RecurringException::Kind::EveryDays;
        work.pattern.every = 2;
        work.pattern.from = QDate(2026, 3, 6);
        work.workdays = true;
        work.count = 3;
        QCOMPARE(recurrenceDates(p, work), QList<QDate>({QDate(2026, 3, 6), QDate(2026, 3, 10), QDate(2026, 3, 12)}));
        TaskRecurrence last;
        last.pattern.kind = RecurringException::Kind::MonthlyByPosition;
        last.pattern.position = 4;
        last.pattern.weekday = 5;
        last.pattern.from = QDate(2026, 3, 1);
        last.count = 2;
        QCOMPARE(recurrenceDates(p, last), QList<QDate>({QDate(2026, 3, 27), QDate(2026, 4, 24)}));
        // Kept in the .qplan; Project's XML carries the flag and the occurrences.
        Project back;
        QVERIFY(readPlanJson(writePlanJson(p), &back).ok);
        QCOMPARE(*back.tasks.at(1).recurrence, *p.tasks.at(1).recurrence);
        Project xml;
        QVERIFY(readMspdi(writeMspdi(p), &xml).ok);
        QVERIFY(xml.tasks.at(1).recurring && !xml.tasks.at(1).recurrence);
        QCOMPARE(xml.tasks.size(), p.tasks.size());
        // An occurrence with progress keeps the pattern as it is.
        setPercentComplete(p, {2}, 50);
        QVERIFY(!setRecurrence(p, 1, weekly(0b0000001, 1, 2)).ok);
        QVERIFY(insertRecurringTask(p, 0, s("Never"), weekly(0b0000001, 1, 0), &error) < 0); // no last day
    }

    void recurringExceptionsAndWorkWeeks()
    {
        Builder b;
        const int t = b.task(s("Build"), 5 * kDay);
        Project &p = b.run();
        RecurringException mondays;
        mondays.kind = RecurringException::Kind::Weekly;
        mondays.name = s("Site closed");
        mondays.daysOfWeek = 0b0000001;
        mondays.from = QDate(2026, 3, 2);
        QVERIFY(setRecurringException(p, 1, -1, mondays, 3).ok); // three Mondays
        schedule(p);
        const Calendar &cal = p.calendars.at(p.calendarIndex(1));
        QCOMPARE(cal.recurring.size(), 1);
        QCOMPARE(cal.recurring.first().to, QDate(2026, 3, 16));
        QCOMPARE_AT(p.tasks.at(t).start, at(3));
        QCOMPARE_AT(p.tasks.at(t).finish, at(10, 17)); // Tue–Fri, then Tue 10
        RecurringException christmas;
        christmas.kind = RecurringException::Kind::YearlyByDay;
        christmas.name = s("Christmas");
        christmas.month = 12;
        christmas.day = 25;
        christmas.from = QDate(2026, 1, 1);
        christmas.to = QDate(2030, 12, 31);
        QVERIFY(setRecurringException(p, 1, -1, christmas).ok);
        QCOMPARE(WorkCalendar(p, 1).minutesOn(QDate(2026, 12, 25)), 0);
        QVERIFY(recurringExceptionXml(christmas).contains(s("<Type>2</Type><Month>11</Month><MonthDay>25</MonthDay>")));
        mondays.daysOfWeek = 0b0010000; // Fridays instead
        QVERIFY(setRecurringException(p, 1, 0, mondays, 2).ok);
        schedule(p);
        QCOMPARE_AT(p.tasks.at(t).finish, at(9, 17)); // Mon–Thu, then Mon 9
        QVERIFY(!setRecurringException(p, 1, 9, mondays).ok);
        RecurringException none = mondays;
        none.daysOfWeek = 0;
        QVERIFY(!setRecurringException(p, 1, -1, none).ok);
        QVERIFY(removeRecurringException(p, 1, 0).ok);
        QCOMPARE(p.calendars.at(p.calendarIndex(1)).recurring.size(), 1); // Christmas
        // A summer work week: Fridays off in March.
        WorkWeek summer;
        summer.name = s("Summer hours");
        summer.from = QDate(2026, 3, 1);
        summer.to = QDate(2026, 3, 31);
        summer.week[4] = DayHours{};
        QVERIFY(setWorkWeek(p, 1, -1, summer).ok);
        schedule(p);
        QCOMPARE_AT(p.tasks.at(t).finish, at(9, 17));
        QVERIFY(!setWorkWeek(p, 1, 3, summer).ok);
        // Both travel as Project's elements, in the .qplan and in its XML.
        Project json;
        QVERIFY(readPlanJson(writePlanJson(p), &json).ok);
        QCOMPARE(json.calendars.at(0).recurring, p.calendars.at(0).recurring);
        QCOMPARE(json.calendars.at(0).workWeeks, p.calendars.at(0).workWeeks);
        const QByteArray out = writeMspdi(p);
        QVERIFY(out.contains("<Name>Summer hours</Name>"));
        Project xml;
        QVERIFY(readMspdi(out, &xml).ok);
        QCOMPARE(xml.calendars.at(0).recurring, p.calendars.at(0).recurring);
        QCOMPARE(xml.calendars.at(0).workWeeks, p.calendars.at(0).workWeeks);
        QCOMPARE(xml.tasks.at(t).finish, p.tasks.at(t).finish);
        QVERIFY(removeWorkWeek(p, 1, 0).ok);
        QVERIFY(p.calendars.at(0).workWeeks.isEmpty());
    }

    void csvParsing()
    {
        const ImportTable comma = parseCsvTable(QByteArray("\xEF\xBB\xBFName,Notes\r\n\"Plan, then do\",\"two\nlines\"\r\nSay \"\"hi\"\",x\n"));
        QCOMPARE(comma.rows.size(), 3);
        QCOMPARE(comma.rows.at(1), QStringList({s("Plan, then do"), s("two\nlines")}));
        QCOMPARE(comma.rows.at(2).first(), s("Say \"\"hi\"\""));
        const ImportTable semi = parseCsvTable(QByteArray("Name;Duration\nA;3d\n"));
        QCOMPARE(semi.rows.at(1), QStringList({s("A"), s("3d")}));
        QCOMPARE(guessImportMapping({s("ID"), s("Task Name"), s("% Complete"), s("Resource Names"), s("Text1"),
                                     s("Mystery"), s("Name")}),
                 QStringList({s("id"), s("name"), s("percent"), s("resources"), s("Text1"), QString(), QString()}));
    }

    void importTasks()
    {
        Builder b;
        b.task(s("Already here"), kDay);
        Project &p = b.run();
        const ImportTable table = parseCsvTable(QByteArray(
            "ID,Task Name,Outline Level,Duration,Start,Predecessors,Resource Names,Text1\n"
            "10,Launch,1,,,,,\n"
            "11,Write copy,2,3d,2026-03-02,,Ana,North\n"
            "12,Design,2,2 days,,11,Ben[50%],South\n"
            ",,,,,,,\n"
            "13,Review,2,1d,,12FS+1d,,\n"
            "14,Done,2,0,,13; 99,,\n"
            "15,Broken,1,abc,,,,\n"));
        const QStringList mapping = guessImportMapping(table.rows.first());
        ImportReport report;
        const EditResult r = QindaOffice::Plan::importTasks(p, table, mapping, true, &report, kC);
        QVERIFY2(r.ok, qPrintable(r.error));
        schedule(p);
        QCOMPARE(report.tasks, 6);
        QCOMPARE(names(p), QStringList({s("Already here"), s("Launch"), s("Write copy"), s("Design"), s("Review"),
                                        s("Done"), s("Broken")}));
        QVERIFY(p.tasks.at(1).summary);
        QCOMPARE(p.tasks.at(2).outlineLevel, 2);
        QCOMPARE_AT(p.tasks.at(2).finish, at(4, 17));
        QCOMPARE_AT(p.tasks.at(3).start, at(5));
        QCOMPARE_AT(p.tasks.at(4).start, at(10)); // FS + 1 day of lag
        QCOMPARE(taskFieldText(p, 4, s("predecessors"), kC), s("4FS+1 day"));
        QCOMPARE(taskFieldText(p, 3, s("resources"), kC), s("Ben[50%]"));
        QCOMPARE(p.tasks.at(2).custom.value(s("Text1")), s("North"));
        QVERIFY(p.tasks.at(5).isMilestone());
        QCOMPARE(p.tasks.at(6).outlineLevel, 1);
        const QString warnings = report.warnings.join(QLatin1Char('\n'));
        QVERIFY(warnings.contains(s("99")));     // no such source task
        QVERIFY(warnings.contains(s("abc")));    // not a duration
        QVERIFY(!warnings.contains(s("Row 4"))); // a blank row is just skipped
        QVERIFY(p.resourceIndexByName(s("Ben")) >= 0);
        ImportReport none;
        QVERIFY(!QindaOffice::Plan::importTasks(p, table, QStringList(8, QString()), true, &none, kC).ok);
    }
};

QTEST_GUILESS_MAIN(PlanRecurringImportTest)

#include "tst_plan_recurring_import.moc"
