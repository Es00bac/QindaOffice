// SPDX-License-Identifier: GPL-3.0-or-later
// Editing through the sheets and the ribbon operations (plan_edit.h,
// outline.h): field text both ways, Project's typing semantics, the
// predecessor and resource-name mini-languages, outline edits, linking,
// progress, manual/inactive tasks, Gantt moves and "Update Project".

#include "outline.h"
#include "plan_edit.h"
#include "plan_fixture.h"

using namespace PlanTest;

namespace {
const QLocale kC = QLocale::c();

EditResult set(Project &p, int i, const char *field, const QString &text)
{
    const EditResult r = setTaskField(p, i, QString::fromLatin1(field), text, kC);
    if (r.ok) {
        schedule(p);
    }
    return r;
}
QString text(const Project &p, int i, const char *field)
{
    return taskFieldText(p, i, QString::fromLatin1(field), kC);
}
} // namespace

class PlanEditTest final : public QObject {
    Q_OBJECT
private slots:
    void fieldsRoundTripAsText()
    {
        Builder b;
        const int a = b.task(QStringLiteral("Design"), 3 * kDay);
        b.run();
        Project &p = b.p;
        QCOMPARE(text(p, a, "duration"), QStringLiteral("3 days"));
        QCOMPARE(text(p, a, "start"), QStringLiteral("Mon 2 Mar 26"));
        QCOMPARE(text(p, a, "finish"), QStringLiteral("Wed 4 Mar 26"));
        QCOMPARE(text(p, a, "mode"), QStringLiteral("Auto"));
        QCOMPARE(text(p, a, "constraint"), QStringLiteral("As Soon As Possible"));
        QCOMPARE(text(p, a, "baselineStart"), QStringLiteral("NA"));
        QVERIFY(set(p, a, "duration", QStringLiteral("2w?")).ok);
        QCOMPARE(text(p, a, "duration"), QStringLiteral("2 wks?"));
        QCOMPARE(p.tasks.at(a).duration, 2400 * 2);
        QVERIFY(set(p, a, "duration", QStringLiteral("12h")).ok);
        QCOMPARE(text(p, a, "duration"), QStringLiteral("12 hrs"));
        QCOMPARE_AT(p.tasks.at(a).finish, at(3, 12));
        const EditResult bad = set(p, a, "duration", QStringLiteral("a while"));
        QVERIFY(!bad.ok);
        QVERIFY(bad.error.contains(QStringLiteral("not a duration")));
        // M83 (D-092): Work is typed now; a task without resources keeps it.
        QVERIFY(set(p, a, "work", QStringLiteral("8h")).ok);
        QCOMPARE(p.tasks.at(a).unassignedWork, 8 * 60);
        QVERIFY(set(p, a, "name", QStringLiteral("  Venue walkthrough ")).ok);
        QCOMPARE(p.tasks.at(a).name, QStringLiteral("Venue walkthrough"));
        QVERIFY(set(p, a, "notes", QStringLiteral("Bring the floor plan\nand a tape measure")).ok);
        QCOMPARE(text(p, a, "notes"), QStringLiteral("Bring the floor plan"));
        QCOMPARE(taskFieldEditText(p, a, QStringLiteral("notes"), kC), p.tasks.at(a).notes);
        QVERIFY(set(p, a, "fixedCost", QStringLiteral("$1,250.00")).ok);
        QCOMPARE(p.tasks.at(a).fixedCost, 125000);
        QCOMPARE(text(p, a, "cost"), QStringLiteral("$1,250.00"));
        QVERIFY(taskFieldIds().contains(QStringLiteral("finishVariance")));
        QVERIFY(!isTaskFieldEditable(QStringLiteral("totalSlack")));
    }

    void typingDatesSetsConstraintsLikeProject()
    {
        Builder b;
        const int a = b.task(QStringLiteral("A"), kDay);
        const int m = b.task(QStringLiteral("M"), 2 * kDay);
        b.run();
        Project &p = b.p;
        QVERIFY(set(p, a, "start", QStringLiteral("Wed 4 Mar 26")).ok);
        QCOMPARE(p.tasks.at(a).constraint, ConstraintType::StartNoEarlierThan);
        QCOMPARE_AT(p.tasks.at(a).start, at(4));
        QVERIFY(set(p, a, "finish", QStringLiteral("2026-03-10")).ok);
        QCOMPARE(p.tasks.at(a).constraint, ConstraintType::FinishNoEarlierThan);
        QCOMPARE_AT(p.tasks.at(a).finish, at(10, 17));
        QVERIFY(set(p, a, "constraint", QStringLiteral("ASAP")).ok);
        QCOMPARE(p.tasks.at(a).constraintDate, kNoDate);
        QVERIFY(set(p, a, "constraintDate", QStringLiteral("9 Mar 2026")).ok); // ASAP + date → SNET
        QCOMPARE(p.tasks.at(a).constraint, ConstraintType::StartNoEarlierThan);
        QVERIFY(set(p, a, "constraint", QStringLiteral("Must Finish On")).ok);
        QCOMPARE(text(p, a, "constraint"), QStringLiteral("Must Finish On"));
        QVERIFY(!set(p, a, "constraint", QStringLiteral("Whenever")).ok);
        // Manual tasks keep what is typed.
        QVERIFY(set(p, m, "mode", QStringLiteral("Manual")).ok);
        QVERIFY(set(p, m, "start", QStringLiteral("Mon 16 Mar 26")).ok);
        QCOMPARE_AT(p.tasks.at(m).start, at(16));
        QCOMPARE_AT(p.tasks.at(m).finish, at(17, 17));
        QVERIFY(set(p, m, "finish", QStringLiteral("Thu 19 Mar 26")).ok);
        QCOMPARE(p.tasks.at(m).duration, 4 * kDay);
        QVERIFY(!set(p, m, "finish", QStringLiteral("Fri 13 Mar 26")).ok);
    }

    void predecessorsLanguage()
    {
        Builder b;
        for (const char *name : {"A", "B", "C", "D"}) {
            b.task(QString::fromLatin1(name), kDay);
        }
        b.run();
        Project &p = b.p;
        QVERIFY(set(p, 3, "predecessors", QStringLiteral("1, 2SS+2d; 3FF-1 day")).ok);
        QCOMPARE(p.tasks.at(3).predecessors.size(), 3);
        QCOMPARE(p.tasks.at(3).predecessors.at(1).type, LinkType::StartToStart);
        QCOMPARE(p.tasks.at(3).predecessors.at(1).lag, 2 * kDay);
        QCOMPARE(p.tasks.at(3).predecessors.at(2).lag, -kDay);
        QCOMPARE(text(p, 3, "predecessors"), QStringLiteral("1, 2SS+2 days, 3FF-1 day"));
        QCOMPARE(text(p, 0, "successors"), QStringLiteral("4"));
        QVERIFY(set(p, 2, "predecessors", QStringLiteral("1FS+50%")).ok);
        QCOMPARE(*p.tasks.at(2).predecessors.at(0).lagPercent, 50);
        QCOMPARE(text(p, 2, "predecessors"), QStringLiteral("1FS+50%"));
        EditResult r = set(p, 0, "predecessors", QStringLiteral("4"));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("circular")));
        QVERIFY(!set(p, 0, "predecessors", QStringLiteral("9")).ok);
        QVERIFY(!set(p, 0, "predecessors", QStringLiteral("1")).ok);
        QVERIFY(!set(p, 1, "predecessors", QStringLiteral("1XX")).ok);
        QVERIFY(!set(p, 1, "predecessors", QStringLiteral("1, 1SS")).ok); // twice
        QVERIFY(set(p, 3, "predecessors", QString()).ok);
        QVERIFY(p.tasks.at(3).predecessors.isEmpty());
    }

    void resourceNamesLanguage()
    {
        Builder b;
        const int a = b.task(QStringLiteral("Print invitations"), 2 * kDay);
        const int ana = b.resource(QStringLiteral("Ana Ruiz"), 4500);
        b.resource(QStringLiteral("Card stock"), 35, 100, ResourceType::Material);
        b.p.resources.last().materialLabel = QStringLiteral("sheets");
        b.run();
        Project &p = b.p;
        QVERIFY(set(p, a, "resources", QStringLiteral("ana ruiz[50%], Card stock[200 sheets], Ben Okafor")).ok);
        QCOMPARE(p.resources.size(), 3); // Ben was created
        QCOMPARE(p.assignments.size(), 3);
        QCOMPARE(p.assignments.at(p.assignmentIndex(p.tasks.at(a).uid, ana)).units, 50);
        QCOMPARE(text(p, a, "resources"), QStringLiteral("Ana Ruiz[50%], Card stock[200 sheets], Ben Okafor"));
        QCOMPARE(p.tasks.at(a).work, 480 + 960);
        QVERIFY(set(p, a, "resources", QStringLiteral("Ben Okafor[0.5]")).ok); // a fraction is units
        QCOMPARE(p.assignments.size(), 1);
        QCOMPARE(p.assignments.first().units, 50);
        QVERIFY(!set(p, a, "resources", QStringLiteral("Ben Okafor[lots]")).ok);
        QVERIFY(!set(p, a, "resources", QStringLiteral("Ben Okafor, Ben Okafor")).ok);
        QCOMPARE(resourceFieldText(p, 0, QStringLiteral("standardRate"), kC), QStringLiteral("$45.00/hr"));
        QVERIFY(setResourceField(p, 0, QStringLiteral("standardRate"), QStringLiteral("400/d"), kC).ok);
        QCOMPARE(p.resources.at(0).standardRate, 5000);
        QVERIFY(setResourceField(p, 0, QStringLiteral("maxUnits"), QStringLiteral("50%"), kC).ok);
        QCOMPARE(p.resources.at(0).maxUnits, 50);
        QVERIFY(!setResourceField(p, 0, QStringLiteral("name"), QStringLiteral("Ben Okafor"), kC).ok);
        QVERIFY(setResourceField(p, 0, QStringLiteral("name"), QStringLiteral("Ana Ruiz-Soto"), kC).ok);
        QCOMPARE(p.resources.at(0).initials, QStringLiteral("AR"));
        QVERIFY(!setResourceField(p, 0, QStringLiteral("type"), QStringLiteral("Robot"), kC).ok);
    }

    void outlineEdits()
    {
        Builder b;
        for (const char *name : {"A", "B", "C", "D"}) {
            b.task(QString::fromLatin1(name), kDay);
        }
        b.run();
        Project &p = b.p;
        QString error;
        QVERIFY(!indentTasks(p, {0}, &error));
        QVERIFY(indentTasks(p, {1, 2}, &error));
        QVERIFY(p.tasks.at(0).summary);
        QCOMPARE(p.tasks.at(2).wbs, QStringLiteral("1.2"));
        QVERIFY(!indentTasks(p, {1}, &error) || p.tasks.at(1).outlineLevel == 3);
        computeStructure(p);
        const int s = insertSummaryAbove(p, {3}, QStringLiteral("Wrap-up"));
        QCOMPARE(s, 3);
        QVERIFY(p.tasks.at(3).summary);
        QCOMPARE(p.tasks.at(4).name, QStringLiteral("D"));
        QCOMPARE(p.tasks.at(4).outlineLevel, 2);
        // Move D's summary up above task A's group? It is the last top-level
        // block; moving up swaps it with the A block.
        const int moved = moveTask(p, 3, true);
        QCOMPARE(moved, 0);
        QCOMPARE(p.tasks.at(0).name, QStringLiteral("Wrap-up"));
        QCOMPARE(moveTask(p, 0, true), -1);
        QVERIFY(outdentTasks(p, {1}, &error)); // D back to the top
        QVERIFY(!p.tasks.at(0).summary);
        const int added = addTask(p, 2, NewTaskKind::Summary, QStringLiteral("Phase"));
        QVERIFY(p.tasks.at(added).summary);
        QCOMPARE(p.tasks.at(added + 1).name, QStringLiteral("New subtask"));
        const int milestone = addTask(p, 0, NewTaskKind::Milestone, QStringLiteral("Kickoff"));
        schedule(p);
        QVERIFY(p.tasks.at(milestone).isMilestone());
        const int before = int(p.tasks.size());
        const QList<int> removed = removeTasks(p, {added + 1}); // the milestone went in above it
        QCOMPARE(removed.size(), 2);
        QCOMPARE(p.tasks.size(), before - 2);
    }

    void linkingProgressAndModes()
    {
        Builder b;
        const int a = b.task(QStringLiteral("A"), 2 * kDay);
        const int bb = b.task(QStringLiteral("B"), 2 * kDay);
        const int c = b.task(QStringLiteral("C"), 2 * kDay);
        b.run();
        Project &p = b.p;
        QVERIFY(linkTasks(p, {c, a, bb}).ok);
        schedule(p);
        QCOMPARE_AT(p.tasks.at(c).start, at(6));
        QVERIFY(!linkTasks(p, {a}).ok);
        QCOMPARE(unlinkTasks(p, {bb}), 2); // one task: all its links
        schedule(p);
        QCOMPARE_AT(p.tasks.at(c).start, at(2));
        QVERIFY(linkTasks(p, {a, bb, c}).ok);
        QCOMPARE(unlinkTasks(p, {a, c}), 0); // not linked to each other
        schedule(p);
        setPercentComplete(p, {a}, 50);
        QCOMPARE_AT(p.tasks.at(a).actualStart, at(2));
        setPercentComplete(p, {a}, 100);
        QCOMPARE_AT(p.tasks.at(a).actualFinish, at(3, 17));
        setPercentComplete(p, {a}, 0);
        QCOMPARE(p.tasks.at(a).actualStart, kNoDate);
        setManual(p, {c}, true);
        QCOMPARE_AT(p.tasks.at(c).manualStart, at(6));
        QVERIFY(moveTaskStart(p, c, at(16, 10)).ok);
        schedule(p);
        QCOMPARE_AT(p.tasks.at(c).start, at(16, 10));
        respectLinks(p, {c});
        schedule(p);
        QCOMPARE_AT(p.tasks.at(c).start, at(6)); // back behind B
        QVERIFY(moveTaskStart(p, bb, at(11)).ok);
        schedule(p);
        QCOMPARE(p.tasks.at(bb).constraint, ConstraintType::StartNoEarlierThan);
        QCOMPARE_AT(p.tasks.at(bb).start, at(11));
        respectLinks(p, {bb});
        schedule(p);
        QCOMPARE_AT(p.tasks.at(bb).start, at(4));
        setActive(p, {bb}, false);
        schedule(p);
        QVERIFY(!p.tasks.at(bb).effectiveActive);
        QCOMPARE(text(p, bb, "active"), QStringLiteral("No"));
        setActive(p, {bb}, true);
        schedule(p);
        // Status date Wed 4 Mar 17:00: A done, B half-way (started Wed).
        updateProgress(p, at(4, 17));
        schedule(p);
        QCOMPARE(p.tasks.at(a).percentComplete, 100);
        QCOMPARE(p.tasks.at(bb).percentComplete, 50);
        QCOMPARE(p.tasks.at(c).percentComplete, 0);
    }

    void assignmentsAndResourceCalendars()
    {
        Builder b;
        const int a = b.task(QStringLiteral("A"), kDay);
        const int s = b.task(QStringLiteral("S"), 0);
        b.task(QStringLiteral("S1"), kDay, 2);
        b.run();
        Project &p = b.p;
        const int r = addResource(p, 0, QStringLiteral("Crew"));
        QCOMPARE(addResource(p, 1, QStringLiteral("Crew")), 1);
        QCOMPARE(p.resources.at(1).name, QStringLiteral("Crew 2"));
        const int uid = p.resources.at(r).uid;
        QVERIFY(assignResource(p, a, uid, 50).ok);
        QVERIFY(assignResource(p, a, uid, 75).ok); // updates
        QCOMPARE(p.assignments.size(), 1);
        QCOMPARE(p.assignments.first().units, 75);
        QVERIFY(!assignResource(p, s, uid, 100).ok);
        QVERIFY(unassignResource(p, a, uid).ok);
        QVERIFY(!unassignResource(p, a, uid).ok);
        QVERIFY(assignResource(p, a, uid, 100).ok);
        const int cal = ensureResourceCalendar(p, r);
        QCOMPARE(ensureResourceCalendar(p, r), cal);
        QCOMPARE(p.calendars.at(p.calendarIndex(cal)).baseUid, p.info.calendarUid);
        removeResources(p, {r});
        QVERIFY(p.assignments.isEmpty());
    }
};

QTEST_GUILESS_MAIN(PlanEditTest)

#include "tst_plan_edit.moc"
