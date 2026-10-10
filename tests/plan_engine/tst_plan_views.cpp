// SPDX-License-Identifier: GPL-3.0-or-later
// M79 (D-088): what Project's views are made of, in the engine — the task
// filters and sorts (plan_views.h), the Network Diagram's layout
// (network_layout.h), the Task Usage table (resources.h), the Cost and Work
// Overview reports, cost resources, and baselines 0–10 with the
// assignments' own.
// Standard calendar from Monday 2 March 2026.

#include "baselines.h"
#include "network_layout.h"
#include "plan_fixture.h"
#include "plan_edit.h"
#include "plan_file.h"
#include "plan_reports.h"
#include "plan_views.h"
#include "resources.h"

using namespace PlanTest;

namespace {

// Phase (summary) → Brief 2d (done), Draft 3d → Review 1d (milestone after
// it: Signed); Print 2d alone with a deadline; Ana on Draft and Print.
struct Plan {
    Builder b;
    int phase = 0, brief = 0, draft = 0, review = 0, signedOff = 0, print = 0;
    int ana = 0;
    Plan()
    {
        phase = b.task(QStringLiteral("Phase"), 0, 1);
        brief = b.task(QStringLiteral("Brief"), 2 * kDay, 2);
        draft = b.task(QStringLiteral("Draft"), 3 * kDay, 2);
        review = b.task(QStringLiteral("Review"), kDay, 2);
        signedOff = b.task(QStringLiteral("Signed"), 0, 2);
        print = b.task(QStringLiteral("Print"), 2 * kDay, 1);
        b.link(draft, brief);
        b.link(review, draft);
        b.link(signedOff, review);
        ana = b.resource(QStringLiteral("Ana"), 5000);
        b.assign(draft, ana);
        b.assign(print, ana, 50);
        b.p.tasks[brief].percentComplete = 100;
        b.p.tasks[brief].actualStart = at(2);
        b.p.tasks[brief].actualFinish = at(3, 17);
        b.p.tasks[print].deadline = at(6, 17);
        b.p.tasks[print].priority = 800;
        b.run();
    }
    QList<int> all() const
    {
        QList<int> out;
        for (int i = 0; i < b.p.tasks.size(); ++i) {
            out.append(i);
        }
        return out;
    }
    QStringList names(const QList<int> &rows) const
    {
        QStringList out;
        for (int i : rows) {
            out.append(b.t(i).name);
        }
        return out;
    }
    QStringList filtered(const QString &id, bool summaries = true) const
    {
        TaskFilter f;
        f.id = id;
        f.resourceUid = ana;
        f.from = QDate(2026, 3, 5);
        f.to = QDate(2026, 3, 5);
        return names(filterRows(b.p, all(), f, at(4, 17), summaries));
    }
};

QStringList strings(std::initializer_list<const char *> list)
{
    QStringList out;
    for (const char *s : list) {
        out.append(QString::fromUtf8(s));
    }
    return out;
}

} // namespace

class PlanViewsTest final : public QObject {
    Q_OBJECT
private slots:
    void filters()
    {
        const Plan x;
        QCOMPARE(taskFilterIds().size(), 12); // M83 added Custom Field…
        QCOMPARE(taskFilterTitle(QStringLiteral("deadlines")), QStringLiteral("Tasks With Deadlines"));
        QCOMPARE(x.filtered(QStringLiteral("all")).size(), 6);
        QCOMPARE(x.filtered(QStringLiteral("completed")), strings({"Phase", "Brief"}));
        QCOMPARE(x.filtered(QStringLiteral("completed"), false), strings({"Brief"}));
        QCOMPARE(x.filtered(QStringLiteral("incomplete")), strings({"Phase", "Draft", "Review", "Signed", "Print"}));
        QCOMPARE(x.filtered(QStringLiteral("milestones")), strings({"Phase", "Signed"}));
        QCOMPARE(x.filtered(QStringLiteral("summary")), strings({"Phase"}));
        QCOMPARE(x.filtered(QStringLiteral("deadlines")), strings({"Print"}));
        QCOMPARE(x.filtered(QStringLiteral("resource")), strings({"Phase", "Draft", "Print"}));
        // Brief → Draft → Review → Signed ends the project (Mon 9 Mar): the
        // unfinished part of that chain is critical; Brief is done.
        QCOMPARE(x.filtered(QStringLiteral("critical")), strings({"Phase", "Draft", "Review", "Signed"}));
        // Working on Thursday 5 March: Draft (Wed–Fri) and its Phase; not
        // Print (Mon–Tue).
        QCOMPARE(x.filtered(QStringLiteral("dateRange")), strings({"Phase", "Draft"}));
        // Late as of Wed 4 Mar 17:00: Draft should be a third done; Print too.
        QCOMPARE(x.filtered(QStringLiteral("late")), strings({"Phase", "Draft", "Print"}));
        // Slipping needs a baseline: none yet, then Draft one day longer.
        QVERIFY(x.filtered(QStringLiteral("slipping")).isEmpty());
        Plan y;
        setBaseline(y.b.p, 0);
        y.b.p.tasks[y.draft].duration += kDay;
        y.b.run();
        QCOMPARE(y.filtered(QStringLiteral("slipping")), strings({"Phase", "Draft", "Review", "Signed"}));
    }

    void sortsKeepTheOutline()
    {
        Plan x;
        QCOMPARE(taskSortIds(), strings({"id", "start", "finish", "priority", "cost"}));
        QCOMPARE(x.names(sortedOutline(x.b.p, QStringLiteral("id"))),
                 strings({"Phase", "Brief", "Draft", "Review", "Signed", "Print"}));
        // Finish: Print (Tue 3) before Phase (Tue 10); inside Phase by finish.
        QCOMPARE(x.names(sortedOutline(x.b.p, QStringLiteral("finish"))),
                 strings({"Print", "Phase", "Brief", "Draft", "Review", "Signed"}));
        // Priority descends: Print 800 first; the rest keep their order.
        QCOMPARE(x.names(sortedOutline(x.b.p, QStringLiteral("priority"))),
                 strings({"Print", "Phase", "Brief", "Draft", "Review", "Signed"}));
        // Cost descends: Draft 24 h × $50 = $1,200 > Print 8 h × $50 = $400.
        QCOMPARE(x.names(sortedOutline(x.b.p, QStringLiteral("cost"))),
                 strings({"Phase", "Draft", "Brief", "Review", "Signed", "Print"}));
    }

    void networkLayout()
    {
        // A → B → D, A → C → D, A → D, and E alone.
        Builder b;
        const int a = b.task(QStringLiteral("A"), kDay);
        const int bb = b.task(QStringLiteral("B"), kDay);
        const int c = b.task(QStringLiteral("C"), kDay);
        const int d = b.task(QStringLiteral("D"), kDay);
        const int e = b.task(QStringLiteral("E"), kDay);
        b.link(bb, a);
        b.link(c, a);
        b.link(d, bb);
        b.link(d, c);
        b.link(d, a, LinkType::StartToStart);
        b.run();
        const NetworkOptions o;
        const NetworkLayout l = layOutNetwork(b.p, {a, bb, c, d, e}, o);
        const auto node = [&](int task) { return l.nodes.at(l.nodeOfTask.value(task)); };
        QCOMPARE(l.columns, 3);
        QCOMPARE(node(a).column, 0);
        QCOMPARE(node(e).column, 0);
        QCOMPARE(node(bb).column, 1);
        QCOMPARE(node(c).column, 1);
        QCOMPARE(node(d).column, 2);
        // Rows: A 0, E 1 (in order); B wants A's row 0, C takes 1; D wants
        // the mean of A, B, C (1/3 → 0).
        QCOMPARE(node(a).row, 0);
        QCOMPARE(node(e).row, 1);
        QCOMPARE(node(bb).row, 0);
        QCOMPARE(node(c).row, 1);
        QCOMPARE(node(d).row, 0);
        QCOMPARE(node(c).box.topLeft(), QPointF(o.margin + o.boxWidth + o.columnGap, o.margin + o.boxHeight + o.rowGap));
        QCOMPARE(l.size, QSizeF(2 * o.margin + 3 * o.boxWidth + 2 * o.columnGap, 2 * o.margin + 2 * o.boxHeight + o.rowGap));
        // No two boxes overlap; no edge segment passes through a box.
        for (int i = 0; i < l.nodes.size(); ++i) {
            for (int k = i + 1; k < l.nodes.size(); ++k) {
                QVERIFY(!l.nodes.at(i).box.intersects(l.nodes.at(k).box));
            }
        }
        QCOMPARE(l.edges.size(), 5);
        for (const NetworkEdge &edge : l.edges) {
            QCOMPARE(edge.points.first(), QPointF(l.nodes.at(edge.from).box.right(), l.nodes.at(edge.from).box.center().y()));
            QCOMPARE(edge.points.last(), QPointF(l.nodes.at(edge.to).box.left(), l.nodes.at(edge.to).box.center().y()));
            for (int s = 1; s + 1 < edge.points.size(); ++s) { // the inner points lie in gaps
                const QPointF &p = edge.points.at(s);
                QCOMPARE(networkNodeAt(l, p), -1);
            }
        }
        // A → D skips a column: out, along the row gap below A, down.
        for (const NetworkEdge &edge : l.edges) {
            if (edge.from == l.nodeOfTask.value(a) && edge.to == l.nodeOfTask.value(d)) {
                QCOMPARE(edge.points.size(), 6);
                QCOMPARE(edge.points.at(2).y(), node(a).box.bottom() + o.rowGap / 2);
                QCOMPARE(int(edge.type), int(LinkType::StartToStart));
            }
        }
        QCOMPARE(networkNodeAt(l, node(c).box.center()), l.nodeOfTask.value(c));
        // A summary sits left of its subtasks; without summaries it is gone.
        const Plan x;
        const NetworkLayout withPhase = layOutNetwork(x.b.p, x.all());
        QCOMPARE(withPhase.nodes.at(withPhase.nodeOfTask.value(x.phase)).column, 0);
        QCOMPARE(withPhase.nodes.at(withPhase.nodeOfTask.value(x.brief)).column, 1);
        NetworkOptions bare;
        bare.summaries = false;
        const NetworkLayout noPhase = layOutNetwork(x.b.p, x.all(), bare);
        QVERIFY(!noPhase.nodeOfTask.contains(x.phase));
        QCOMPARE(noPhase.nodes.at(noPhase.nodeOfTask.value(x.brief)).column, 0);
        QCOMPARE(noPhase.nodes.at(noPhase.nodeOfTask.value(x.signedOff)).column, 3);
    }

    void taskUsage()
    {
        const Plan x;
        const UsageTable t = taskUsageTable(x.b.p, x.all(), QDate(2026, 3, 2), 7, false);
        // Phase, Brief, Draft + Ana, Review, Signed, Print + Ana.
        QCOMPARE(t.rows.size(), 8);
        QCOMPARE(t.rows.at(0).id, QStringLiteral("t:%1").arg(x.b.t(x.phase).uid));
        QCOMPARE(t.rows.at(2).name, QStringLiteral("Draft"));
        QVERIFY(t.rows.at(3).assignment);
        QCOMPARE(t.rows.at(3).name, QStringLiteral("Ana"));
        // Draft: Wed–Fri, 8 h a day; Print: Mon–Tue at 50 %, 4 h a day.
        QCOMPARE(t.rows.at(2).work.at(2), 8 * kHour);
        QCOMPARE(t.rows.at(2).total, 24 * kHour);
        QCOMPARE(t.rows.at(7).work.at(0), 4 * kHour);
        QCOMPARE(t.rows.at(7).total, 8 * kHour);
        // The summary sums its subtasks' work (Brief has no resources).
        QCOMPARE(t.rows.at(0).total, 24 * kHour);
        const UsageTable weeks = taskUsageTable(x.b.p, x.all(), QDate(2026, 3, 4), 2, true);
        QCOMPARE(weeks.bucketStarts.first(), QDate(2026, 3, 2));
        QCOMPARE(weeks.rows.at(6).work.at(0), 8 * kHour);
    }

    void costAndWorkOverviews()
    {
        Plan x;
        x.b.p.tasks[x.draft].percentComplete = 50;
        x.b.p.tasks[x.draft].actualStart = x.b.t(x.draft).start;
        x.b.run();
        const QLocale c = QLocale::c();
        const PlanReport cost = buildReport(x.b.p, QStringLiteral("cost"), QDate(2026, 3, 4), c);
        QCOMPARE(cost.title, QStringLiteral("Cost Overview"));
        const auto kpi = [](const PlanReport &r, const QString &id) {
            for (const ReportKpi &k : r.kpis) {
                if (k.id == id) {
                    return k;
                }
            }
            return ReportKpi{};
        };
        // Draft $1,200 (half done), Print $400: $1,600, $600 spent.
        QCOMPARE(kpi(cost, QStringLiteral("cost")).value, formatMoney(160000, QStringLiteral("USD"), c));
        QCOMPARE(kpi(cost, QStringLiteral("actualCost")).value, formatMoney(60000, QStringLiteral("USD"), c));
        QCOMPARE(kpi(cost, QStringLiteral("remainingCost")).value, formatMoney(100000, QStringLiteral("USD"), c));
        QCOMPARE(kpi(cost, QStringLiteral("actualCost")).percent, 38);
        QVERIFY(kpi(cost, QStringLiteral("costVariance")).id.isEmpty()); // no baseline yet
        QCOMPARE(cost.tables.at(0).rows.size(), 2); // Phase, Print
        QCOMPARE(cost.tables.at(1).rows.at(0).at(0), QStringLiteral("Ana"));
        const PlanReport work = buildReport(x.b.p, QStringLiteral("work"), QDate(2026, 3, 4), c);
        QCOMPARE(work.title, QStringLiteral("Work Overview"));
        QCOMPARE(kpi(work, QStringLiteral("work")).value, formatWork(32 * kHour, c));
        QCOMPARE(kpi(work, QStringLiteral("actualWork")).value, formatWork(12 * kHour, c));
        setBaseline(x.b.p, 0);
        x.b.p.tasks[x.print].duration += kDay;
        x.b.run();
        const PlanReport after = buildReport(x.b.p, QStringLiteral("work"), QDate(2026, 3, 4), c);
        QCOMPARE(kpi(after, QStringLiteral("workVariance")).value, formatWork(4 * kHour, c));
        QCOMPARE(kpi(after, QStringLiteral("workVariance")).tone, QStringLiteral("warning"));
    }

    void costResources()
    {
        // M79: Project's cost resources — no rate, no units; an amount per
        // assignment, in the task's and the resource's cost, never work.
        Plan x;
        const int airfare = x.b.resource(QStringLiteral("Airfare"), 0, 100, ResourceType::Cost);
        Q_UNUSED(airfare)
        QVERIFY(setResourceNames(x.b.p, x.draft, QStringLiteral("Ana, Airfare[$450]"), QLocale::c()).ok);
        x.b.run();
        const int a = x.b.p.assignmentIndex(x.b.t(x.draft).uid, airfare);
        QCOMPARE(x.b.p.assignments.at(a).costInput, 45000);
        QCOMPARE(x.b.p.assignments.at(a).work, 0);
        QCOMPARE(x.b.t(x.draft).cost, 120000 + 45000);
        QCOMPARE(x.b.t(x.draft).work, 24 * kHour); // Ana's only
        const Resource &r = x.b.p.resources.at(x.b.p.resourceIndex(airfare));
        QCOMPARE(r.cost, 45000);
        QVERIFY(!r.overallocated);
        QCOMPARE(resourceNamesText(x.b.p, x.draft, QLocale::c()), QStringLiteral("Ana, Airfare[$450.00]"));
        QCOMPARE(resourceFieldText(x.b.p, x.b.p.resourceIndex(airfare), QStringLiteral("type"), QLocale::c()),
                 QStringLiteral("Cost"));
        QVERIFY(resourceFieldText(x.b.p, x.b.p.resourceIndex(airfare), QStringLiteral("standardRate"), QLocale::c()).isEmpty());
        QVERIFY(!setResourceField(x.b.p, x.b.p.resourceIndex(airfare), QStringLiteral("standardRate"), QStringLiteral("50"),
                                  QLocale::c()).ok);
        QVERIFY(!setResourceNames(x.b.p, x.draft, QStringLiteral("Airfare[lots]"), QLocale::c()).ok);
        // Re-typing the names without an amount keeps it; a new one replaces it.
        QVERIFY(setResourceNames(x.b.p, x.draft, QStringLiteral("Ana, Airfare"), QLocale::c()).ok);
        QCOMPARE(x.b.p.assignments.at(x.b.p.assignmentIndex(x.b.t(x.draft).uid, airfare)).costInput, 45000);
        QVERIFY(setResourceNames(x.b.p, x.draft, QStringLiteral("Ana, Airfare[300]"), QLocale::c()).ok);
        QCOMPARE(x.b.p.assignments.at(x.b.p.assignmentIndex(x.b.t(x.draft).uid, airfare)).costInput, 30000);
        // The .qplan keeps the type and the amount.
        Project back;
        QVERIFY(readPlanJson(writePlanJson(x.b.p), &back).ok);
        QCOMPARE(int(back.resources.at(back.resourceIndex(airfare)).type), int(ResourceType::Cost));
        QCOMPARE(back.tasks.at(back.taskIndex(x.b.t(x.draft).uid)).cost, 120000 + 30000);
    }

    void elevenBaselinesWithTheirAssignments()
    {
        Plan x;
        QCOMPARE(kBaselineCount, 11);
        QCOMPARE(baselineName(0), QStringLiteral("Baseline"));
        QCOMPARE(baselineName(10), QStringLiteral("Baseline 10"));
        QCOMPARE(setBaseline(x.b.p, 7, {x.b.t(x.draft).uid}), 1);
        const Assignment &a = x.b.p.assignments.at(x.b.p.assignmentIndex(x.b.t(x.draft).uid, x.ana));
        QVERIFY(a.baselines[7].set);
        QCOMPARE(a.baselines[7].work, 24 * kHour);
        QCOMPARE(a.baselines[7].cost, 120000);
        QCOMPARE_AT(a.baselines[7].start, at(4));
        const Assignment &p = x.b.p.assignments.at(x.b.p.assignmentIndex(x.b.t(x.print).uid, x.ana));
        QVERIFY(!p.baselines[7].set);
        x.b.p.tasks[x.draft].duration += kDay;
        x.b.run();
        const Variance v = variance(x.b.p, x.b.t(x.draft), 7);
        QVERIFY(v.valid);
        QCOMPARE(v.duration, kDay);
        QCOMPARE(v.work, 8 * kHour);
        QCOMPARE(v.cost, 40000);
        QVERIFY(!variance(x.b.p, x.b.t(x.print), 7).valid);
        QCOMPARE(clearBaseline(x.b.p, 7, {x.b.t(x.draft).uid}), 1);
        QVERIFY(!x.b.p.assignments.at(x.b.p.assignmentIndex(x.b.t(x.draft).uid, x.ana)).baselines[7].set);
        QVERIFY(!hasBaseline(x.b.p, 7));
    }
};

QTEST_GUILESS_MAIN(PlanViewsTest)

#include "tst_plan_views.moc"
