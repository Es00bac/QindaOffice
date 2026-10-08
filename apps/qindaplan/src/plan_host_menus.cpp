// SPDX-License-Identifier: GPL-3.0-or-later
// PlanHost: the menu tree (each command exactly once — the desktop's
// validator refuses duplicates) and command enablement / check state.
#include "plan_command_table.h"
#include "plan_document.h"
#include "plan_host.h"

#include "standard_actions.h"

#include <QUndoStack>

namespace QindaOffice::Apps::Plan {

using Quick::MenuNode;
namespace ActionId = Quick::ActionId;

namespace {

MenuNode a(const char *raw)
{
    return MenuNode::action(QString::fromLatin1(raw));
}

QList<MenuNode> nodes(std::initializer_list<const char *> ids)
{
    QList<MenuNode> out;
    for (const char *raw : ids) {
        out.append(raw == nullptr ? MenuNode::separator() : a(raw));
    }
    return out;
}

} // namespace

void PlanHost::insertMenus()
{
    Quick::MenuTree *tree = menuTree();
    const QString close = QString::fromLatin1(ActionId::kFileCloseWindow);
    tree->insertInto(QStringLiteral("fileMenu"), close,
                     MenuNode::submenu(QStringLiteral("planImportExportMenu"), tr("&Import and Export"),
                                       nodes({"planImportMspdiAction", nullptr, "planExportPdfAction",
                                                "planExportCsvAction", "planExportXlsxAction",
                                                "planExportMspdiAction", "planExportTimelineAction"})));
    tree->insertInto(QStringLiteral("fileMenu"), close, a("planPageSetupAction"));
    tree->insertInto(QStringLiteral("fileMenu"), close, MenuNode::separator());

    const QString before = QStringLiteral("aiMenu");
    QList<MenuNode> task = nodes({"planInsertTaskAction", "planInsertMilestoneAction", "planInsertSummaryAction",
                                    "planDeleteTaskAction", nullptr, "planIndentAction", "planOutdentAction",
                                    "planMoveUpAction", "planMoveDownAction", nullptr, "planLinkAction",
                                    "planUnlinkAction", nullptr});
    task.append(MenuNode::submenu(QStringLiteral("planProgressMenu"), tr("&Progress"),
                                  nodes({"planMark0Action", "planMark25Action", "planMark50Action", "planMark75Action",
                                           "planMark100Action"})));
    task.append(nodes({nullptr, "planManualScheduleAction", "planAutoScheduleAction", "planRespectLinksAction",
                         "planInactivateAction", nullptr, "planTaskInfoAction", "planTaskNotesAction",
                         "planScrollToTaskAction", nullptr, "planToggleSubtasksAction", "planExpandAllAction"}));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("planTaskMenu"), tr("&Task"), task));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("planResourceMenu"), tr("&Resource"),
                                                     nodes({"planAssignResourcesAction", nullptr,
                                                              "planAddResourceAction", "planAddMaterialAction",
                                                              "planDeleteResourceAction", "planResourceInfoAction",
                                                              "planResourceCalendarAction", nullptr,
                                                              "planLevelResourceAction", "planLevelAllAction",
                                                              "planClearLevelingAction",
                                                              "planNextOverallocationAction"})));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("planProjectMenu"), tr("&Project"),
                                                     nodes({"planProjectInfoAction", "planWorkingTimeAction", nullptr,
                                                              "planSetBaselineAction", "planClearBaselineAction",
                                                              nullptr, "planStatusDateAction",
                                                              "planUpdateProjectAction", "planRecalculateAction"})));
    tree->insertMenuBefore(before, MenuNode::submenu(QStringLiteral("planReportMenu"), tr("Re&port"),
                                                     nodes({"planReportOverviewAction", "planReportCriticalAction",
                                                              "planReportResourcesAction", "planReportLateAction"})));

    const QString firstView = QString::fromLatin1(ActionId::kViewZoomIn);
    QList<MenuNode> view = nodes({"planViewGanttAction", "planViewTaskSheetAction", "planViewResourceSheetAction",
                                    "planViewResourceUsageAction", "planViewCalendarAction", "planViewReportsAction",
                                    nullptr});
    view.append(MenuNode::submenu(QStringLiteral("planTablesMenu"), tr("T&ables"),
                                  nodes({"planTableEntryAction", "planTableTrackingAction", "planTableVarianceAction",
                                           "planTableCostAction", "planTableScheduleAction"})));
    view.append(MenuNode::submenu(QStringLiteral("planTimescaleMenu"), tr("Time&scale"),
                                  nodes({"planTimescaleDaysAction", "planTimescaleWeeksAction",
                                           "planTimescaleMonthsAction", nullptr, "planGoToTodayAction"})));
    view.append(nodes({nullptr, "planShowTimelineAction", "planShowCriticalAction", "planShowBaselineAction",
                         "planShowBarTextAction", "planUsageWeeksAction", nullptr}));
    for (const MenuNode &node : std::as_const(view)) {
        tree->insertInto(QStringLiteral("viewMenu"), firstView, node);
    }
}

void PlanHost::syncEnablement()
{
    Quick::ActionModel *model = actions();
    const QList<int> rows = m_service->selectedRows();
    const bool selected = !rows.isEmpty();
    const QVariantMap current = m_service->currentTask();
    const bool summary = current.value(QStringLiteral("summary")).toBool();
    model->setEnabled({QStringLiteral("planDeleteTaskAction"), QStringLiteral("planIndentAction"),
                       QStringLiteral("planOutdentAction"), QStringLiteral("planMoveUpAction"),
                       QStringLiteral("planMoveDownAction"), QStringLiteral("planUnlinkAction"),
                       QStringLiteral("planMark0Action"), QStringLiteral("planMark25Action"),
                       QStringLiteral("planMark50Action"), QStringLiteral("planMark75Action"),
                       QStringLiteral("planMark100Action"), QStringLiteral("planRespectLinksAction"),
                       QStringLiteral("planInactivateAction"), QStringLiteral("planManualScheduleAction"),
                       QStringLiteral("planAutoScheduleAction"), QStringLiteral("planTaskInfoAction"),
                       QStringLiteral("planTaskNotesAction"), QStringLiteral("planScrollToTaskAction"),
                       QStringLiteral("planAssignResourcesAction"), QString::fromLatin1(ActionId::kEditCut),
                       QString::fromLatin1(ActionId::kEditCopy)},
                      selected);
    model->setEnabled(QStringLiteral("planLinkAction"), rows.size() >= 2);
    model->setEnabled(QStringLiteral("planToggleSubtasksAction"), summary);
    const bool resource = m_service->project().resourceIndex(m_service->currentResourceUid()) >= 0;
    model->setEnabled({QStringLiteral("planDeleteResourceAction"), QStringLiteral("planResourceInfoAction"),
                       QStringLiteral("planResourceCalendarAction")},
                      resource);
    bool overallocated = false;
    for (const auto &r : m_service->project().resources) {
        overallocated = overallocated || r.overallocated;
    }
    model->setEnabled(QStringLiteral("planNextOverallocationAction"), overallocated);
    model->setEnabled({QString::fromLatin1(ActionId::kEditPaste), QString::fromLatin1(ActionId::kEditSelectAll),
                       QString::fromLatin1(ActionId::kEditFind), QString::fromLatin1(ActionId::kEditReplace),
                       QString::fromLatin1(ActionId::kViewZoomIn), QString::fromLatin1(ActionId::kViewZoomOut),
                       QString::fromLatin1(ActionId::kViewZoomReset)},
                      true);
    // Find Next/Previous repeat the last Find over task names.
    model->setEnabled({QString::fromLatin1(ActionId::kEditFindNext), QString::fromLatin1(ActionId::kEditFindPrevious)},
                      !m_findText.isEmpty());
    model->setEnabled(QString::fromLatin1(ActionId::kEditUndo), m_document->undoStack()->canUndo());
    model->setEnabled(QString::fromLatin1(ActionId::kEditRedo), m_document->undoStack()->canRedo());
}

void PlanHost::syncChecks()
{
    const QVariantMap current = m_service->currentTask();
    if (!current.isEmpty()) {
        action(current.value(QStringLiteral("manual")).toBool() ? QStringLiteral("planManualScheduleAction")
                                                                : QStringLiteral("planAutoScheduleAction"))
            ->setChecked(true);
        action(QStringLiteral("planInactivateAction"))->setChecked(!current.value(QStringLiteral("active")).toBool());
    }
    const QString table = m_service->table();
    const QString tableAction = QStringLiteral("planTable") + table.left(1).toUpper() + table.mid(1) + QStringLiteral("Action");
    if (Quick::ActionEntry *entry = action(tableAction)) {
        entry->setChecked(true);
    }
    const QString scale = m_service->timescale();
    const QString scaleAction = QStringLiteral("planTimescale") + scale.left(1).toUpper() + scale.mid(1) + QStringLiteral("Action");
    if (Quick::ActionEntry *entry = action(scaleAction)) {
        entry->setChecked(true);
    }
    action(QStringLiteral("planShowCriticalAction"))->setChecked(m_service->showCritical());
    action(QStringLiteral("planShowBaselineAction"))->setChecked(m_service->showBaseline());
    action(QStringLiteral("planShowBarTextAction"))->setChecked(m_service->showBarText());
    action(QStringLiteral("planUsageWeeksAction"))->setChecked(m_service->usageWeeks());
}

} // namespace QindaOffice::Apps::Plan
