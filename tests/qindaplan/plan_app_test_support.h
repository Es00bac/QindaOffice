// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Shared fixture for the QindaPlan app tests: "Spring fundraiser gala", a
// realistic plan for a nonprofit's spring gala — 4 phases, 36 subtasks, 7
// milestones, 6 resources (5 people and card stock), a staff day off, one
// overallocation (Marcus plans the menu and rents the AV on the same April
// days), a baseline saved at the start and the slippage since (sponsor
// outreach runs three days long, so the gala is three days later than
// planned; the venue contract came a day late), progress
// through the status date Friday 27 March 2026. "Today" is pinned to
// Monday 30 March 2026 so every date the tests and grabs show is fixed.

#include "qindaplan/src/plan_document.h"
#include "qindaplan/src/plan_host.h"
#include "qindaplan/src/plan_service.h"

#include "baselines.h"
#include "plan_file.h"
#include "plan_time.h"
#include "scheduler.h"

#include <QGuiApplication>
#include <QHash>
#include <QTemporaryDir>
#include <QtTest>

namespace PlanAppTest {

namespace Engine = ::QindaOffice::Plan;
using QindaOffice::Apps::Plan::PlanDocument;
using QindaOffice::Apps::Plan::PlanHost;
using QindaOffice::Apps::Plan::PlanService;

inline const QString kToday = QStringLiteral("2026-03-30");
inline constexpr Engine::Minutes kDay = 480;

inline QString s(const char *text)
{
    return QString::fromUtf8(text);
}

inline bool ok(const QVariantMap &result)
{
    return result.value(QStringLiteral("ok")).toBool();
}

struct GalaBuilder {
    Engine::Project p = Engine::newProject(QDate(2026, 3, 2), s("Spring fundraiser gala"));
    QHash<QString, int> uid; // task name → uid
    QHash<QString, int> res; // resource name → uid

    int task(const char *name, double days, int level = 2)
    {
        Engine::Task t;
        t.uid = p.nextTaskUid();
        // Name-based ids, so row ids (and the structural fixtures that list
        // them) are the same on every run.
        t.guid = QUuid::createUuidV5(QUuid(QStringLiteral("{6f1d3c2e-5a4b-4c8d-9e7f-0a1b2c3d4e5f}")), s(name));
        t.name = s(name);
        t.duration = Engine::Minutes(days * kDay);
        t.durationUnit = days < 1 && days > 0 ? Engine::DurationUnit::Hours : Engine::DurationUnit::Days;
        t.outlineLevel = level;
        p.tasks.append(t);
        uid.insert(t.name, t.uid);
        return t.uid;
    }
    Engine::Task &byName(const char *name) { return p.tasks[p.taskIndex(uid.value(s(name)))]; }
    void link(const char *successor, const char *predecessor, Engine::LinkType type = Engine::LinkType::FinishToStart,
              double lagDays = 0)
    {
        Engine::Link l;
        l.predecessorUid = uid.value(s(predecessor));
        l.type = type;
        l.lag = Engine::Minutes(lagDays * kDay);
        byName(successor).predecessors.append(l);
    }
    void person(const char *name, const char *initials, const char *group, qint64 ratePerHour, int maxUnits = 100)
    {
        Engine::Resource r;
        r.uid = p.nextResourceUid();
        r.name = s(name);
        r.initials = s(initials);
        r.group = s(group);
        r.standardRate = ratePerHour;
        r.maxUnits = maxUnits;
        r.email = s(initials).toLower() + s("@riverside-arts.org");
        p.resources.append(r);
        res.insert(r.name, r.uid);
    }
    void assign(const char *task, const char *resource, int units = 100)
    {
        Engine::Assignment a;
        a.uid = p.nextAssignmentUid();
        a.taskUid = uid.value(s(task));
        a.resourceUid = res.value(s(resource));
        a.units = units;
        p.assignments.append(a);
    }
    void done(const char *name, int percent)
    {
        Engine::Task &t = byName(name);
        t.percentComplete = percent;
        t.actualStart = t.start;
        if (percent >= 100) {
            t.actualFinish = t.finish;
        }
    }
};

// The plan as it stands on Monday 30 March 2026 (see the header comment).
inline Engine::Project galaProject()
{
    GalaBuilder b;
    Engine::Project &p = b.p;
    p.info.manager = s("Dana Whitfield");
    p.info.company = s("Riverside Arts Collective");
    p.info.notes = s("Our spring benefit: dinner, a silent auction and the scholarship appeal.");
    p.calendars[0].exceptions.append({QDate(2026, 4, 10), QDate(2026, 4, 10), s("Staff day"), {}});

    b.person("Dana Whitfield", "DW", "Management", 5500);
    b.person("Priya Nair", "PN", "Development", 4800);
    b.person("Marcus Bell", "MB", "Operations", 4500);
    b.person("Lena Ortiz", "LO", "Design", 5000);
    b.person("Sam Chen", "SC", "Volunteers", 3200);
    Engine::Resource paper;
    paper.uid = p.nextResourceUid();
    paper.name = s("Card stock");
    paper.type = Engine::ResourceType::Material;
    paper.materialLabel = s("sheets");
    paper.standardRate = 35;
    p.resources.append(paper);
    b.res.insert(paper.name, paper.uid);

    b.task("Planning", 0, 1);
    b.task("Kickoff meeting", 1);
    b.task("Set budget and goals", 3);
    b.task("Book the venue", 5);
    b.task("Caterer tastings", 4);
    b.task("Form host committee", 3);
    b.task("Venue and date confirmed", 0);
    b.task("Sponsorship and marketing", 0, 1);
    b.task("Build sponsor prospect list", 4);
    b.task("Sponsor outreach", 36);
    b.task("Sponsors confirmed", 0);
    b.task("Design invitations", 5);
    b.task("Print invitations", 3);
    b.task("Mail invitations", 2);
    b.task("Launch ticket page", 3);
    b.task("Social media campaign", 20);
    b.task("Press release", 2);
    b.task("Invitations out", 0);
    b.task("Sponsor program booklet", 4);
    b.task("Program and logistics", 0, 1);
    b.task("Recruit auction donations", 25);
    b.task("Book entertainment", 4);
    b.task("Plan menu with caterer", 3);
    b.task("Rent AV equipment", 3);
    b.task("Floor plan and seating", 4);
    b.task("Recruit volunteers", 10);
    b.task("Order décor and flowers", 3);
    b.task("Write run of show", 3);
    b.task("Volunteer training", 1);
    b.task("Final headcount to caterer", 1);
    b.task("Logistics locked", 0);
    b.task("Gala and wrap-up", 0, 1);
    b.task("Venue setup", 1);
    b.task("Rehearsal", 1);
    b.task("Gala night", 0);
    b.task("Teardown", 0.5);
    b.task("Thank-you letters", 3);
    b.task("Reconcile budget", 2);
    b.task("Post-event report", 2);
    b.task("Project closed", 0);

    using LT = Engine::LinkType;
    b.link("Set budget and goals", "Kickoff meeting");
    b.link("Book the venue", "Set budget and goals");
    b.link("Caterer tastings", "Book the venue", LT::StartToStart, 2);
    b.link("Form host committee", "Kickoff meeting");
    b.link("Venue and date confirmed", "Book the venue");
    b.link("Venue and date confirmed", "Caterer tastings");
    b.link("Build sponsor prospect list", "Set budget and goals");
    b.link("Sponsor outreach", "Build sponsor prospect list");
    b.link("Sponsors confirmed", "Sponsor outreach");
    b.link("Design invitations", "Venue and date confirmed");
    b.link("Print invitations", "Design invitations");
    b.link("Mail invitations", "Print invitations");
    b.link("Launch ticket page", "Design invitations");
    b.link("Social media campaign", "Launch ticket page", LT::StartToStart);
    b.link("Sponsor program booklet", "Sponsors confirmed");
    b.link("Press release", "Sponsors confirmed");
    b.link("Invitations out", "Mail invitations");
    b.link("Recruit auction donations", "Form host committee");
    b.link("Book entertainment", "Venue and date confirmed");
    b.link("Plan menu with caterer", "Venue and date confirmed", LT::FinishToStart, 15);
    b.link("Rent AV equipment", "Book entertainment", LT::FinishToStart, 12);
    b.link("Floor plan and seating", "Plan menu with caterer");
    b.link("Recruit volunteers", "Recruit auction donations");
    b.link("Order décor and flowers", "Floor plan and seating");
    b.link("Write run of show", "Book entertainment");
    b.link("Write run of show", "Floor plan and seating");
    b.link("Volunteer training", "Recruit volunteers");
    b.link("Volunteer training", "Write run of show");
    b.link("Final headcount to caterer", "Invitations out", LT::FinishToStart, 10);
    b.link("Logistics locked", "Volunteer training");
    b.link("Logistics locked", "Final headcount to caterer");
    b.link("Logistics locked", "Order décor and flowers");
    b.link("Logistics locked", "Rent AV equipment");
    b.link("Venue setup", "Logistics locked");
    b.link("Venue setup", "Sponsor program booklet");
    b.link("Rehearsal", "Venue setup");
    b.link("Gala night", "Rehearsal");
    b.link("Teardown", "Gala night");
    b.link("Thank-you letters", "Teardown");
    b.link("Reconcile budget", "Teardown", LT::FinishToStart, 3);
    b.link("Post-event report", "Reconcile budget");
    b.link("Post-event report", "Thank-you letters");
    b.link("Project closed", "Post-event report");
    // The ballroom is ours on Friday 15 May: the gala must not slip past it.
    b.byName("Gala night").constraint = Engine::ConstraintType::FinishNoLaterThan;
    b.byName("Gala night").constraintDate = Engine::toMinutes(QDate(2026, 5, 15), 17 * 60);
    b.byName("Sponsor outreach").notes = s("Tiered asks: presenting $10k, gold $5k, table $2.5k.");

    b.assign("Kickoff meeting", "Dana Whitfield");
    b.assign("Kickoff meeting", "Priya Nair");
    b.assign("Set budget and goals", "Dana Whitfield", 50);
    b.assign("Set budget and goals", "Priya Nair");
    b.assign("Book the venue", "Marcus Bell");
    b.assign("Caterer tastings", "Dana Whitfield", 50);
    b.assign("Form host committee", "Dana Whitfield", 50);
    b.assign("Build sponsor prospect list", "Priya Nair");
    b.assign("Sponsor outreach", "Priya Nair", 75);
    b.assign("Design invitations", "Lena Ortiz");
    b.assign("Print invitations", "Lena Ortiz", 50);
    b.assign("Print invitations", "Card stock", 30000);
    b.assign("Mail invitations", "Sam Chen", 50);
    b.assign("Launch ticket page", "Lena Ortiz", 50);
    b.assign("Social media campaign", "Priya Nair", 25);
    b.assign("Sponsor program booklet", "Lena Ortiz");
    b.assign("Press release", "Priya Nair", 25);
    b.assign("Recruit auction donations", "Sam Chen", 50);
    b.assign("Book entertainment", "Marcus Bell");
    b.assign("Plan menu with caterer", "Marcus Bell");
    b.assign("Rent AV equipment", "Marcus Bell");
    b.assign("Floor plan and seating", "Dana Whitfield", 50);
    b.assign("Recruit volunteers", "Sam Chen");
    b.assign("Order décor and flowers", "Lena Ortiz", 50);
    b.assign("Write run of show", "Dana Whitfield");
    b.assign("Volunteer training", "Sam Chen");
    b.assign("Final headcount to caterer", "Marcus Bell", 25);
    b.assign("Venue setup", "Marcus Bell");
    b.assign("Venue setup", "Sam Chen");
    b.assign("Venue setup", "Lena Ortiz");
    b.assign("Rehearsal", "Dana Whitfield");
    b.assign("Teardown", "Sam Chen");
    b.assign("Thank-you letters", "Priya Nair", 50);
    b.assign("Reconcile budget", "Dana Whitfield", 50);
    b.assign("Post-event report", "Dana Whitfield", 50);
    for (Engine::Task &t : p.tasks) {
        t.fixedCost = 0;
    }
    b.byName("Book the venue").fixedCost = 450000;          // venue deposit
    b.byName("Book entertainment").fixedCost = 180000;      // band
    b.byName("Rent AV equipment").fixedCost = 95000;
    b.byName("Order décor and flowers").fixedCost = 120000;

    // The plan as agreed on day one is the baseline.
    Engine::schedule(p);
    Engine::setBaseline(p, 0);

    // Then real life: outreach ran three days long, and progress through
    // Friday 27 March.
    b.byName("Sponsor outreach").duration += 3 * kDay;
    Engine::schedule(p);
    for (const char *name : {"Kickoff meeting", "Set budget and goals", "Form host committee", "Book the venue",
                             "Build sponsor prospect list", "Caterer tastings", "Venue and date confirmed"}) {
        b.done(name, 100);
    }
    b.byName("Book the venue").actualFinish = Engine::toMinutes(QDate(2026, 3, 13), 17 * 60); // a day late
    Engine::schedule(p);
    for (const char *name : {"Design invitations", "Book entertainment"}) {
        b.done(name, 100);
    }
    b.done("Sponsor outreach", 30);
    b.done("Launch ticket page", 100);
    b.done("Print invitations", 60);
    b.done("Social media campaign", 15);
    b.done("Recruit auction donations", 40);
    p.info.statusDate = Engine::toMinutes(QDate(2026, 3, 27), 17 * 60);
    Engine::schedule(p);
    return p;
}

// Writes the gala to `dir`/gala.qplan and returns the path.
inline QString writeGala(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(s("Spring fundraiser gala.qplan"));
    QString error;
    if (!Engine::savePlanFile(path, galaProject(), &error)) {
        qWarning("gala not written: %s", qPrintable(error));
    }
    return path;
}

} // namespace PlanAppTest

#define PLAN_APP_TEST_MAIN(TestClass)                                                      \
    int main(int argc, char *argv[])                                                       \
    {                                                                                      \
        qputenv("QT_QPA_PLATFORM", "offscreen");                                           \
        qputenv("QT_QUICK_BACKEND", "software");                                           \
        QTemporaryDir home;                                                                \
        qputenv("XDG_CONFIG_HOME", home.filePath(QStringLiteral("config")).toUtf8());      \
        qputenv("XDG_STATE_HOME", home.filePath(QStringLiteral("state")).toUtf8());        \
        qputenv("XDG_DATA_HOME", home.filePath(QStringLiteral("data")).toUtf8());          \
        qputenv("XDG_CACHE_HOME", home.filePath(QStringLiteral("cache")).toUtf8());        \
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));             \
        QGuiApplication app(argc, argv);                                                   \
        QGuiApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaPlan"));      \
        QGuiApplication::setApplicationDisplayName(QStringLiteral("QindaPlan"));           \
        TestClass test;                                                                    \
        return QTest::qExec(&test, argc, argv);                                            \
    }
