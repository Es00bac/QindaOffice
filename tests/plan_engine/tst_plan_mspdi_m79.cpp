// SPDX-License-Identifier: GPL-3.0-or-later
// M79 (D-088): what QindaPlan now models in Microsoft Project XML round-trips
// as Project wrote it — baselines 0–10 of tasks, assignments and the project
// summary task (the file's element replayed while unchanged, ours once the
// baseline is set or cleared here), leveling delays in elapsed and working
// time with the pre-leveled dates, the critical slack limit and multiple
// critical paths, and Project's recurring calendar exceptions and work
// weeks, now scheduled. Project-shaped fixture:
// data/project_m79_sample.xml (its header has the expected schedule).
//
// AGENT-NOTE (schema): no MSPDI XSD is available through Portage, so the
// shape is checked structurally — every Task/Assignment child in Project's
// own order (mspdi_order.cpp), every Baseline's children in the order of
// the schema's TaskBaseline / AssignmentBaseline sequences, and the value
// spelling of each element QindaPlan writes (xs:dateTime, xs:duration,
// integers, decimals).

#include "baselines.h"
#include "leveling.h"
#include "mspdi.h"
#include "mspdi_keep.h"
#include "mspdi_p.h"
#include "plan_file.h"
#include "plan_fixture.h"
#include "work_calendar.h"

#include <QFile>
#include <QRegularExpression>
#include <QXmlStreamReader>

using namespace PlanTest;

namespace {

QByteArray sample()
{
    QFile f(QStringLiteral(SOURCE_DIR "/data/project_m79_sample.xml"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

Project readSample(QStringList *warnings = nullptr)
{
    Project p;
    const MspdiResult r = readMspdi(sample(), &p);
    if (warnings != nullptr) {
        *warnings = r.warnings;
    }
    return p;
}

// Whitespace between elements removed, so a replayed element (re-indented
// by the writer) compares with the file's text.
QByteArray tight(QByteArray xml)
{
    static const QRegularExpression gaps(QStringLiteral(">\\s+<"));
    return QString::fromUtf8(xml).replace(gaps, QStringLiteral("><")).toUtf8();
}

// The <Task> or <Assignment> element of `uid` in `xml`, tight.
QByteArray element(const QByteArray &xml, const char *kind, int uid)
{
    const QByteArray t = tight(xml);
    const QByteArray open = QByteArray("<") + kind + "><UID>" + QByteArray::number(uid) + "</UID>";
    const qsizetype from = t.indexOf(open);
    if (from < 0) {
        return {};
    }
    const QByteArray close = QByteArray("</") + kind + ">";
    return t.mid(from, t.indexOf(close, from) + close.size() - from);
}

int count(const QByteArray &haystack, const QByteArray &needle)
{
    int n = 0;
    for (qsizetype at = haystack.indexOf(needle); at >= 0; at = haystack.indexOf(needle, at + 1)) {
        ++n;
    }
    return n;
}

const Task &named(const Project &p, const char *name)
{
    for (const Task &t : p.tasks) {
        if (t.name == QLatin1String(name)) {
            return t;
        }
    }
    qFatal("no task %s", name);
}

} // namespace

class PlanMspdiM79Test final : public QObject {
    Q_OBJECT
private slots:
    void readsWhatProjectWrote()
    {
        QStringList warnings;
        const Project p = readSample(&warnings);
        QVERIFY2(!p.tasks.isEmpty(), "the fixture did not read");
        QCOMPARE(p.info.criticalSlackDays, 1);
        QVERIFY(!p.info.multipleCriticalPaths);
        const Task &pack = named(p, "Pack offices");
        QVERIFY(pack.baselines[0].set && pack.baselines[3].set && pack.baselines[10].set);
        QVERIFY(!pack.baselines[1].set);
        QCOMPARE(pack.baselines[10].cost, 80000);
        QCOMPARE(pack.baselines[10].work, 16 * kHour);
        QCOMPARE_AT(pack.baselines[10].finish, at(3, 17));
        // Leveling: 2 elapsed days and 1 working day, from the unleveled start.
        const Task &move = named(p, "Move furniture");
        QCOMPARE(move.levelingDelay, 2 * 1440);
        QVERIFY(move.levelingDelayElapsed);
        QCOMPARE_AT(move.unleveledStart, at(5));
        QCOMPARE_AT(move.start, at(9));
        QCOMPARE_AT(move.finish, at(10, 17));
        QCOMPARE_AT(move.preLeveledStart, at(5));
        QCOMPARE_AT(move.preLeveledFinish, at(6, 17));
        const Task &it = named(p, "Set up IT");
        QCOMPARE(it.levelingDelay, kDay);
        QVERIFY(!it.levelingDelayElapsed);
        QCOMPARE_AT(it.start, at(12));
        QCOMPARE_AT(p.finish, at(12, 17));
        // Book movers has 8 days of slack: over the limit of 1 day.
        QVERIFY(!named(p, "Book movers").critical);
        // The assignments' baselines.
        const Assignment &a = p.assignments.at(p.assignmentIndex(pack.uid, 1));
        QVERIFY(a.baselines[0].set && a.baselines[3].set && !a.baselines[10].set);
        QCOMPARE(a.baselines[0].cost, 120000);
        QCOMPARE(a.baselines[3].work, 24 * kHour);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
        // A cost resource and its assignment's amount (no warning since M79).
        const Resource &fee = p.resources.at(p.resourceIndex(2));
        QCOMPARE(int(fee.type), int(ResourceType::Cost));
        QCOMPARE(p.assignments.at(p.assignmentIndex(4, 2)).costInput, 65000);
        QCOMPARE(named(p, "Book movers").cost, 65000);
    }

    void costResourcesRoundTrip()
    {
        const Project p = readSample();
        const QByteArray out = writeMspdi(p);
        const QByteArray t = tight(out);
        const qsizetype at = t.indexOf("<Name>Movers fee</Name>");
        QVERIFY(at > 0);
        QVERIFY(t.mid(at).startsWith("<Name>Movers fee</Name><Type>2</Type>"));
        QVERIFY(t.indexOf("<IsCostResource>1</IsCostResource>", at) > 0);
        QVERIFY(element(out, "Assignment", 3).contains("<Cost>65000</Cost>"));
        Project back;
        QVERIFY(readMspdi(out, &back).ok);
        QCOMPARE(back.assignments.at(back.assignmentIndex(4, 2)).costInput, 65000);
        // A cost resource made here is written as Project writes one.
        Project mine = p;
        mine.resources[mine.resourceIndex(2)].extra = QJsonObject();
        const QByteArray fresh = tight(writeMspdi(mine));
        QVERIFY(fresh.contains("<Type>2</Type>"));
        QVERIFY(fresh.contains("<IsCostResource>1</IsCostResource>"));
    }

    void unchangedBaselinesGoBackAsWritten()
    {
        const QByteArray in = sample();
        const QByteArray out = writeMspdi(readSample());
        // Every Baseline element of the file is in the output as it was —
        // Interim, EstimatedDuration, BCWS and BCWP included.
        const QByteArray tin = tight(in);
        int baselines = 0;
        for (qsizetype from = tin.indexOf("<Baseline>"); from >= 0; from = tin.indexOf("<Baseline>", from + 1)) {
            const QByteArray fragment = tin.mid(from, tin.indexOf("</Baseline>", from) + 11 - from);
            QVERIFY2(tight(out).contains(fragment), fragment.constData());
            ++baselines;
        }
        QCOMPARE(baselines, 9);
        QCOMPARE(count(tight(out), "<Baseline>"), 9);
        QVERIFY(element(out, "Task", 0).contains("<BCWS>120000.00</BCWS>"));
        // Writing what we read again changes nothing.
        Project again;
        QVERIFY(readMspdi(out, &again).ok);
        QCOMPARE(writeMspdi(again), out);
    }

    void changedBaselinesAreWrittenFromTheModel()
    {
        Project p = readSample();
        const int pack = p.taskIndex(1);
        p.tasks[pack].duration = 4 * kDay; // Pack now runs to Thursday
        schedule(p);
        QCOMPARE(setBaseline(p, 3), 4);
        QCOMPARE(clearBaseline(p, 10), 1);
        const QByteArray out = writeMspdi(p);
        const QByteArray task = element(out, "Task", 1);
        // Baseline 0 is still the file's; baseline 3 is ours (no Interim).
        QVERIFY(task.contains("<Baseline><Number>0</Number><Interim>0</Interim>"));
        QVERIFY(task.contains("<Baseline><Number>3</Number><Start>2026-03-02T08:00:00</Start>"
                              "<Finish>2026-03-05T17:00:00</Finish><Duration>PT32H0M0S</Duration>"
                              "<DurationFormat>7</DurationFormat><Work>PT32H0M0S</Work><Cost>160000</Cost></Baseline>"));
        QVERIFY(!out.contains("<Number>10</Number>"));
        // The assignment's baseline 3 follows the task's; its 0 is the file's.
        const QByteArray assignment = element(out, "Assignment", 1);
        QVERIFY(assignment.contains("<Baseline><Number>3</Number><Start>2026-03-02T08:00:00</Start>"
                                    "<Finish>2026-03-05T17:00:00</Finish><Work>PT32H0M0S</Work><Cost>160000</Cost></Baseline>"));
        QVERIFY(assignment.contains("<BCWP>60000.00</BCWP>"));
        // The project summary task: baseline 0 untouched (no task's changed),
        // baseline 3 rolled up from the top level — the whole new baseline.
        const QByteArray summary = element(out, "Task", 0);
        QVERIFY(summary.contains("<Number>0</Number><Interim>0</Interim>"));
        // Pack 32 h ($1,600), Move 16 h ($800) and the movers' fee ($650);
        // Set up IT ends it all.
        QVERIFY(summary.contains("<Baseline><Number>3</Number><Start>2026-03-02T08:00:00</Start>"
                                 "<Finish>2026-03-12T17:00:00</Finish><Duration>PT72H0M0S</Duration>"));
        QVERIFY(summary.contains("<Work>PT48H0M0S</Work><Cost>305000</Cost></Baseline>"));
        Project back;
        QVERIFY(readMspdi(out, &back).ok);
        QCOMPARE(back.tasks.at(back.taskIndex(1)).baselines[3].work, 32 * kHour);
        QVERIFY(!back.tasks.at(back.taskIndex(1)).baselines[10].set);
    }

    void levelingDelaysRoundTrip()
    {
        Project p = readSample();
        QByteArray out = writeMspdi(p);
        QVERIFY(element(out, "Task", 2).contains("<LevelingDelay>28800</LevelingDelay><LevelingDelayFormat>8</LevelingDelayFormat>"
                                                 "<PreLeveledStart>2026-03-05T08:00:00</PreLeveledStart>"));
        QVERIFY(element(out, "Task", 3).contains("<LevelingDelay>4800</LevelingDelay><LevelingDelayFormat>7</LevelingDelayFormat>"));
        QVERIFY(element(out, "Task", 1).contains("<LevelingDelay>0</LevelingDelay><LevelingDelayFormat>8</LevelingDelayFormat>"));
        QVERIFY(!element(out, "Task", 4).contains("<LevelingDelay>"));
        // Clear Leveling: the delays go to zero in the file's own formats.
        clearLeveling(p);
        out = writeMspdi(p);
        QVERIFY(element(out, "Task", 3).contains("<LevelingDelay>0</LevelingDelay><LevelingDelayFormat>7</LevelingDelayFormat>"));
        QVERIFY(!element(out, "Task", 2).contains("<PreLeveledStart>"));
        // A delay QindaPlan sets is elapsed days, as Project's leveling writes.
        Task &book = p.tasks[p.taskIndex(4)];
        book.levelingDelay = 1440;
        schedule(p);
        out = writeMspdi(p);
        QVERIFY(element(out, "Task", 4).contains("<LevelingDelay>14400</LevelingDelay><LevelingDelayFormat>8</LevelingDelayFormat>"));
        Project back;
        QVERIFY(readMspdi(out, &back).ok);
        QCOMPARE_AT(back.tasks.at(back.taskIndex(4)).start, at(3));
    }

    void criticalOptionsRoundTrip()
    {
        Project p = readSample();
        QByteArray out = writeMspdi(p);
        QVERIFY(out.contains("<CriticalSlackLimit>1</CriticalSlackLimit>"));
        QVERIFY(out.contains("<MultipleCriticalPaths>0</MultipleCriticalPaths>"));
        p.info.criticalSlackDays = 0;
        p.info.multipleCriticalPaths = true;
        schedule(p);
        out = writeMspdi(p);
        QVERIFY(out.contains("<CriticalSlackLimit>0</CriticalSlackLimit>"));
        QVERIFY(out.contains("<MultipleCriticalPaths>1</MultipleCriticalPaths>"));
        Project back;
        QVERIFY(readMspdi(out, &back).ok);
        QVERIFY(back.info.multipleCriticalPaths);
        QVERIFY(named(back, "Book movers").critical); // it drives nothing
    }

    void writtenShapeFollowsTheSchema()
    {
        Project p = readSample();
        p.tasks[p.taskIndex(1)].duration = 4 * kDay;
        schedule(p);
        setBaseline(p, 3);
        setBaseline(p, 6);
        p.tasks[p.taskIndex(4)].levelingDelay = 720;
        p.tasks[p.taskIndex(4)].levelingDelayElapsed = false;
        schedule(p);
        const QByteArray out = writeMspdi(p);
        using QindaOffice::Plan::Mspdi::Kind;
        using QindaOffice::Plan::Mspdi::schemaRank;
        // The schema's sequences (mspdi_pj15.xsd) for the baselines.
        const QStringList taskBaseline{QStringLiteral("TimephasedData"), QStringLiteral("Number"), QStringLiteral("Interim"),
                                       QStringLiteral("Start"), QStringLiteral("Finish"), QStringLiteral("Duration"),
                                       QStringLiteral("DurationFormat"), QStringLiteral("EstimatedDuration"),
                                       QStringLiteral("Work"), QStringLiteral("Cost"), QStringLiteral("BCWS"),
                                       QStringLiteral("BCWP")};
        const QStringList assignmentBaseline{QStringLiteral("TimephasedData"), QStringLiteral("Number"),
                                             QStringLiteral("Start"), QStringLiteral("Finish"), QStringLiteral("Work"),
                                             QStringLiteral("Cost"), QStringLiteral("BCWS"), QStringLiteral("BCWP")};
        static const QRegularExpression dateTime(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}$"));
        static const QRegularExpression duration(QStringLiteral("^-?PT\\d+H\\d+M\\d+S$"));
        static const QRegularExpression integer(QStringLiteral("^-?\\d+$"));
        QXmlStreamReader xml(out);
        QStringList path;
        QList<QStringList> children(1); // the document's own list of top elements
        int checked = 0;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                children.last().append(xml.name().toString());
                path.append(xml.name().toString());
                children.append(QStringList());
            } else if (xml.isCharacters() && !xml.isWhitespace() && path.size() >= 2) {
                const QString name = path.last();
                const QString text = xml.text().toString();
                if (name == QLatin1String("Start") || name == QLatin1String("Finish") || name.startsWith(QLatin1String("PreLeveled"))) {
                    QVERIFY2(dateTime.match(text).hasMatch(), qPrintable(name + QLatin1Char('=') + text));
                } else if (name == QLatin1String("Duration") || name == QLatin1String("Work")) {
                    QVERIFY2(duration.match(text).hasMatch(), qPrintable(name + QLatin1Char('=') + text));
                } else if (name == QLatin1String("LevelingDelay") || name == QLatin1String("LevelingDelayFormat")
                           || name == QLatin1String("Number") || name == QLatin1String("CriticalSlackLimit")) {
                    QVERIFY2(integer.match(text).hasMatch(), qPrintable(name + QLatin1Char('=') + text));
                }
            } else if (xml.isEndElement()) {
                const QString name = path.takeLast();
                const QStringList kids = children.takeLast();
                const QString parent = path.isEmpty() ? QString() : path.last();
                QStringList order;
                Kind kind = Kind::Task;
                if (name == QLatin1String("Baseline")) {
                    order = parent == QLatin1String("Assignment") ? assignmentBaseline : taskBaseline;
                } else if (name != QLatin1String("Task") && name != QLatin1String("Assignment")) {
                    continue;
                } else {
                    kind = name == QLatin1String("Task") ? Kind::Task : Kind::Assignment;
                }
                int last = -1;
                for (const QString &kid : kids) {
                    const int rank = order.isEmpty() ? schemaRank(kind, kid) : int(order.indexOf(kid));
                    QVERIFY2(rank >= 0, qPrintable(name + QStringLiteral(": unknown child ") + kid));
                    QVERIFY2(rank >= last, qPrintable(name + QStringLiteral(": ") + kid + QStringLiteral(" out of order")));
                    last = rank;
                }
                ++checked;
            }
        }
        QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
        QVERIFY(checked > 20);
        QVERIFY(element(out, "Task", 4).contains("<LevelingDelay>7200</LevelingDelay><LevelingDelayFormat>7</LevelingDelayFormat>"));
    }

    void recurrencesFromProjectAreScheduled()
    {
        // Project's codes (mspdi_recurrence.cpp has the table).
        using QindaOffice::Plan::Mspdi::parseRecurringException;
        using Kind = RecurringException::Kind;
        const auto parse = [](const char *xml, QString *why = nullptr) {
            return parseRecurringException(QString::fromUtf8(xml), why);
        };
        const auto christmas = parse("<Exception><TimePeriod><FromDate>2026-01-01T00:00:00</FromDate>"
                                     "<ToDate>2030-12-25T23:59:00</ToDate></TimePeriod><Name>Christmas</Name><Type>2</Type>"
                                     "<Month>11</Month><MonthDay>25</MonthDay><DayWorking>0</DayWorking></Exception>");
        QVERIFY(christmas);
        QCOMPARE(int(christmas->kind), int(Kind::YearlyByDay));
        QCOMPARE(christmas->month, 12);
        QCOMPARE(christmas->day, 25);
        QVERIFY(christmas->times.isEmpty());
        QVERIFY(christmas->fallsOn(QDate(2027, 12, 25)));
        const auto thanksgiving = parse("<Exception><TimePeriod><FromDate>2026-01-01T00:00:00</FromDate>"
                                        "<ToDate>2030-12-31T23:59:00</ToDate></TimePeriod><Type>3</Type><Month>10</Month>"
                                        "<MonthItem>7</MonthItem><MonthPosition>3</MonthPosition><DayWorking>0</DayWorking>"
                                        "</Exception>");
        QVERIFY(thanksgiving);
        QCOMPARE(thanksgiving->weekday, 4); // Thursday
        QVERIFY(thanksgiving->fallsOn(QDate(2026, 11, 26)));
        QVERIFY(thanksgiving->fallsOn(QDate(2027, 11, 25)));
        const auto standUp = parse("<Exception><TimePeriod><FromDate>2026-03-02T00:00:00</FromDate>"
                                   "<ToDate>2026-03-31T23:59:00</ToDate></TimePeriod><Type>6</Type><Period>2</Period>"
                                   "<DaysOfWeek>34</DaysOfWeek><DayWorking>1</DayWorking><WorkingTimes><WorkingTime>"
                                   "<FromTime>09:00:00</FromTime><ToTime>17:00:00</ToTime></WorkingTime></WorkingTimes>"
                                   "</Exception>");
        QVERIFY(standUp);
        QCOMPARE(standUp->daysOfWeek, 0b0010001); // Monday and Friday
        QCOMPARE(standUp->every, 2);
        QCOMPARE(standUp->times, (DayHours{{540, 1020}}));
        const auto lastFriday = parse("<Exception><TimePeriod><FromDate>2026-01-01T00:00:00</FromDate>"
                                      "<ToDate>2026-12-31T23:59:00</ToDate></TimePeriod><Type>5</Type><Period>1</Period>"
                                      "<MonthItem>8</MonthItem><MonthPosition>4</MonthPosition><DayWorking>0</DayWorking>"
                                      "</Exception>");
        QVERIFY(lastFriday);
        QVERIFY(lastFriday->fallsOn(QDate(2026, 3, 27)));
        const auto counted = parse("<Exception><EnteredByOccurrences>1</EnteredByOccurrences><TimePeriod>"
                                   "<FromDate>2026-03-02T00:00:00</FromDate></TimePeriod><Occurrences>3</Occurrences>"
                                   "<Type>6</Type><Period>1</Period><DaysOfWeek>2</DaysOfWeek><DayWorking>0</DayWorking>"
                                   "</Exception>");
        QVERIFY(counted);
        QCOMPARE(counted->to, QDate(2026, 3, 16)); // the third Monday
        QCOMPARE(int(parse("<Exception><TimePeriod><FromDate>2026-03-02T00:00:00</FromDate></TimePeriod><Type>7</Type>"
                           "<Period>3</Period><DayWorking>0</DayWorking></Exception>")->kind),
                 int(Kind::EveryDays));
        QString why;
        QVERIFY(!parse("<Exception><TimePeriod><FromDate>2026-01-01T00:00:00</FromDate></TimePeriod><Type>5</Type>"
                       "<MonthItem>1</MonthItem><MonthPosition>0</MonthPosition><DayWorking>0</DayWorking></Exception>",
                       &why));
        QVERIFY(why.contains(QStringLiteral("weekday")));
        // The keep sample's "Monday review" (non-working Mondays from 6 April)
        // is scheduled, its work week read, both written back as they were.
        QFile f(QStringLiteral(SOURCE_DIR "/data/project_keep_sample.xml"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        Project keep;
        const MspdiResult r = readMspdi(f.readAll(), &keep);
        QVERIFY(r.ok);
        QVERIFY(!r.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("not scheduled")));
        const Calendar &standard = keep.calendars.at(keep.calendarIndex(1));
        QCOMPARE(standard.recurring.size(), 1);
        QCOMPARE(standard.workWeeks.size(), 1);
        QCOMPARE(standard.workWeeks.first().name, QStringLiteral("Summer"));
        QCOMPARE(WorkCalendar(keep, 1).minutesOn(QDate(2026, 4, 13)), 0);
        QCOMPARE(WorkCalendar(keep, 1).minutesOn(QDate(2026, 4, 14)), kDay);
        const QByteArray out = writeMspdi(keep);
        QVERIFY(out.contains("<Name>Monday review</Name>"));
        QVERIFY(out.contains("<Name>Summer</Name>"));
        Project back;
        QVERIFY(readPlanJson(writePlanJson(keep), &back).ok); // and through .qplan
        QCOMPARE(back.calendars.at(back.calendarIndex(1)).recurring, standard.recurring);
        QCOMPARE(back.calendars.at(back.calendarIndex(1)).workWeeks, standard.workWeeks);
    }

    void qplanKeepsItAll()
    {
        const Project p = readSample();
        Project back;
        const LoadResult r = readPlanJson(writePlanJson(p), &back);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(writeMspdi(back), writeMspdi(p));
        QCOMPARE(back.tasks.at(back.taskIndex(2)).levelingDelay, 2 * 1440);
        QCOMPARE(back.info.criticalSlackDays, 1);
    }

    void earlierQplanFilesAreAdopted()
    {
        // M63–M78 kept baseline 4 as a verbatim element; M51–M78 leveled
        // with an absolute floor (B could start Monday; leveled to Thursday).
        const QByteArray json = R"({"format": "qindaplan", "version": 1,
            "project": {"title": "Old", "start": "2026-03-02T08:00", "calendarUid": 1,
                        "mspdi": [{"name": "CriticalSlackLimit", "after": "FYStartDate",
                                   "xml": "<CriticalSlackLimit>2</CriticalSlackLimit>"}]},
            "calendars": [{"uid": 1, "name": "Standard", "week": {
                "mon": [["08:00","12:00"],["13:00","17:00"]], "tue": [["08:00","12:00"],["13:00","17:00"]],
                "wed": [["08:00","12:00"],["13:00","17:00"]], "thu": [["08:00","12:00"],["13:00","17:00"]],
                "fri": [["08:00","12:00"],["13:00","17:00"]], "sat": [], "sun": []}}],
            "tasks": [
              {"uid": 1, "guid": "00000000-0000-4000-8000-000000000001", "name": "A", "level": 1, "duration": 960,
               "mspdi": [{"name": "Baseline", "after": "ExtendedAttribute",
                          "xml": "<Baseline><Number>4</Number><Start>2026-03-09T08:00:00</Start><Finish>2026-03-10T17:00:00</Finish><Duration>PT16H0M0S</Duration><Cost>5000</Cost></Baseline>"},
                         {"name": "Hyperlink", "after": "Notes", "xml": "<Hyperlink>Plan</Hyperlink>"}]},
              {"uid": 2, "guid": "00000000-0000-4000-8000-000000000002", "name": "B", "level": 1, "duration": 960,
               "levelingFloor": "2026-03-05T08:00"},
              {"uid": 3, "guid": "00000000-0000-4000-8000-000000000003", "name": "C", "level": 1, "duration": 480,
               "mspdi": [{"name": "LevelingDelay", "after": "Deadline", "derived": true,
                          "xml": "<LevelingDelay>14400</LevelingDelay>"},
                         {"name": "LevelingDelayFormat", "after": "LevelingDelay", "derived": true,
                          "xml": "<LevelingDelayFormat>8</LevelingDelayFormat>"}]}],
            "resources": [], "assignments": [], "view": {}})";
        Project p;
        const LoadResult r = readPlanJson(json, &p);
        QVERIFY2(r.ok, qPrintable(r.error));
        const Task &a = p.tasks.at(p.taskIndex(1));
        QVERIFY(a.baselines[4].set);
        QCOMPARE(a.baselines[4].cost, 5000);
        QCOMPARE_AT(a.baselines[4].finish, at(10, 17));
        const QByteArray out = writeMspdi(p);
        QCOMPARE(count(out, "<Number>4</Number>"), 1);
        QVERIFY(out.contains("<Hyperlink>Plan</Hyperlink>"));
        const Task &b = p.tasks.at(p.taskIndex(2));
        QCOMPARE(b.levelingFloor, kNoDate);
        QCOMPARE(b.levelingDelay, 3 * 1440);
        QCOMPARE_AT(b.start, at(5));
        const QByteArray saved = writePlanJson(p);
        QVERIFY(!saved.contains("levelingFloor"));
        QVERIFY(saved.contains("\"levelingDelay\": 4320"));
        // The options and C's leveling delay (1 eday) join the model, and
        // each element is written once.
        QCOMPARE(p.info.criticalSlackDays, 2);
        QCOMPARE(count(out, "<CriticalSlackLimit>"), 1);
        QVERIFY(out.contains("<CriticalSlackLimit>2</CriticalSlackLimit>"));
        const Task &c = p.tasks.at(p.taskIndex(3));
        QCOMPARE(c.levelingDelay, 1440);
        QCOMPARE_AT(c.start, at(3));
        QCOMPARE(count(element(out, "Task", 3), "<LevelingDelay>"), 1);
    }
};

QTEST_GUILESS_MAIN(PlanMspdiM79Test)

#include "tst_plan_mspdi_m79.moc"
