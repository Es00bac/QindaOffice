// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PlanService: the whole QML API of a QindaPlan window over its
// PlanDocument (D-051). It owns the view models the toolkit controls draw —
// the Gantt's task model and calendar, the task and resource sheets'
// grid models, the usage table's plan model and one selection — and
// rebuilds them from the scheduled Project after every change.
//
// AGENT-CONTRACT (with qml/*.qml):
//  * Tasks are addressed by their GUID string (`taskId`), the id the
//    SelectionModel, the GanttTaskModel and the sheet's row ids share.
//    Resources by uid. Row numbers (IDs) are display only.
//  * Every figure crosses as text formatted by the engine (plus raw values
//    where QML must compare); QML never computes dates, spans or money.
//  * Edits return {ok, error}; a refusal changes nothing. Each call is one
//    undo step (PlanDocument::apply).
//  * `revision` increments after every change; bind to it to refresh
//    values read through Q_INVOKABLE calls.
//
// Files: plan_service.cpp (models, selection, view state),
// plan_service_columns.cpp (the sheet tables), plan_service_tasks.cpp,
// plan_service_resources.cpp, plan_service_project.cpp (project,
// calendars, baselines, status), plan_service_output.cpp (reports, files,
// clipboard), plan_service_views.cpp (Gantt rows, calendar events, usage
// table), plan_service_display.cpp (M79: filter, sort, Gantt styles, the
// Network Diagram, Task Usage), plan_service_tracking.cpp (M79: Update
// Tasks, Mark on Track, leveling options, Level Selection),
// plan_service_fields.cpp (M83: task types, assignments' units and work,
// custom fields with their filter and sort), plan_service_schedule.cpp
// (M83: splits, recurring tasks, Reschedule Uncompleted Work, the import,
// recurring calendar exceptions and work weeks), plan_service_groups.cpp
// (M89: Group By and Highlight, the rows the sheet and the Gantt share).
//
// AGENT-CONTRACT (M79, the rows a view shows): `visibleRows()` is the
// project's tasks in the sort order (outline kept), without those under a
// collapsed summary, through the filter (with their summary rows when
// relatedSummaries) — the sheet, the Gantt, the Network Diagram, Task Usage
// and the printout all show exactly these. The Calendar view takes the
// filter but not the collapse (it has no outline). M89 (D-097): with a
// group (groupBy), summary tasks are left out, the tasks come band by band
// (plan_groups.h) and a collapsed band's tasks are not visible; the sheet
// and the Gantt add each band's header row before its tasks.

#include "network_layout.h"
#include "plan_groups.h"
#include "plan_types.h"
#include "plan_views.h"

#include <qindatk/editable_grid_model.h>
#include <qindatk/gantt_calendar.h>
#include <qindatk/gantt_task_model.h>
#include <qindatk/resource_plan_model.h>
#include <qindatk/selection_model.h>

#include <QColor>
#include <QDate>
#include <QLocale>
#include <QObject>
#include <QRectF>
#include <QSet>
#include <QSizeF>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

namespace QindaOffice::Plan {
struct LevelingResult;
} // namespace QindaOffice::Plan

namespace QindaOffice::Apps::Plan {

namespace Engine = ::QindaOffice::Plan;
class PlanDocument;
class PlanGanttAdapter;
class ResourceSheetSource;
class TaskSheetSource;

class PlanService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Owned by PlanHost.")
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY changed)
    Q_PROPERTY(int taskCount READ taskCount NOTIFY changed)
    Q_PROPERTY(int resourceCount READ resourceCount NOTIFY changed)
    Q_PROPERTY(QVariantList issues READ issues NOTIFY changed)
    Q_PROPERTY(QString today READ today WRITE setToday NOTIFY changed)
    Q_PROPERTY(QindaTK::GanttTaskModel *ganttModel READ ganttModel CONSTANT)
    Q_PROPERTY(QindaTK::GanttCalendar *ganttCalendar READ ganttCalendar CONSTANT)
    Q_PROPERTY(QindaTK::SelectionModel *selection READ selection CONSTANT)
    Q_PROPERTY(QindaTK::EditableGridModel *sheetModel READ sheetModel CONSTANT)
    Q_PROPERTY(QindaTK::EditableGridModel *resourceSheetModel READ resourceSheetModel CONSTANT)
    Q_PROPERTY(QindaTK::ResourcePlanModel *usageModel READ usageModel CONSTANT)
    // View state (not undoable, not saved).
    Q_PROPERTY(QString table READ table WRITE setTable NOTIFY viewChanged)
    Q_PROPERTY(QVariantList sheetColumns READ sheetColumns NOTIFY viewChanged)
    Q_PROPERTY(QVariantList resourceColumns READ resourceColumns CONSTANT)
    Q_PROPERTY(QString timescale READ timescale WRITE setTimescale NOTIFY viewChanged)
    // Timescale width of one day as a multiple of Tk.Theme.size.control
    // (QML multiplies; the service knows no theme sizes).
    Q_PROPERTY(double dayScale READ dayScale NOTIFY viewChanged)
    Q_PROPERTY(bool showCritical READ showCritical WRITE setShowCritical NOTIFY viewChanged)
    Q_PROPERTY(bool showBaseline READ showBaseline WRITE setShowBaseline NOTIFY viewChanged)
    Q_PROPERTY(bool showBarText READ showBarText WRITE setShowBarText NOTIFY viewChanged)
    Q_PROPERTY(bool usageWeeks READ usageWeeks WRITE setUsageWeeks NOTIFY viewChanged)
    // M79 view state: Project's filter, sort, Gantt style (Gantt Chart,
    // Tracking Gantt, Leveling Gantt), the baseline the bars show, slack.
    Q_PROPERTY(QString filter READ filter NOTIFY viewChanged)
    Q_PROPERTY(QString filterTitle READ filterTitle NOTIFY viewChanged)
    Q_PROPERTY(bool relatedSummaries READ relatedSummaries WRITE setRelatedSummaries NOTIFY viewChanged)
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY viewChanged)
    Q_PROPERTY(QString ganttStyle READ ganttStyle WRITE setGanttStyle NOTIFY viewChanged)
    Q_PROPERTY(int ganttBaseline READ ganttBaseline WRITE setGanttBaseline NOTIFY viewChanged)
    Q_PROPERTY(bool showSlack READ showSlack WRITE setShowSlack NOTIFY viewChanged)
    // M83: Project's progress lines; M89 (D-097) drawn on screen too, at the
    // status date (else today): `progressLines` is GanttView's list.
    Q_PROPERTY(bool showProgressLines READ showProgressLines WRITE setShowProgressLines NOTIFY viewChanged)
    Q_PROPERTY(QVariantList progressLines READ progressLines NOTIFY changed)
    // M89 (D-097): Group By (plan_groups.h ids), Highlight (filter ids;
    // `highlightFill` is the theme colour QML hands in for the marked rows)
    // and the second baseline lane.
    Q_PROPERTY(QString groupBy READ groupBy WRITE setGroupBy NOTIFY viewChanged)
    Q_PROPERTY(QString groupTitle READ groupTitle NOTIFY viewChanged)
    Q_PROPERTY(QString highlight READ highlight NOTIFY viewChanged)
    Q_PROPERTY(QColor highlightFill READ highlightFill WRITE setHighlightFill NOTIFY viewChanged)
    Q_PROPERTY(bool showBaseline2 READ showBaseline2 WRITE setShowBaseline2 NOTIFY viewChanged)
    Q_PROPERTY(int ganttBaseline2 READ ganttBaseline2 WRITE setGanttBaseline2 NOTIFY viewChanged)
    Q_PROPERTY(QindaTK::ResourcePlanModel *taskUsageModel READ taskUsageModel CONSTANT)
    Q_PROPERTY(QVariantList taskUsageRows READ taskUsageRows NOTIFY changed)
    Q_PROPERTY(QSizeF networkSize READ networkSize NOTIFY changed)
    Q_PROPERTY(QString ganttStartUtc READ ganttStartUtc NOTIFY changed)
    Q_PROPERTY(QString projectStartIso READ projectStartIso NOTIFY changed)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY changed)
    Q_PROPERTY(QVariantList calendarEvents READ calendarEvents NOTIFY changed)
    Q_PROPERTY(QVariantList calendarSources READ calendarSources CONSTANT)
    Q_PROPERTY(QVariantList usageRows READ usageRows NOTIFY changed)
    // Selection (task GUIDs; the first is current).
    Q_PROPERTY(QString currentTaskId READ currentTaskId WRITE setCurrentTaskId NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedTaskIds READ selectedTaskIds WRITE setSelectedTaskIds NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap currentTask READ currentTask NOTIFY selectionChanged)
    Q_PROPERTY(int currentResourceUid READ currentResourceUid WRITE setCurrentResourceUid NOTIFY selectionChanged)
public:
    explicit PlanService(PlanDocument *document, QObject *parent = nullptr);
    ~PlanService() override;

    [[nodiscard]] PlanDocument *document() const { return m_document; }
    [[nodiscard]] const Engine::Project &project() const;
    [[nodiscard]] QLocale locale() const { return m_locale; }
    void setLocale(const QLocale &locale);

    [[nodiscard]] int revision() const { return m_revision; }
    [[nodiscard]] QString title() const;
    [[nodiscard]] QVariantMap summary() const;
    [[nodiscard]] int taskCount() const;
    [[nodiscard]] int resourceCount() const;
    [[nodiscard]] QVariantList issues() const;
    [[nodiscard]] QString today() const { return m_today.toString(Qt::ISODate); }
    void setToday(const QString &iso);
    [[nodiscard]] QDate todayDate() const { return m_today; }

    [[nodiscard]] QindaTK::GanttTaskModel *ganttModel() const { return m_gantt; }
    [[nodiscard]] QindaTK::GanttCalendar *ganttCalendar() const { return m_ganttCalendar; }
    [[nodiscard]] QindaTK::SelectionModel *selection() const { return m_selection; }
    [[nodiscard]] QindaTK::EditableGridModel *sheetModel() const { return m_sheet; }
    [[nodiscard]] QindaTK::EditableGridModel *resourceSheetModel() const { return m_resourceSheet; }
    [[nodiscard]] QindaTK::ResourcePlanModel *usageModel() const { return m_usage; }

    [[nodiscard]] QString table() const { return m_table; }
    void setTable(const QString &table);
    [[nodiscard]] QStringList tableFields() const;
    [[nodiscard]] static QStringList tableIds();
    [[nodiscard]] QVariantList sheetColumns() const;
    [[nodiscard]] QVariantList resourceColumns() const;
    [[nodiscard]] QString timescale() const { return m_timescale; }
    void setTimescale(const QString &timescale);
    [[nodiscard]] double dayScale() const;
    [[nodiscard]] bool showCritical() const { return m_showCritical; }
    void setShowCritical(bool on);
    [[nodiscard]] bool showBaseline() const { return m_showBaseline; }
    void setShowBaseline(bool on);
    [[nodiscard]] bool showBarText() const { return m_showBarText; }
    void setShowBarText(bool on);
    [[nodiscard]] bool usageWeeks() const { return m_usageWeeks; }
    void setUsageWeeks(bool on);
    [[nodiscard]] QString ganttStartUtc() const;
    [[nodiscard]] QString projectStartIso() const;
    [[nodiscard]] QVariantList markers() const;
    [[nodiscard]] QVariantList calendarEvents() const { return m_calendarEvents; }
    [[nodiscard]] QVariantList calendarSources() const;
    [[nodiscard]] QVariantList usageRows() const { return m_usageRows; }

    // ---- display (plan_service_display.cpp, M79) ----
    [[nodiscard]] QString filter() const { return m_filter.id; }
    [[nodiscard]] QString filterTitle() const;
    // id: plan_views.h's filter ids; "resource" takes {resourceUid},
    // "dateRange" {from, to} (dates as typed). Not undoable, not saved.
    Q_INVOKABLE QVariantMap applyFilter(const QString &id, const QVariantMap &params = {});
    [[nodiscard]] bool relatedSummaries() const { return m_relatedSummaries; }
    void setRelatedSummaries(bool on);
    [[nodiscard]] QString sortKey() const { return m_sortKey; }
    void setSortKey(const QString &key);
    [[nodiscard]] QString ganttStyle() const { return m_ganttStyle; }
    void setGanttStyle(const QString &style); // "gantt" | "tracking" | "leveling"
    [[nodiscard]] int ganttBaseline() const { return m_ganttBaseline; }
    void setGanttBaseline(int number); // 0–10
    [[nodiscard]] bool showSlack() const { return m_showSlack; }
    void setShowSlack(bool on);
    [[nodiscard]] bool showProgressLines() const { return m_showProgressLines; }
    void setShowProgressLines(bool on);
    [[nodiscard]] QindaTK::ResourcePlanModel *taskUsageModel() const { return m_taskUsage; }
    [[nodiscard]] QVariantList taskUsageRows() const { return m_taskUsageRows; }
    // The Network Diagram's layout of the visible rows (points).
    [[nodiscard]] const Engine::NetworkLayout &networkLayout() const { return m_network; }
    [[nodiscard]] QSizeF networkSize() const { return m_network.size; }
    Q_INVOKABLE int visibleRowCount() const { return int(m_visible.size()); }
    Q_INVOKABLE QString networkTaskAt(double x, double y) const;
    Q_INVOKABLE QRectF networkBoxOf(const QString &taskId) const;

    // ---- M89 (D-097): Group By, Highlight, planning bars
    // (plan_service_groups.cpp). The sheet and the Gantt show the same
    // rows: with a group, each band's header row ("group:<key>", QindaTK
    // D-312) then, unless it is collapsed, its tasks. View state only. ----
    [[nodiscard]] QString groupBy() const { return m_groupBy; }
    void setGroupBy(const QString &id);
    [[nodiscard]] QString groupTitle() const;
    // Group ▸ Custom Field…: bands by a custom field's value.
    Q_INVOKABLE QVariantMap setCustomGroup(const QString &field);
    // A group header row of the sheet (EditableGrid.groupHeader): {label,
    // summary, collapsed, level, values: {column: text}}, null otherwise.
    Q_INVOKABLE QVariant groupHeader(const QString &rowId) const;
    // The chevron of either view (groupToggled): the band shows or hides its tasks.
    Q_INVOKABLE void setGroupCollapsed(const QString &groupId, bool collapsed);
    [[nodiscard]] QString highlight() const { return m_highlight; }
    Q_INVOKABLE QVariantMap applyHighlight(const QString &id); // "none" or a filter id without a question
    [[nodiscard]] bool isHighlighted(int row) const { return m_highlighted.contains(row); }
    [[nodiscard]] QColor highlightFill() const { return m_highlightFill; }
    void setHighlightFill(const QColor &fill);
    [[nodiscard]] bool showBaseline2() const { return m_showBaseline2; }
    void setShowBaseline2(bool on);
    [[nodiscard]] int ganttBaseline2() const { return m_ganttBaseline2; }
    void setGanttBaseline2(int number); // 0–10
    [[nodiscard]] QVariantList progressLines() const;

    [[nodiscard]] QString currentTaskId() const;
    void setCurrentTaskId(const QString &taskId);
    [[nodiscard]] QStringList selectedTaskIds() const { return m_selected; }
    void setSelectedTaskIds(const QStringList &taskIds);
    [[nodiscard]] QVariantMap currentTask() const;
    [[nodiscard]] int currentResourceUid() const { return m_currentResource; }
    void setCurrentResourceUid(int uid);
    // Rows (indices into project().tasks) of the selection, sorted.
    [[nodiscard]] QList<int> selectedRows() const;
    [[nodiscard]] int rowOf(const QString &taskId) const;
    [[nodiscard]] QString idOfRow(int row) const;
    // Tasks under a collapsed summary are hidden from the sheet and Gantt.
    [[nodiscard]] QList<int> visibleRows() const { return m_visible; }
    [[nodiscard]] bool isCollapsed(int taskUid) const { return m_collapsed.contains(taskUid); }

    // ---- tasks (plan_service_tasks.cpp) ----
    Q_INVOKABLE QVariantMap setTaskField(const QString &taskId, const QString &field, const QString &text);
    Q_INVOKABLE QString taskFieldText(const QString &taskId, const QString &field) const;
    Q_INVOKABLE QVariantMap taskInfo(const QString &taskId) const;
    Q_INVOKABLE QVariantMap saveTaskInfo(const QString &taskId, const QVariantMap &fields);
    Q_INVOKABLE QVariantMap insertTask(const QString &kind, const QString &name = {}); // "task" | "milestone" | "summary"
    Q_INVOKABLE QVariantMap deleteSelectedTasks();
    Q_INVOKABLE QVariantMap indentSelected();
    Q_INVOKABLE QVariantMap outdentSelected();
    Q_INVOKABLE QVariantMap moveSelected(bool up);
    Q_INVOKABLE QVariantMap linkSelected();
    Q_INVOKABLE QVariantMap unlinkSelected();
    Q_INVOKABLE QVariantMap markSelected(int percent);
    Q_INVOKABLE QVariantMap setSelectedManual(bool manual);
    Q_INVOKABLE QVariantMap toggleSelectedActive();
    Q_INVOKABLE QVariantMap respectLinksSelected();
    Q_INVOKABLE QVariantMap moveTaskStart(const QString &taskId, const QString &startUtc);
    Q_INVOKABLE void toggleSubtasks(const QString &taskId = {});
    // Find selects the next (or previous) task whose name contains `text`
    // after the current one, wrapping; Replace rewrites task names (one
    // undo step). Both case-insensitive; collapsed subtasks are searched
    // too (their summary opens).
    Q_INVOKABLE QVariantMap findTask(const QString &text, bool forward = true);
    Q_INVOKABLE QVariantMap replaceInNames(const QString &text, const QString &replacement);
    Q_INVOKABLE void expandAll();
    // Commits from the sheet grid (TaskSheetSource).
    QString commitSheetChanges(const QVariantList &changes);

    // ---- resources (plan_service_resources.cpp) ----
    Q_INVOKABLE QVariantList resourceList() const;
    Q_INVOKABLE QVariantList assignmentsOf(const QString &taskId) const;
    Q_INVOKABLE QVariantMap assign(int resourceUid, const QString &unitsText);
    Q_INVOKABLE QVariantMap unassign(int resourceUid);
    Q_INVOKABLE QVariantMap addResource(const QString &name = {}, const QString &type = QStringLiteral("work"));
    Q_INVOKABLE QVariantMap deleteResource(int resourceUid);
    Q_INVOKABLE QVariantMap setResourceField(int resourceUid, const QString &field, const QString &text);
    // 0 = every resource; from/to = the leveling range (empty: all of it).
    Q_INVOKABLE QVariantMap level(int resourceUid, const QString &from = {}, const QString &to = {});
    Q_INVOKABLE QVariantMap clearLeveling();
    Q_INVOKABLE QVariantMap nextOverallocation();
    QString commitResourceSheetChanges(const QVariantList &changes);

    // ---- project (plan_service_project.cpp) ----
    Q_INVOKABLE QVariantMap projectInfo() const;
    Q_INVOKABLE QVariantMap setProjectInfo(const QVariantMap &fields);
    Q_INVOKABLE QVariantList calendars() const;
    Q_INVOKABLE QVariantList calendarWeek(int calendarUid) const;
    Q_INVOKABLE QVariantMap setCalendarDay(int calendarUid, int day, const QString &hoursText);
    Q_INVOKABLE QVariantList calendarExceptions(int calendarUid) const;
    Q_INVOKABLE QVariantMap addCalendarException(int calendarUid, const QString &name, const QString &from,
                                                 const QString &to, const QString &hoursText);
    Q_INVOKABLE QVariantMap removeCalendarException(int calendarUid, int index);
    Q_INVOKABLE int resourceCalendar(int resourceUid);
    Q_INVOKABLE QVariantList baselines() const;
    Q_INVOKABLE QVariantMap setBaseline(int number, bool selectedOnly = false);
    Q_INVOKABLE QVariantMap clearBaseline(int number, bool selectedOnly = false);
    Q_INVOKABLE QVariantMap setStatusDate(const QString &date);
    Q_INVOKABLE QVariantMap updateProject(const QString &date);
    Q_INVOKABLE QVariantMap report(const QString &id) const;
    Q_INVOKABLE QVariantMap exportCsv(const QUrl &url) const;
    // The same table as an Excel workbook (M63; plan_xlsx.h).
    Q_INVOKABLE QVariantMap exportXlsx(const QUrl &url) const;
    Q_INVOKABLE QVariantMap exportMspdi(const QUrl &url) const;
    Q_INVOKABLE QVariantMap exportTimelinePng(const QUrl &url, int width = 1600, int height = 280) const;
    Q_INVOKABLE QVariantMap importMspdi(const QUrl &url);
    Q_INVOKABLE QVariantMap copySelection() const;
    Q_INVOKABLE QVariantMap cutSelection();
    Q_INVOKABLE QVariantMap paste();
    Q_INVOKABLE QString formatUtc(const QString &utc) const;

    // ---- tracking and leveling (plan_service_tracking.cpp, M79) ----
    // Mark on Track: the selected tasks done through the status date (or
    // today). Update Tasks: {percent, actualDuration, remainingDuration,
    // actualStart, actualFinish} as typed, on the selected tasks, one step.
    Q_INVOKABLE QVariantMap markOnTrack();
    Q_INVOKABLE QVariantMap updateTasks(const QVariantMap &fields);
    // {order: "id"|"standard"|"priority", withinSlack, clearFirst}.
    Q_INVOKABLE QVariantMap levelingOptions() const;
    Q_INVOKABLE QVariantMap setLevelingOptions(const QVariantMap &options);
    Q_INVOKABLE QVariantMap levelSelection();
    Q_INVOKABLE QVariantMap clearLevelingSelected();

    // ---- M83 (D-092): task types and custom fields (plan_service_fields.cpp) ----
    // "units" | "duration" | "work" on the selection; effort driving likewise.
    Q_INVOKABLE QVariantMap setSelectedTaskType(const QString &type);
    Q_INVOKABLE QVariantMap setSelectedEffortDriven(bool on);
    // The task's assignments: [{resourceUid, name, kind ("work"|"material"|
    // "cost"), units, work, cost}] as text; field "units" | "work" typed.
    Q_INVOKABLE QVariantList assignmentDetails(const QString &taskId) const;
    Q_INVOKABLE QVariantMap setAssignmentField(const QString &taskId, int resourceUid, const QString &field,
                                               const QString &text);
    // Project ▸ Custom Fields: [{field, title, alias, lookup, formula, used}]
    // of one kind ("Text", "Number", …); a definition {field, kind, alias,
    // lookup: [{value, description}] (as typed), restrict, formula} saved as
    // one step; the fields in use [{field, title}]; a task's custom values
    // [{field, title, value, editable}].
    Q_INVOKABLE QVariantList customFields(const QString &kind) const;
    Q_INVOKABLE QVariantMap customFieldDefinition(const QString &field) const;
    Q_INVOKABLE QVariantMap saveCustomField(const QString &field, const QVariantMap &definition);
    Q_INVOKABLE QVariantList usedCustomFields() const;
    Q_INVOKABLE QVariantList taskCustomValues(const QString &taskId) const;
    // View ▸ Filter ▸ Custom Field… (test: plan_views.h) and Sort ▸ by
    // Custom Field…; both view state.
    Q_INVOKABLE QVariantMap applyCustomFilter(const QString &field, const QString &test, const QString &value);
    Q_INVOKABLE QVariantMap setCustomSort(const QString &field, bool descending);

    // ---- M83: schedule tools (plan_service_schedule.cpp) ----
    // Split Task: the current task stops at `stop`, resumes at `resume`.
    Q_INVOKABLE QVariantMap splitCurrentTask(const QString &stop, const QString &resume);
    Q_INVOKABLE QVariantMap removeSplitsSelected();
    Q_INVOKABLE QVariantMap rescheduleUncompletedWork(const QString &after);
    // Recurring tasks: the pattern of a recurring summary as fields ({} for
    // a new one), and Insert (taskId empty) or Recurring Task Information.
    // Fields: name, duration, frequency ("daily"|"weekly"|"monthly"|
    // "yearly"), every, workdays, days (Mon-first bit mask), byPosition,
    // day, position, weekday, month, from, to, count, startTime.
    Q_INVOKABLE QVariantMap recurringTaskFields(const QString &taskId) const;
    Q_INVOKABLE QVariantMap saveRecurringTask(const QString &taskId, const QVariantMap &fields);
    // Import Tasks: a preview {headers, rows, mapping, fields: [{id, title}]}
    // of an .xlsx or .csv, then the import with the chosen mapping (one step).
    Q_INVOKABLE QVariantMap importPreview(const QUrl &url, bool firstRowHeaders = true);
    Q_INVOKABLE QVariantMap importTasks(const QUrl &url, const QStringList &mapping, bool firstRowHeaders);
    // Change Working Time: a calendar's recurring exceptions and work weeks
    // as fields (same keys as the recurring task's pattern, plus hours) and
    // their edits; index -1 adds.
    Q_INVOKABLE QVariantList recurringExceptions(int calendarUid) const;
    Q_INVOKABLE QVariantMap saveRecurringException(int calendarUid, int index, const QVariantMap &fields);
    Q_INVOKABLE QVariantMap deleteRecurringException(int calendarUid, int index);
    Q_INVOKABLE QVariantList workWeeks(int calendarUid) const;
    Q_INVOKABLE QVariantMap saveWorkWeek(int calendarUid, int index, const QVariantMap &fields);
    Q_INVOKABLE QVariantMap deleteWorkWeek(int calendarUid, int index);

    // The Gantt rows for a project (the committed one or a moved preview
    // copy; plan_service_views.cpp). Same order and ids for both.
    [[nodiscard]] QVariantList ganttRows(const Engine::Project &project) const;
    // While alive, project changes do not push rows into the Gantt model
    // (it is inside commitPreview/undo and adopts rows itself).
    class GanttGuard {
    public:
        explicit GanttGuard(PlanService *service)
            : m_service(service)
        {
            ++m_service->m_ganttBusy;
        }
        ~GanttGuard() { --m_service->m_ganttBusy; }
        GanttGuard(const GanttGuard &) = delete;
        GanttGuard &operator=(const GanttGuard &) = delete;

    private:
        PlanService *m_service;
    };

    // Helpers shared by the parts.
    [[nodiscard]] QVariantMap apply(const QString &label, const std::function<QString(Engine::Project &)> &operation);
    [[nodiscard]] static QVariantMap ok(const QVariantMap &extra = {});
    [[nodiscard]] static QVariantMap fail(const QString &error);
    [[nodiscard]] static QString utcOf(Engine::Minutes instant);
    [[nodiscard]] static Engine::Minutes minutesOfUtc(const QString &utc);

signals:
    void changed();
    void viewChanged();
    void selectionChanged();
    // Something the user should hear about (a toast): variant "info" |
    // "warning" | "danger".
    void notify(const QString &title, const QString &text, const QString &variant);
    // A Gantt or sheet should scroll to show the task.
    void revealTask(const QString &taskId);

private:
    void rebuild();
    void rebuildVisible();
    void rebuildSheets();
    void rebuildGantt();       // plan_service_views.cpp
    void rebuildCalendar();    // plan_service_views.cpp
    void rebuildUsage();       // plan_service_views.cpp
    void rebuildGanttCalendar();
    void pruneSelection();
    // plan_service_display.cpp (M79)
    void rebuildTaskUsage();
    void rebuildNetwork();
    [[nodiscard]] QVariantMap ganttRow(const Engine::Project &p, int index, const QSet<int> &shown,
                                       const QHash<int, QString> &guidByUid) const;
    // plan_service_groups.cpp (M89): the bands of the visible rows and the
    // sheet/Gantt rows they make; a band's Gantt row from `p`'s tasks.
    void rebuildGroups();
    [[nodiscard]] QStringList displayRowIds() const;
    [[nodiscard]] QVariantMap groupGanttRow(const Engine::Project &p, int group) const;
    // M89: the planning bars of a task row (QindaTK D-313): slack, deadline,
    // progress point, split pieces, the second baseline.
    void addPlanningBars(QVariantMap *row, const Engine::Project &p, int index) const;
    // A leveling run's outcome as text and a toast (plan_service_resources.cpp).
    QVariantMap leveled(const QVariantMap &applied, const Engine::LevelingResult &result);

    PlanDocument *m_document;
    QLocale m_locale;
    QDate m_today;
    int m_revision = 0;
    QindaTK::GanttTaskModel *m_gantt = nullptr;
    QindaTK::GanttCalendar *m_ganttCalendar = nullptr;
    PlanGanttAdapter *m_adapter = nullptr;
    QindaTK::SelectionModel *m_selection = nullptr;
    QindaTK::EditableGridModel *m_sheet = nullptr;
    TaskSheetSource *m_sheetSource = nullptr;
    QindaTK::EditableGridModel *m_resourceSheet = nullptr;
    ResourceSheetSource *m_resourceSource = nullptr;
    QindaTK::ResourcePlanModel *m_usage = nullptr;
    QString m_table = QStringLiteral("entry");
    QString m_timescale = QStringLiteral("weeks");
    bool m_showCritical = true;
    bool m_showBaseline = true;
    bool m_showBarText = true;
    bool m_usageWeeks = true;
    QSet<int> m_collapsed;
    QList<int> m_visible;
    QStringList m_selected;
    int m_currentResource = 0;
    QVariantList m_calendarEvents;
    QVariantList m_usageRows;
    // M79
    Engine::TaskFilter m_filter;
    bool m_relatedSummaries = true;
    QString m_sortKey = QStringLiteral("id");
    QString m_ganttStyle = QStringLiteral("gantt");
    int m_ganttBaseline = 0;
    bool m_showSlack = false;
    bool m_showProgressLines = false; // M83
    QList<int> m_filtered; // filter only (the Calendar view)
    // M89 (D-097): the rows the sheet and the Gantt show — a task, or a
    // band's header (task -1) — the bands, which are collapsed, Highlight.
    struct DisplayRow {
        int task = -1;
        int group = -1;
    };
    QList<DisplayRow> m_display;
    QList<Engine::TaskGroup> m_groups;
    QString m_groupBy = QStringLiteral("none");
    QSet<QString> m_collapsedGroups;
    QHash<QString, int> m_groupRows; // a band's row id → its index in m_groups
    QString m_highlight = QStringLiteral("none"); // a filter id (plan_views.h) or "none"
    QSet<int> m_highlighted;
    QColor m_highlightFill;
    bool m_showBaseline2 = false;
    int m_ganttBaseline2 = 1;
    QindaTK::ResourcePlanModel *m_taskUsage = nullptr;
    QVariantList m_taskUsageRows;
    Engine::NetworkLayout m_network;
    QHash<QString, int> m_rowByGuid;
    int m_ganttBusy = 0;
    bool m_syncingSelection = false;
    QStringList m_sheetRows;
    QStringList m_resourceRows;
};

} // namespace QindaOffice::Apps::Plan
