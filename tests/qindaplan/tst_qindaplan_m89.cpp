// SPDX-License-Identifier: GPL-3.0-or-later
// M89 (D-097) through PlanHost and PlanService over the seeded gala: View ▸
// Group By (built-in groups and a custom field) as QindaTK group header rows
// the sheet and the Gantt share — bands in their value's order with their
// rollups, collapsed and expanded, summaries left out, a bar preview with
// the same rows — View ▸ Highlight, and the planning bars the GanttView
// draws since M89: slack and free slack, deadlines, the progress point and
// line, split pieces and a second baseline. All of it is view state: no
// undo step.

#include "plan_app_test_support.h"

#include "plan_document.h"
#include "plan_groups.h"

#include <QUndoStack>

using namespace PlanAppTest;

namespace {

struct Loaded {
    QTemporaryDir dir;
    PlanHost host;
    PlanService *plan = host.plan();
    bool loaded = false;
    Loaded()
    {
        plan->setToday(kToday);
        loaded = host.loadPath(writeGala(dir));
    }
    QString id(const char *name) const
    {
        const Engine::Project &p = plan->project();
        for (int i = 0; i < p.tasks.size(); ++i) {
            if (p.tasks.at(i).name == s(name)) {
                return plan->idOfRow(i);
            }
        }
        return {};
    }
    const Engine::Task &task(const char *name) const { return plan->project().tasks.at(plan->rowOf(id(name))); }
    bool trigger(const char *command) { return host.actions()->trigger(s(command)); }
    QUndoStack *undo() const { return host.planDocument()->undoStack(); }
    [[nodiscard]] QVariantMap ganttRow(const QString &taskId) const
    {
        return plan->ganttModel()->get(plan->ganttModel()->indexOfId(taskId));
    }
    [[nodiscard]] QStringList sheetRows() const
    {
        QStringList out;
        for (int r = 0; r < plan->sheetModel()->rows(); ++r) {
            out.append(plan->sheetModel()->rowIdAt(r));
        }
        return out;
    }
};

} // namespace

class PlanM89Test final : public QObject {
    Q_OBJECT
private slots:
    void commandsExist()
    {
        Loaded l;
        QVERIFY(l.loaded);
        for (const char *command :
             {"planGroupNoneAction", "planGroupCompleteAction", "planGroupConstraintAction", "planGroupCriticalAction",
              "planGroupDurationAction", "planGroupMilestonesAction", "planGroupPriorityAction", "planGroupResourceAction",
              "planGroupStatusAction", "planGroupModeAction", "planGroupActiveAction", "planGroupCustomAction",
              "planHighlightNoneAction", "planHighlightCriticalAction", "planHighlightIncompleteAction",
              "planHighlightCompletedAction", "planHighlightLateAction", "planHighlightSlippingAction",
              "planHighlightMilestonesAction", "planHighlightSummaryAction", "planHighlightDeadlinesAction",
              "planShowBaseline2Action", "planBaseline2Shown0Action", "planBaseline2Shown10Action"}) {
            QVERIFY2(l.host.actions()->entry(s(command)) != nullptr, command);
        }
    }

    void groupByBandsTheRowsBothViewsShow()
    {
        Loaded l;
        QVERIFY(l.loaded);
        const int steps = l.undo()->count();
        const QStringList before = l.sheetRows();
        QVERIFY(l.trigger("planGroupCriticalAction"));
        QCOMPARE(l.plan->groupBy(), s("critical"));
        QCOMPARE(l.plan->groupTitle(), s("Critical"));
        const QStringList rows = l.sheetRows();
        // Two bands, No before Yes; summaries left out, every leaf kept.
        const QVariantMap first = l.plan->groupHeader(rows.at(0)).toMap();
        QCOMPARE(first.value(s("label")).toString(), s("Critical: No"));
        int bands = 0;
        int leaves = 0;
        for (const QString &row : rows) {
            if (row == QLatin1String("new")) {
                continue;
            }
            if (l.plan->groupHeader(row).isValid()) {
                ++bands;
            } else {
                QVERIFY(!l.plan->project().tasks.at(l.plan->rowOf(row)).summary);
                ++leaves;
            }
        }
        QCOMPARE(bands, 2);
        int leafTasks = 0;
        for (const Engine::Task &t : l.plan->project().tasks) {
            leafTasks += t.summary ? 0 : 1;
        }
        QCOMPARE(leaves, leafTasks);
        // The Gantt shows the same rows in the same order, bands as groups.
        QCOMPARE(l.plan->ganttModel()->rowCount(), rows.size() - 1);
        for (int r = 0; r + 1 < rows.size(); ++r) {
            QCOMPARE(l.plan->ganttModel()->get(r).value(s("taskId")).toString(), rows.at(r));
        }
        QCOMPARE(l.plan->ganttModel()->get(0).value(s("kind")).toString(), s("group"));
        QCOMPARE(l.plan->ganttModel()->get(1).value(s("outlineLevel")).toInt(), 1);
        // A band's rollups: its tasks' work and cost.
        const QVariantMap values = first.value(s("values")).toMap();
        QVERIFY(values.value(s("work")).toString().endsWith(s("hrs")));
        QVERIFY(!values.value(s("start")).toString().isEmpty());
        QVERIFY(first.value(s("summary")).toString().contains(s("task")));
        // Collapsing a band hides its tasks in both views; expanding brings them back.
        l.plan->setGroupCollapsed(rows.at(0), true);
        const QStringList collapsed = l.sheetRows();
        QVERIFY(collapsed.size() < rows.size());
        QCOMPARE(collapsed.at(0), rows.at(0));
        QVERIFY(l.plan->groupHeader(collapsed.at(1)).isValid()); // the next band right after
        QVERIFY(l.plan->ganttModel()->get(0).value(s("collapsed")).toBool());
        QCOMPARE(l.plan->ganttModel()->rowCount(), collapsed.size() - 1);
        l.plan->setGroupCollapsed(rows.at(0), false);
        QCOMPARE(l.sheetRows(), rows);
        // A bar preview keeps the committed rows (the adapter's contract).
        const QString venue = l.id("Floor plan and seating");
        const qint64 start = l.ganttRow(venue).value(s("startUtc")).toString().toLongLong();
        QVERIFY2(l.plan->ganttModel()->previewMove(venue, QString::number(start + 86400000LL * 2)),
                 qPrintable(l.plan->ganttModel()->lastError()));
        l.plan->ganttModel()->cancelPreview();
        // No group: the outline again; nothing was an undo step.
        QVERIFY(l.trigger("planGroupNoneAction"));
        QCOMPARE(l.sheetRows(), before);
        QCOMPARE(l.undo()->count(), steps);
    }

    void groupsComeInTheirValuesOrder()
    {
        Loaded l;
        l.plan->setGroupBy(s("priority"));
        l.plan->setTaskField(l.id("Book the venue"), s("priority"), s("900"));
        const QStringList rows = l.sheetRows();
        QCOMPARE(l.plan->groupHeader(rows.at(0)).toMap().value(s("label")).toString(), s("Priority: 900"));
        l.plan->setGroupBy(s("status"));
        QStringList labels;
        for (const QString &row : l.sheetRows()) {
            if (l.plan->groupHeader(row).isValid()) {
                labels.append(l.plan->groupHeader(row).toMap().value(s("label")).toString());
            }
        }
        // Complete, On Schedule, Late, Future Task — those the gala has.
        QVERIFY(labels.contains(s("Status: Complete")));
        QVERIFY(labels.contains(s("Status: Future Task")));
        QVERIFY(labels.indexOf(s("Status: Complete")) < labels.indexOf(s("Status: Future Task")));
        // A custom field: its values, the tasks without one last.
        QVERIFY(ok(l.plan->setTaskField(l.id("Book the venue"), s("Text1"), s("Board"))));
        QVERIFY(ok(l.plan->setTaskField(l.id("Rehearsal"), s("Text1"), s("Volunteers"))));
        QVERIFY(ok(l.plan->setCustomGroup(s("Text1"))));
        QCOMPARE(l.plan->groupBy(), s("custom:Text1"));
        labels.clear();
        for (const QString &row : l.sheetRows()) {
            if (l.plan->groupHeader(row).isValid()) {
                labels.append(l.plan->groupHeader(row).toMap().value(s("label")).toString());
            }
        }
        QCOMPARE(labels.size(), 3);
        QVERIFY(labels.at(0).endsWith(s(": Board")));
        QVERIFY(labels.at(1).endsWith(s(": Volunteers")));
        QVERIFY(labels.at(2).endsWith(s(": (none)")));
        QVERIFY(!ok(l.plan->setCustomGroup(s("Nothing1"))));
        QVERIFY(Engine::isTaskGroupId(s("custom:Number3")));
        QVERIFY(!Engine::isTaskGroupId(s("bogus")));
    }

    void highlightMarksWithoutHiding()
    {
        Loaded l;
        const int rows = l.plan->sheetModel()->rows();
        QVERIFY(l.trigger("planHighlightCriticalAction"));
        QCOMPARE(l.plan->highlight(), s("critical"));
        QCOMPARE(l.plan->sheetModel()->rows(), rows);
        const Engine::Project &p = l.plan->project();
        int marked = 0;
        for (int i = 0; i < p.tasks.size(); ++i) {
            QCOMPARE(l.plan->isHighlighted(i), p.tasks.at(i).critical);
            marked += l.plan->isHighlighted(i) ? 1 : 0;
        }
        QVERIFY(marked > 0);
        QVERIFY(!ok(l.plan->applyHighlight(s("resource")))); // the ones that ask are not offered
        QVERIFY(l.trigger("planHighlightNoneAction"));
        QVERIFY(!l.plan->isHighlighted(p.taskIndex(l.task("Sponsor outreach").uid)));
    }

    void planningBarsOnScreen()
    {
        Loaded l;
        const QString outreach = l.id("Sponsor outreach");
        // A deadline shows always.
        QVERIFY(ok(l.plan->setTaskField(outreach, s("deadline"), s("2026-04-10"))));
        QVERIFY(l.ganttRow(outreach).contains(s("deadlineUtc")));
        // In progress: the progress point lies inside the bar.
        QVERIFY(l.task("Sponsor outreach").percentComplete > 0);
        const QVariantMap row = l.ganttRow(outreach);
        QVERIFY(row.value(s("progressUtc")).toString().toLongLong() > row.value(s("startUtc")).toString().toLongLong());
        // Slack: a task with total slack gets its bar; critical ones none.
        QVERIFY(l.trigger("planShowSlackAction"));
        int slackBars = 0;
        for (int r = 0; r < l.plan->ganttModel()->rowCount(); ++r) {
            slackBars += l.plan->ganttModel()->get(r).contains(s("slackFinishUtc")) ? 1 : 0;
            if (l.plan->ganttModel()->get(r).value(s("critical")).toBool()) {
                QVERIFY(!l.plan->ganttModel()->get(r).contains(s("slackFinishUtc")));
            }
        }
        QVERIFY(slackBars > 0);
        // A split task's pieces (as M83's test splits it).
        const QString floor = l.id("Floor plan and seating");
        l.plan->setSelectedTaskIds({floor});
        const QDate start = Engine::dateOf(l.task("Floor plan and seating").start);
        const QVariantMap split = l.plan->splitCurrentTask(start.toString(Qt::ISODate), start.addDays(7).toString(Qt::ISODate));
        QVERIFY2(ok(split), qPrintable(split.value(s("error")).toString()));
        const QVariantList pieces = l.ganttRow(floor).value(s("segments")).toList();
        QCOMPARE(pieces.size(), 2);
        QCOMPARE(pieces.first().toMap().value(s("startUtc")), l.ganttRow(floor).value(s("startUtc")));
        // The second baseline: Baseline 1 under Baseline.
        QVERIFY(ok(l.plan->setBaseline(1)));
        QVERIFY(!l.ganttRow(outreach).contains(s("baseline2StartUtc")));
        QVERIFY(l.trigger("planBaseline2Shown1Action"));
        QVERIFY(l.plan->showBaseline2());
        QCOMPARE(l.plan->ganttBaseline2(), 1);
        QVERIFY(l.ganttRow(outreach).contains(s("baseline2StartUtc")));
        QVERIFY(l.ganttRow(outreach).contains(s("baseline2FinishUtc")));
        // Progress lines at the status date.
        QVERIFY(l.plan->progressLines().isEmpty());
        QVERIFY(l.trigger("planShowProgressLinesAction"));
        const QVariantList lines = l.plan->progressLines();
        QCOMPARE(lines.size(), 1);
        QCOMPARE(lines.first().toMap().value(s("utc")).toString(), PlanService::utcOf(l.plan->project().info.statusDate));
        // The model took every row: no field was refused.
        QVERIFY2(l.plan->ganttModel()->lastError().isEmpty(), qPrintable(l.plan->ganttModel()->lastError()));
    }
};

PLAN_APP_TEST_MAIN(PlanM89Test)
#include "tst_qindaplan_m89.moc"
