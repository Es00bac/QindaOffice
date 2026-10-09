// SPDX-License-Identifier: GPL-3.0-or-later
// Microsoft Project XML (mspdi.h): a file in the shape Project 2016 saves
// (data/project2016_sample.xml) imports field by field and schedules to the
// hand-computed dates below; export → re-import gives the same model; the
// export is well-formed MSPDI; malformed input fails with its line.
//
// Expected schedule (standard calendar Mon–Fri 08–12, 13–17; Ana away
// Mon 9 – Wed 11 March):
//   Brief        done: Mon 2 – Tue 3 (actuals)
//   Concepts     FS Brief +2d, started Fri 6 (actual), 3 days on Ana's
//                calendar: Fri 6, Thu 12, Fri 13 → finishes Fri 13 17:00
//   Review       FF Concepts (SNET Thu 12 is earlier): Fri 13
//   Approved     milestone FS Review: Fri 13 17:00
//   Print run    FS Approved -1d: Fri 13 08:00 – Mon 16 17:00
//   Proofing     SS Print run +50% (1 day): Mon 16 from 09:00 (the
//                weekly "Monday stand-up" exception: Mondays 09–17, M79)
//   Pack         SF Proofing: finishes when Proofing starts → Fri 13
//   Vendor call  manual: Mon 16 – Tue 17
//   Foil cover   inactive (not in roll-ups)
//   Phase 1      Mon 2 – Fri 13; Phase 2 Fri 13 – Tue 17; finish Tue 17 17:00

#include "mspdi.h"
#include "plan_fixture.h"
#include "work_calendar.h"

#include <QFile>
#include <QXmlStreamReader>

using namespace PlanTest;

namespace {

QByteArray sample()
{
    QFile f(QStringLiteral(SOURCE_DIR "/data/project2016_sample.xml"));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

const Task &byName(const Project &p, const char *name)
{
    for (const Task &t : p.tasks) {
        if (t.name == QLatin1String(name)) {
            return t;
        }
    }
    qFatal("no task %s", name);
}

bool sameCalendar(const Calendar &a, const Calendar &b)
{
    return a.uid == b.uid && a.name == b.name && a.baseUid == b.baseUid && a.week == b.week && a.exceptions == b.exceptions;
}

void compareTasks(const Task &a, const Task &b)
{
    const QString what = a.name;
    QCOMPARE(b.uid, a.uid);
    QCOMPARE(b.guid, a.guid);
    QCOMPARE(b.name, a.name);
    QCOMPARE(b.outlineLevel, a.outlineLevel);
    QCOMPARE(b.duration, a.duration);
    QCOMPARE(int(b.durationUnit), int(a.durationUnit));
    QCOMPARE(b.estimated, a.estimated);
    QCOMPARE(b.milestone, a.milestone);
    QCOMPARE(b.manual, a.manual);
    QCOMPARE(b.active, a.active);
    QCOMPARE(b.manualStart, a.manualStart);
    QCOMPARE(b.manualFinish, a.manualFinish);
    QCOMPARE(int(b.constraint), int(a.constraint));
    QCOMPARE(b.constraintDate, a.constraintDate);
    QCOMPARE(b.predecessors, a.predecessors);
    QCOMPARE(b.percentComplete, a.percentComplete);
    QCOMPARE(b.actualStart, a.actualStart);
    QCOMPARE(b.actualFinish, a.actualFinish);
    QCOMPARE(b.calendarUid, a.calendarUid);
    QCOMPARE(b.fixedCost, a.fixedCost);
    QCOMPARE(b.notes, a.notes);
    for (int n = 0; n < kBaselineCount; ++n) {
        const BaselineData &x = a.baselines[size_t(n)];
        const BaselineData &y = b.baselines[size_t(n)];
        QCOMPARE(y.set, x.set);
        QCOMPARE(y.start, x.start);
        QCOMPARE(y.finish, x.finish);
        QCOMPARE(y.duration, x.duration);
        QCOMPARE(y.work, x.work);
        QCOMPARE(y.cost, x.cost);
    }
    QCOMPARE_AT(b.start, a.start);
    QCOMPARE_AT(b.finish, a.finish);
    QCOMPARE(b.totalSlack, a.totalSlack);
    QCOMPARE(b.critical, a.critical);
    QCOMPARE(b.cost, a.cost);
    QCOMPARE(b.work, a.work);
    Q_UNUSED(what)
}

} // namespace

class PlanMspdiTest final : public QObject {
    Q_OBJECT
private slots:
    void importsProject2016File()
    {
        const QByteArray xml = sample();
        QVERIFY(!xml.isEmpty());
        QVERIFY(looksLikeMspdi(xml));
        Project p;
        const MspdiResult r = readMspdi(xml, &p);
        QVERIFY2(r.ok, qPrintable(r.error));

        // Project properties; the UID 0 summary task becomes the notes.
        QCOMPARE(p.info.title, QStringLiteral("Spring catalogue"));
        QCOMPARE(p.info.company, QStringLiteral("Sloom Studio"));
        QCOMPARE(p.info.manager, QStringLiteral("Jarrod"));
        QCOMPARE(p.info.notes, QStringLiteral("Catalogue for the spring season."));
        QCOMPARE_AT(p.info.start, at(2));
        QCOMPARE_AT(p.info.statusDate, at(6, 17));
        QCOMPARE(p.info.currency, QStringLiteral("USD"));
        QCOMPARE(p.info.calendarUid, 1);
        QCOMPARE(p.info.minutesPerDay, 480);
        QCOMPARE(p.info.defaultFinishMinute, 17 * 60);

        // Calendars: the week, the legacy + 2007 copies of one holiday merged
        // under its name, the weekly recurrence skipped with a warning.
        QCOMPARE(p.calendars.size(), 3);
        const Calendar &standard = p.calendars.at(0);
        const DayHours day{{480, 720}, {780, 1020}};
        for (int d = 0; d < 5; ++d) {
            QCOMPARE(*standard.week[size_t(d)], day);
        }
        QCOMPARE(*standard.week[5], DayHours{});
        QCOMPARE(*standard.week[6], DayHours{});
        QCOMPARE(standard.exceptions.size(), 2);
        QCOMPARE(standard.exceptions.at(0).from, QDate(2026, 3, 20));
        QCOMPARE(standard.exceptions.at(0).name, QStringLiteral("Studio day"));
        QCOMPARE(standard.exceptions.at(1).name, QStringLiteral("Good Friday"));
        // M79: the weekly "Monday stand-up" (Mondays 09:00–17:00) is scheduled.
        QVERIFY(!r.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("Monday stand-up")));
        QCOMPARE(standard.recurring.size(), 1);
        QCOMPARE(WorkCalendar(p, 1).hours(QDate(2026, 3, 16)).first().from, 9 * 60);
        const Calendar &ana = p.calendars.at(1);
        QCOMPARE(ana.baseUid, 1);
        QVERIFY(!ana.week[0].has_value());
        QCOMPARE(ana.exceptions.size(), 1);
        QCOMPARE(ana.exceptions.at(0).to, QDate(2026, 3, 11));
        QCOMPARE(WorkCalendar(p, 2).minutesOn(QDate(2026, 3, 10)), 0);
        QCOMPARE(WorkCalendar(p, 2).minutesOn(QDate(2026, 3, 20)), 0); // the base's holiday

        // Tasks: the blank row and the summary task are gone; 11 remain.
        QCOMPARE(p.tasks.size(), 11);
        const Task &brief = byName(p, "Brief");
        QCOMPARE(brief.guid, QUuid(QStringLiteral("0B6F3A10-1111-4C11-8A11-000000000002")));
        QCOMPARE(brief.duration, 2 * kDay);
        QCOMPARE(brief.percentComplete, 100);
        QCOMPARE(brief.notes, QStringLiteral("Agree scope, page count and paper stock."));
        QVERIFY(brief.baselines[0].set && brief.baselines[1].set && !brief.baselines[2].set);
        QCOMPARE(brief.baselines[0].work, 8 * kHour);
        QCOMPARE(brief.baselines[0].cost, 38500);
        QCOMPARE_AT(brief.baselines[1].finish, at(4, 17));
        // M79: baselines 3–10 are modelled (baseline 5 was only kept before).
        QVERIFY(brief.baselines[5].set);
        QCOMPARE_AT(brief.baselines[5].finish, at(2, 17));
        const Task &phase1 = byName(p, "Phase 1: Design");
        QVERIFY(phase1.summary);
        QCOMPARE(phase1.actualStart, kNoDate); // roll-ups are ours to compute
        const Task &concepts = byName(p, "Concepts");
        QCOMPARE(concepts.predecessors.size(), 1);
        QCOMPARE(concepts.predecessors.at(0).lag, 2 * kDay);
        QCOMPARE(int(concepts.predecessors.at(0).type), int(LinkType::FinishToStart));
        QCOMPARE(int(byName(p, "Review").predecessors.at(0).type), int(LinkType::FinishToFinish));
        QCOMPARE(int(byName(p, "Review").constraint), int(ConstraintType::StartNoEarlierThan));
        QCOMPARE(byName(p, "Review").fixedCost, 15000);
        QVERIFY(byName(p, "Design approved").milestone);
        QCOMPARE(byName(p, "Print run").predecessors.at(0).lag, -kDay);
        const Task &proofing = byName(p, "Proofing");
        QCOMPARE(int(proofing.predecessors.at(0).type), int(LinkType::StartToStart));
        QCOMPARE(*proofing.predecessors.at(0).lagPercent, 50);
        QVERIFY(proofing.estimated);
        const Task &pack = byName(p, "Pack");
        QCOMPARE(pack.predecessors.size(), 1); // the link to UID 99 was dropped
        QCOMPARE(int(pack.predecessors.at(0).type), int(LinkType::StartToFinish));
        QCOMPARE(int(pack.durationUnit), int(DurationUnit::Hours));
        QVERIFY(r.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("UID 99")));
        const Task &vendor = byName(p, "Vendor call");
        QVERIFY(vendor.manual);
        QCOMPARE_AT(vendor.manualStart, at(16));
        const Task &foil = byName(p, "Foil cover (dropped)");
        QVERIFY(!foil.active);
        QVERIFY(r.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("Elapsed")));

        // Resources: the blank UID 0 is skipped; rates are per hour.
        QCOMPARE(p.resources.size(), 3);
        const Resource &anaR = p.resources.at(p.resourceIndex(1));
        QCOMPARE(anaR.standardRate, 6000);
        QCOMPARE(anaR.calendarUid, 2);
        QCOMPARE(anaR.email, QStringLiteral("ana@example.com"));
        const Resource &ben = p.resources.at(p.resourceIndex(2));
        QCOMPARE(ben.maxUnits, 50);
        QCOMPARE(ben.costPerUse, 2500);
        const Resource &paper = p.resources.at(p.resourceIndex(3));
        QCOMPARE(int(paper.type), int(ResourceType::Material));
        QCOMPARE(paper.materialLabel, QStringLiteral("reams"));
        QCOMPARE(paper.standardRate, 1250);
        // Assignments: the unassigned placeholder skipped silently, the
        // dangling one dropped with a warning.
        QCOMPARE(p.assignments.size(), 3);
        QCOMPARE(p.assignments.at(p.assignmentIndex(2, 2)).units, 50);
        QCOMPARE(p.assignments.at(p.assignmentIndex(7, 3)).units, 4000);
        QVERIFY(r.warnings.join(QLatin1Char('\n')).contains(QStringLiteral("UID 5")));

        // The schedule (see the header).
        QCOMPARE_AT(concepts.start, at(6));
        QCOMPARE_AT(concepts.finish, at(13, 17));
        QCOMPARE_AT(byName(p, "Review").start, at(13));
        QCOMPARE_AT(byName(p, "Design approved").start, at(13, 17));
        QCOMPARE_AT(byName(p, "Print run").start, at(13));
        QCOMPARE_AT(byName(p, "Print run").finish, at(16, 17));
        QCOMPARE_AT(proofing.start, at(16, 9)); // the Monday stand-up hours (M79)
        QCOMPARE_AT(pack.start, at(13));
        QCOMPARE_AT(pack.finish, at(13, 17));
        QCOMPARE_AT(phase1.finish, at(13, 17));
        QCOMPARE_AT(byName(p, "Phase 2: Production").start, at(13));
        QCOMPARE_AT(p.finish, at(17, 17));
        QVERIFY(vendor.critical);
        // Costs: Ben 8 h × $45 + $25 per use; Ana 24 h × $60; 40 reams × $12.50.
        QCOMPARE(brief.cost, 38500);
        QCOMPARE(concepts.cost, 144000);
        QCOMPARE(byName(p, "Print run").cost, 50000);
        QCOMPARE(phase1.cost, 38500 + 144000 + 15000);
    }

    void roundTripsThroughExport()
    {
        Project first;
        QVERIFY(readMspdi(sample(), &first).ok);
        const QByteArray xml = writeMspdi(first);
        QVERIFY(looksLikeMspdi(xml));
        Project second;
        const MspdiResult r = readMspdi(xml, &second);
        QVERIFY2(r.ok, qPrintable(r.error));
        // Our export adds no problem of its own. (M63: what the file had and
        // QindaPlan does not schedule — the recurring exception, baseline 5 —
        // is kept and written back, so re-reading reports it again.)
        Project original;
        const MspdiResult first0 = readMspdi(sample(), &original);
        for (const QString &w : r.warnings) {
            QVERIFY2(first0.warnings.contains(w), qPrintable(w));
        }
        QCOMPARE(second.info.title, first.info.title);
        QCOMPARE(second.info.notes, first.info.notes);
        QCOMPARE(second.info.start, first.info.start);
        QCOMPARE(second.info.statusDate, first.info.statusDate);
        QCOMPARE(second.info.calendarUid, first.info.calendarUid);
        QCOMPARE(second.info.company, first.info.company);
        QCOMPARE(second.info.manager, first.info.manager);
        QCOMPARE(second.calendars.size(), first.calendars.size());
        for (int i = 0; i < first.calendars.size(); ++i) {
            QVERIFY2(sameCalendar(first.calendars.at(i), second.calendars.at(i)), qPrintable(first.calendars.at(i).name));
        }
        QCOMPARE(second.tasks.size(), first.tasks.size());
        for (int i = 0; i < first.tasks.size(); ++i) {
            compareTasks(first.tasks.at(i), second.tasks.at(i));
            if (QTest::currentTestFailed()) {
                qWarning("task %s differs", qPrintable(first.tasks.at(i).name));
                return;
            }
        }
        QCOMPARE(second.resources.size(), first.resources.size());
        for (int i = 0; i < first.resources.size(); ++i) {
            const Resource &a = first.resources.at(i);
            const Resource &b = second.resources.at(i);
            QCOMPARE(b.uid, a.uid);
            QCOMPARE(b.name, a.name);
            QCOMPARE(int(b.type), int(a.type));
            QCOMPARE(b.initials, a.initials);
            QCOMPARE(b.group, a.group);
            QCOMPARE(b.email, a.email);
            QCOMPARE(b.materialLabel, a.materialLabel);
            QCOMPARE(b.maxUnits, a.maxUnits);
            QCOMPARE(b.standardRate, a.standardRate);
            QCOMPARE(b.costPerUse, a.costPerUse);
            QCOMPARE(b.calendarUid, a.calendarUid);
        }
        QCOMPARE(second.assignments.size(), first.assignments.size());
        for (int i = 0; i < first.assignments.size(); ++i) {
            QCOMPARE(second.assignments.at(i).uid, first.assignments.at(i).uid);
            QCOMPARE(second.assignments.at(i).taskUid, first.assignments.at(i).taskUid);
            QCOMPARE(second.assignments.at(i).resourceUid, first.assignments.at(i).resourceUid);
            QCOMPARE(second.assignments.at(i).units, first.assignments.at(i).units);
        }
        QCOMPARE(second.finish, first.finish);
    }

    void exportIsWellFormedMspdi()
    {
        Project p;
        QVERIFY(readMspdi(sample(), &p).ok);
        const QByteArray xml = writeMspdi(p);
        QXmlStreamReader reader(xml);
        int tasks = 0;
        int links = 0;
        int baselines = 0;
        int exceptions = 0;
        QStringList firstChildren;
        int depth = 0;
        while (!reader.atEnd()) {
            reader.readNext();
            if (reader.isStartElement()) {
                ++depth;
                if (depth == 1) {
                    QCOMPARE(reader.name(), QStringView(u"Project"));
                    QCOMPARE(reader.namespaceUri(), QStringView(u"http://schemas.microsoft.com/project"));
                } else if (depth == 2) {
                    firstChildren.append(reader.name().toString());
                }
                tasks += reader.name() == u"Task" ? 1 : 0;
                links += reader.name() == u"PredecessorLink" ? 1 : 0;
                baselines += reader.name() == u"Baseline" ? 1 : 0;
                exceptions += reader.name() == u"Exception" ? 1 : 0;
            } else if (reader.isEndElement()) {
                --depth;
            }
        }
        QVERIFY2(!reader.hasError(), qPrintable(reader.errorString()));
        QCOMPARE(tasks, 12);      // UID 0 + 11
        QCOMPARE(links, 6);
        // M63: baseline 5 and the recurring "Monday stand-up" are kept and
        // written back next to what QindaPlan models.
        QCOMPARE(baselines, 4);
        QCOMPARE(exceptions, 4);  // two holidays + the vacation + the kept recurring one
        // Schema order: properties, then Calendars, Tasks, Resources, Assignments.
        const int calendars = int(firstChildren.indexOf(QStringLiteral("Calendars")));
        QVERIFY(calendars > firstChildren.indexOf(QStringLiteral("StatusDate")));
        QCOMPARE(firstChildren.mid(calendars), (QStringList{QStringLiteral("Calendars"), QStringLiteral("Tasks"),
                                                            QStringLiteral("Resources"), QStringLiteral("Assignments")}));
        QVERIFY(xml.contains("<LinkLag>9600</LinkLag>"));
        QVERIFY(xml.contains("<LinkLag>500</LinkLag>\n    <LagFormat>19</LagFormat>"));
        QVERIFY(xml.contains("<StandardRate>60.00</StandardRate>"));
        QVERIFY(xml.contains("<Units>40.00</Units>"));
        QVERIFY(xml.contains("<FixedCost>15000</FixedCost>"));
        QVERIFY(xml.contains("<Duration>PT16H0M0S</Duration>"));
    }

    void malformedAndForeignInput()
    {
        Project p;
        MspdiResult r = readMspdi("<?xml version=\"1.0\"?>\n<Project xmlns=\"http://schemas.microsoft.com/project\">\n"
                                  "<Tasks><Task><UID>1</Task>\n</Project>",
                                  &p);
        QVERIFY(!r.ok);
        QVERIFY2(r.error.startsWith(QStringLiteral("line 3")), qPrintable(r.error));
        r = readMspdi("<?xml version=\"1.0\"?><workbook/>", &p);
        QVERIFY(!r.ok);
        QVERIFY(!looksLikeMspdi("<?xml version=\"1.0\"?><workbook/>"));
        QVERIFY(!looksLikeMspdi("<Project><Name>not MSPDI</Name></Project>"));
        // An empty but valid document still gives a schedulable project.
        r = readMspdi("<Project xmlns=\"http://schemas.microsoft.com/project\"><StartDate>2026-03-02T08:00:00</StartDate></Project>", &p);
        QVERIFY(r.ok);
        QCOMPARE(p.calendars.size(), 1);
        QCOMPARE_AT(p.info.start, at(2));
    }
};

QTEST_GUILESS_MAIN(PlanMspdiTest)

#include "tst_plan_mspdi.moc"
