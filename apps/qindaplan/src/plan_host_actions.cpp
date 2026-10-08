// SPDX-License-Identifier: GPL-3.0-or-later
// PlanHost: the QindaPlan commands (ActionEntries) and their handlers.
//
// AGENT-CONTRACT (rule 5): every id below is the command's activation
// identity for the panel export, the ribbon and the keyboard — never rename
// or reuse one; add new ids for new commands. plan_host_menus.cpp places
// each in the menu tree exactly once.
#include "plan_command_table.h"
#include "plan_document.h"
#include "plan_host.h"

#include "standard_actions.h"

#include <QKeySequence>
#include <QUndoStack>

namespace QindaOffice::Apps::Plan {

using Quick::ActionEntry;
namespace ActionId = Quick::ActionId;

namespace {
QString id(const char *raw)
{
    return QString::fromLatin1(raw);
}
} // namespace

void PlanHost::report(const QVariantMap &result, const QString &doneMessage)
{
    if (!result.value(QStringLiteral("ok")).toBool()) {
        emit m_service->notify(tr("Not done"), result.value(QStringLiteral("error")).toString(), QStringLiteral("warning"));
    } else if (!doneMessage.isEmpty()) {
        showStatusMessage(doneMessage);
    }
}

void PlanHost::createActions()
{
    for (const PlanCommand &c : planCommands()) {
        ActionEntry *entry = actions()->add(id(c.id), tr(c.text), QKeySequence(QString::fromLatin1(c.shortcut)), id(c.icon));
        if (c.group != nullptr) {
            entry->setCheckable(true);
            if (c.group[0] != '\0') {
                entry->setRadioGroup(id(c.group));
            }
        }
    }
    action(id(ActionId::kFileNew))->setText(tr("&New Project"));
    action(QStringLiteral("planViewGanttAction"))->setChecked(true);
    action(QStringLiteral("planTableEntryAction"))->setChecked(true);
    action(QStringLiteral("planTimescaleWeeksAction"))->setChecked(true);
    action(QStringLiteral("planAutoScheduleAction"))->setChecked(true);

    const auto on = [this](const char *raw, auto &&slot) {
        connect(action(id(raw)), &ActionEntry::triggered, this, std::forward<decltype(slot)>(slot));
    };
    const QStringList views = viewKeys();
    const char *viewIds[] = {"planViewGanttAction",         "planViewTaskSheetAction", "planViewResourceSheetAction",
                             "planViewResourceUsageAction", "planViewCalendarAction",  "planViewReportsAction"};
    for (int i = 0; i < views.size(); ++i) {
        const QString key = views.at(i);
        connect(action(id(viewIds[i])), &ActionEntry::toggled, this, [this, key](bool checked) {
            if (checked) {
                setCurrentView(key);
            }
        });
    }
    const QStringList tableIds = PlanService::tableIds();
    const char *tableActions[] = {"planTableEntryAction", "planTableTrackingAction", "planTableVarianceAction",
                                  "planTableCostAction", "planTableScheduleAction"};
    for (int i = 0; i < tableIds.size(); ++i) {
        const QString table = tableIds.at(i);
        connect(action(id(tableActions[i])), &ActionEntry::toggled, this, [this, table](bool checked) {
            if (checked) {
                m_service->setTable(table);
            }
        });
    }
    const QList<std::pair<const char *, const char *>> scales{{"planTimescaleDaysAction", "days"},
                                                              {"planTimescaleWeeksAction", "weeks"},
                                                              {"planTimescaleMonthsAction", "months"}};
    for (const auto &[raw, scale] : scales) {
        const QString s = id(scale);
        connect(action(id(raw)), &ActionEntry::toggled, this, [this, s](bool checked) {
            if (checked) {
                m_service->setTimescale(s);
            }
        });
    }
    connect(action(QStringLiteral("planShowCriticalAction")), &ActionEntry::toggled, m_service, &PlanService::setShowCritical);
    connect(action(QStringLiteral("planShowBaselineAction")), &ActionEntry::toggled, m_service, &PlanService::setShowBaseline);
    connect(action(QStringLiteral("planShowBarTextAction")), &ActionEntry::toggled, m_service, &PlanService::setShowBarText);
    connect(action(QStringLiteral("planUsageWeeksAction")), &ActionEntry::toggled, m_service, &PlanService::setUsageWeeks);
    connect(action(QStringLiteral("planShowTimelineAction")), &ActionEntry::toggled, this, &PlanHost::chromeStateChanged);
    action(QStringLiteral("planShowCriticalAction"))->setChecked(true);
    action(QStringLiteral("planShowBaselineAction"))->setChecked(true);
    action(QStringLiteral("planShowBarTextAction"))->setChecked(true);
    action(QStringLiteral("planShowTimelineAction"))->setChecked(true);

    // ---- Task ----
    on("planInsertTaskAction", [this] { report(m_service->insertTask(QStringLiteral("task"))); });
    on("planInsertMilestoneAction", [this] { report(m_service->insertTask(QStringLiteral("milestone"))); });
    on("planInsertSummaryAction", [this] { report(m_service->insertTask(QStringLiteral("summary"))); });
    on("planDeleteTaskAction", [this] { report(m_service->deleteSelectedTasks()); });
    on("planIndentAction", [this] { report(m_service->indentSelected()); });
    on("planOutdentAction", [this] { report(m_service->outdentSelected()); });
    on("planMoveUpAction", [this] { report(m_service->moveSelected(true)); });
    on("planMoveDownAction", [this] { report(m_service->moveSelected(false)); });
    on("planLinkAction", [this] { report(m_service->linkSelected()); });
    on("planUnlinkAction", [this] { report(m_service->unlinkSelected()); });
    on("planMark0Action", [this] { report(m_service->markSelected(0)); });
    on("planMark25Action", [this] { report(m_service->markSelected(25)); });
    on("planMark50Action", [this] { report(m_service->markSelected(50)); });
    on("planMark75Action", [this] { report(m_service->markSelected(75)); });
    on("planMark100Action", [this] { report(m_service->markSelected(100)); });
    on("planRespectLinksAction", [this] { report(m_service->respectLinksSelected()); });
    on("planInactivateAction", [this] { report(m_service->toggleSelectedActive()); });
    on("planManualScheduleAction", [this] { report(m_service->setSelectedManual(true)); });
    on("planAutoScheduleAction", [this] { report(m_service->setSelectedManual(false)); });
    on("planTaskInfoAction", [this] { emit taskInfoRequested(m_service->currentTaskId(), QStringLiteral("general")); });
    on("planTaskNotesAction", [this] { emit taskInfoRequested(m_service->currentTaskId(), QStringLiteral("notes")); });
    on("planScrollToTaskAction", [this] { emit scrollToTaskRequested(m_service->currentTaskId()); });
    on("planToggleSubtasksAction", [this] { m_service->toggleSubtasks(); });
    on("planExpandAllAction", [this] { m_service->expandAll(); });

    // ---- Resource ----
    on("planAssignResourcesAction", [this] { emit assignResourcesRequested(); });
    on("planAddResourceAction", [this] {
        setCurrentView(QStringLiteral("resources"));
        report(m_service->addResource());
    });
    on("planAddMaterialAction", [this] {
        setCurrentView(QStringLiteral("resources"));
        report(m_service->addResource(tr("New material"), QStringLiteral("material")));
    });
    on("planDeleteResourceAction", [this] { report(m_service->deleteResource(m_service->currentResourceUid())); });
    on("planResourceInfoAction", [this] { emit resourceInfoRequested(m_service->currentResourceUid()); });
    on("planResourceCalendarAction", [this] {
        const int uid = m_service->resourceCalendar(m_service->currentResourceUid());
        if (uid > 0) {
            emit workingTimeRequested(uid);
        }
    });
    on("planLevelResourceAction", [this] { emit levelRequested(); });
    on("planLevelAllAction", [this] { report(m_service->level(0)); });
    on("planClearLevelingAction", [this] { report(m_service->clearLeveling(), tr("Leveling delays removed")); });
    on("planNextOverallocationAction", [this] {
        const QVariantMap r = m_service->nextOverallocation();
        report(r, r.value(QStringLiteral("ok")).toBool()
                      ? tr("%1 is overallocated on %2").arg(r.value(QStringLiteral("resource")).toString(),
                                                           r.value(QStringLiteral("date")).toString())
                      : QString());
    });

    // ---- Project ----
    on("planProjectInfoAction", [this] { emit projectInfoRequested(); });
    on("planWorkingTimeAction", [this] { emit workingTimeRequested(m_service->project().info.calendarUid); });
    on("planSetBaselineAction", [this] { emit baselineRequested(false); });
    on("planClearBaselineAction", [this] { emit baselineRequested(true); });
    on("planStatusDateAction", [this] { emit statusDateRequested(false); });
    on("planUpdateProjectAction", [this] { emit statusDateRequested(true); });
    on("planRecalculateAction", [this] {
        (void)m_service->apply(tr("Recalculate"), [](::QindaOffice::Plan::Project &) { return QString(); });
        showStatusMessage(tr("The schedule is up to date"));
    });

    // ---- Report and files ----
    const QList<std::pair<const char *, const char *>> reports{{"planReportOverviewAction", "overview"},
                                                               {"planReportCriticalAction", "critical"},
                                                               {"planReportResourcesAction", "resources"},
                                                               {"planReportLateAction", "late"}};
    for (const auto &[raw, reportKey] : reports) {
        const QString key = id(reportKey);
        on(raw, [this, key] {
            setReportId(key);
            setCurrentView(QStringLiteral("reports"));
        });
    }
    on("planExportPdfAction", [this] { emit fileRequested(QStringLiteral("pdf")); });
    on("planExportCsvAction", [this] { emit fileRequested(QStringLiteral("csv")); });
    on("planExportXlsxAction", [this] { emit fileRequested(QStringLiteral("xlsx")); });
    on("planExportMspdiAction", [this] { emit fileRequested(QStringLiteral("mspdi")); });
    on("planExportTimelineAction", [this] { emit fileRequested(QStringLiteral("png")); });
    on("planImportMspdiAction", [this] { emit fileRequested(QStringLiteral("import")); });
    on("planPageSetupAction", [this] { emit pageSetupRequested(); });
    on("planGoToTodayAction", [this] { emit goToTodayRequested(); });
    wireEditCommands();
}

void PlanHost::wireEditCommands()
{
    const auto on = [this](const char *raw, auto &&slot) {
        connect(action(id(raw)), &ActionEntry::triggered, this, std::forward<decltype(slot)>(slot));
    };
    QUndoStack *stack = m_document->undoStack();
    ActionEntry *undo = action(id(ActionId::kEditUndo));
    ActionEntry *redo = action(id(ActionId::kEditRedo));
    connect(stack, &QUndoStack::canUndoChanged, undo, &ActionEntry::setEnabled);
    connect(stack, &QUndoStack::canRedoChanged, redo, &ActionEntry::setEnabled);
    // AGENT-NOTE: the labels stay "Undo"/"Redo" (not "Undo Edit Cells"):
    // the ribbon sizes its groups from the labels, and a label that grows
    // with every edit re-squeezes the whole Task tab to icons.
    on(ActionId::kEditUndo, [stack] { stack->undo(); });
    on(ActionId::kEditRedo, [stack] { stack->redo(); });
    on(ActionId::kEditCopy, [this] { report(m_service->copySelection()); });
    on(ActionId::kEditCut, [this] { report(m_service->cutSelection()); });
    on(ActionId::kEditPaste, [this] { report(m_service->paste()); });
    on(ActionId::kEditSelectAll, [this] {
        QStringList all;
        for (int row : m_service->visibleRows()) {
            all.append(m_service->idOfRow(row));
        }
        m_service->setSelectedTaskIds(all);
    });
    on(ActionId::kEditFind, [this] { emit findRequested(false); });
    on(ActionId::kEditReplace, [this] { emit findRequested(true); });
    on(ActionId::kEditFindNext, [this] { report(m_service->findTask(m_findText, true)); });
    on(ActionId::kEditFindPrevious, [this] { report(m_service->findTask(m_findText, false)); });
    on(ActionId::kViewZoomIn, [this] { setZoom(m_zoom * 1.25); });
    on(ActionId::kViewZoomOut, [this] { setZoom(m_zoom / 1.25); });
    on(ActionId::kViewZoomReset, [this] { setZoom(1.0); });
}

} // namespace QindaOffice::Apps::Plan
