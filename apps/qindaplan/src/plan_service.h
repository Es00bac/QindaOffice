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
// table).

#include "plan_types.h"

#include <qindatk/editable_grid_model.h>
#include <qindatk/gantt_calendar.h>
#include <qindatk/gantt_task_model.h>
#include <qindatk/resource_plan_model.h>
#include <qindatk/selection_model.h>

#include <QDate>
#include <QLocale>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

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
    Q_INVOKABLE QVariantMap level(int resourceUid); // 0 = every resource
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
    Q_INVOKABLE QVariantMap clearBaseline(int number);
    Q_INVOKABLE QVariantMap setStatusDate(const QString &date);
    Q_INVOKABLE QVariantMap updateProject(const QString &date);
    Q_INVOKABLE QVariantMap report(const QString &id) const;
    Q_INVOKABLE QVariantMap exportCsv(const QUrl &url) const;
    Q_INVOKABLE QVariantMap exportMspdi(const QUrl &url) const;
    Q_INVOKABLE QVariantMap exportTimelinePng(const QUrl &url, int width = 1600, int height = 280) const;
    Q_INVOKABLE QVariantMap importMspdi(const QUrl &url);
    Q_INVOKABLE QVariantMap copySelection() const;
    Q_INVOKABLE QVariantMap cutSelection();
    Q_INVOKABLE QVariantMap paste();
    Q_INVOKABLE QString formatUtc(const QString &utc) const;

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
    QHash<QString, int> m_rowByGuid;
    int m_ganttBusy = 0;
    bool m_syncingSelection = false;
    QStringList m_sheetRows;
    QStringList m_resourceRows;
};

} // namespace QindaOffice::Apps::Plan
