// SPDX-License-Identifier: GPL-3.0-or-later
// The .qplan format (plan_file.h): a full round trip, unknown fields kept,
// newer versions refused, damaged input rejected, atomic save; the task
// clipboard (subtrees, remapped links, resources by name) and CSV/TSV.

#include "plan_edit.h"
#include "plan_file.h"
#include "plan_fixture.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

using namespace PlanTest;

namespace {

Builder sample()
{
    Builder b;
    b.p.info.title = QStringLiteral("Spring fundraiser gala");
    b.p.info.manager = QStringLiteral("Dana Whitfield");
    b.p.info.statusDate = at(9, 17);
    b.p.calendars[0].exceptions.append({QDate(2026, 3, 13), QDate(2026, 3, 13), QStringLiteral("Staff day"), {}});
    Calendar ana;
    ana.uid = 2;
    ana.name = QStringLiteral("Ana");
    ana.baseUid = 1;
    ana.week[5] = DayHours{{9 * 60, 13 * 60}};
    b.p.calendars.append(ana);
    const int phase = b.task(QStringLiteral("Planning"), 0);
    const int a = b.task(QStringLiteral("Book venue"), 3 * kDay, 2);
    const int c = b.task(QStringLiteral("Caterer tasting"), 4 * kHour, 2);
    const int m = b.task(QStringLiteral("Venue signed"), 0, 2);
    b.p.tasks[c].durationUnit = DurationUnit::Hours;
    b.p.tasks[c].estimated = true;
    b.link(c, a, LinkType::StartToStart, kDay);
    b.link(m, a);
    b.p.tasks[m].predecessors[0].lagPercent = 10;
    b.constrain(a, ConstraintType::StartNoEarlierThan, at(3));
    b.p.tasks[a].notes = QStringLiteral("Ballroom or the botanical garden");
    b.p.tasks[a].percentComplete = 25;
    b.p.tasks[a].actualStart = at(3);
    b.p.tasks[a].fixedCost = 50000;
    const int r = b.resource(QStringLiteral("Ana"), 4500);
    b.p.resources.last().calendarUid = 2;
    b.p.resources.last().email = QStringLiteral("ana@example.org");
    const int paper = b.resource(QStringLiteral("Card stock"), 35, 100, ResourceType::Material);
    b.p.resources.last().materialLabel = QStringLiteral("sheets");
    b.assign(a, r, 50);
    b.assign(c, paper, 20000);
    Q_UNUSED(phase)
    b.run();
    return b;
}

} // namespace

class PlanFileTest final : public QObject {
    Q_OBJECT
private slots:
    void roundTrip()
    {
        const Builder b = sample();
        const QByteArray json = writePlanJson(b.p);
        Project back;
        const LoadResult r = readPlanJson(json, &back);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.warnings.isEmpty());
        QCOMPARE(writePlanJson(back), json); // byte-identical
        QCOMPARE(back.info.title, b.p.info.title);
        QCOMPARE(back.info.statusDate, b.p.info.statusDate);
        QCOMPARE(back.calendars.size(), 2);
        QCOMPARE(back.calendars.at(0).exceptions.size(), 1);
        QVERIFY(back.calendars.at(1).week[5].has_value());
        QVERIFY(!back.calendars.at(1).week[0].has_value());
        QCOMPARE(back.tasks.size(), b.p.tasks.size());
        for (int i = 0; i < back.tasks.size(); ++i) {
            const Task &x = back.tasks.at(i);
            const Task &y = b.p.tasks.at(i);
            QCOMPARE(x.uid, y.uid);
            QCOMPARE(x.guid, y.guid);
            QCOMPARE(x.name, y.name);
            QCOMPARE(x.outlineLevel, y.outlineLevel);
            QCOMPARE(x.duration, y.duration);
            QCOMPARE(x.durationUnit, y.durationUnit);
            QCOMPARE(x.estimated, y.estimated);
            QCOMPARE(x.predecessors, y.predecessors);
            QCOMPARE(x.start, y.start);
            QCOMPARE(x.finish, y.finish);
            QCOMPARE(x.cost, y.cost);
        }
        QCOMPARE(back.resources.at(0).email, QStringLiteral("ana@example.org"));
        QCOMPARE(back.assignments.size(), 2);
        QCOMPARE(back.assignments.at(1).units, 20000);
    }

    void unknownFieldsSurvive()
    {
        const Builder b = sample();
        QJsonObject root = QJsonDocument::fromJson(writePlanJson(b.p)).object();
        root.insert(QStringLiteral("futureTopLevel"), QStringLiteral("keep me"));
        QJsonArray tasks = root.value(QStringLiteral("tasks")).toArray();
        QJsonObject first = tasks.at(1).toObject();
        first.insert(QStringLiteral("riskScore"), 7);
        tasks[1] = first;
        root.insert(QStringLiteral("tasks"), tasks);
        QJsonObject info = root.value(QStringLiteral("project")).toObject();
        info.insert(QStringLiteral("portfolio"), QStringLiteral("Events"));
        root.insert(QStringLiteral("project"), info);
        Project back;
        QVERIFY(readPlanJson(QJsonDocument(root).toJson(), &back).ok);
        QCOMPARE(back.tasks.at(1).extra.value(QStringLiteral("riskScore")).toInt(), 7);
        const QJsonObject again = QJsonDocument::fromJson(writePlanJson(back)).object();
        QCOMPARE(again.value(QStringLiteral("futureTopLevel")).toString(), QStringLiteral("keep me"));
        QCOMPARE(again.value(QStringLiteral("tasks")).toArray().at(1).toObject().value(QStringLiteral("riskScore")).toInt(), 7);
        QCOMPARE(again.value(QStringLiteral("project")).toObject().value(QStringLiteral("portfolio")).toString(),
                 QStringLiteral("Events"));
    }

    void refusalsAndRepairs()
    {
        Project p;
        LoadResult r = readPlanJson(QByteArrayLiteral("{\"format\":\"qindaplan\",\"version\":2}"), &p);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("newer")));
        QVERIFY(!readPlanJson(QByteArrayLiteral("{\"format\":\"other\"}"), &p).ok);
        r = readPlanJson(QByteArrayLiteral("{not json"), &p);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("byte")));
        // Duplicate uids, a dangling assignment, a missing calendar.
        const QByteArray repaired = QByteArrayLiteral(
            "{\"format\":\"qindaplan\",\"version\":1,\"project\":{\"start\":\"2026-03-02T08:00\",\"calendarUid\":7},"
            "\"tasks\":[{\"uid\":1,\"name\":\"A\"},{\"uid\":1,\"name\":\"B\"}],"
            "\"resources\":[{\"uid\":1,\"name\":\"R\"}],\"assignments\":[{\"uid\":1,\"task\":9,\"resource\":1}]}");
        r = readPlanJson(repaired, &p);
        QVERIFY(r.ok);
        QCOMPARE(r.warnings.size(), 3);
        QVERIFY(p.tasks.at(0).uid != p.tasks.at(1).uid);
        QVERIFY(p.assignments.isEmpty());
        QCOMPARE(p.calendars.size(), 1);
        QCOMPARE_AT(p.tasks.at(1).finish, at(2, 17));
    }

    void atomicSaveAndLoad()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("gala.qplan"));
        const Builder b = sample();
        QString error;
        QVERIFY(savePlanFile(path, b.p, &error));
        Project back;
        const LoadResult r = loadPlanFile(path, &back);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(back.tasks.size(), b.p.tasks.size());
        QVERIFY(!savePlanFile(dir.filePath(QStringLiteral("missing/dir/x.qplan")), b.p, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!loadPlanFile(dir.filePath(QStringLiteral("nope.qplan")), &back).ok);
    }

    void clipboard()
    {
        Builder b = sample();
        Project &p = b.p;
        const QByteArray data = copyTasks(p, {0}); // the whole Planning phase
        QVERIFY(!data.isEmpty());
        const int before = int(p.tasks.size());
        const QList<int> rows = pasteTasks(p, int(p.tasks.size()), data);
        schedule(p);
        QCOMPARE(rows.size(), 4);
        QCOMPARE(p.tasks.size(), before + 4);
        const Task &copyOfTasting = p.tasks.at(rows.at(2));
        QCOMPARE(copyOfTasting.name, QStringLiteral("Caterer tasting"));
        QCOMPARE(copyOfTasting.predecessors.size(), 1);
        QCOMPARE(copyOfTasting.predecessors.first().predecessorUid, p.tasks.at(rows.at(1)).uid); // remapped
        QVERIFY(p.tasks.at(rows.at(0)).summary);
        QCOMPARE(p.tasks.at(rows.at(1)).percentComplete, 0);                                  // progress not copied
        QCOMPARE(p.resources.size(), 2);                                                      // found by name
        QCOMPARE(p.assignmentsOfTask(p.tasks.at(rows.at(1)).uid).size(), 1);
        QVERIFY(pasteTasks(p, 0, QByteArrayLiteral("{\"format\":\"nope\"}")).isEmpty());
        const QString tsv = copyTasksText(p, {1}, QLocale::c());
        QVERIFY(tsv.startsWith(QStringLiteral("ID\tTask Name\tDuration")));
        QVERIFY(tsv.contains(QStringLiteral("Book venue\t3 days")));
    }

    void csv()
    {
        const Builder b = sample();
        const QByteArray csv = taskSheetCsv(b.p, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("duration"),
                                                  QStringLiteral("predecessors"), QStringLiteral("notes")},
                                            QLocale::c());
        const QStringList lines = QString::fromUtf8(csv).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 5);
        QCOMPARE(lines.at(0), QStringLiteral("ID,Task Name,Duration,Predecessors,Notes"));
        QCOMPARE(lines.at(2), QStringLiteral("2,  Book venue,3 days,,Ballroom or the botanical garden"));
        QCOMPARE(lines.at(3), QStringLiteral("3,  Caterer tasting,4 hrs?,2SS+1 day,"));
        QCOMPARE(lines.at(4), QStringLiteral("4,  Venue signed,0 days,2FS+10%,"));
    }
};

QTEST_GUILESS_MAIN(PlanFileTest)

#include "tst_plan_file.moc"
