// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// PlanHost: one QindaPlan window = one project (M51, D-051; rule 1). The
// host owns the PlanDocument, the window's PlanService (`plan`), the
// commands and menus, the current view and the ONE print source Print,
// Print Preview and Export PDF render (the Gantt chart, or the report on
// screen when the Reports view is open).
//
// AGENT-CONTRACT (with qml/PlanWindow.qml and friends):
//  * Commands are ActionEntries with the app-scoped `plan…Action` ids of
//    plan_host_actions.cpp (rule 5 — never rename one). Handlers either
//    call PlanService directly (edits; a refusal becomes a toast through
//    PlanService::notify) or emit a request signal the QML answers with a
//    dialog. The host never opens UI itself.
//  * The view is a radio group (planView…Action, Ctrl+1…6) mirrored by
//    `currentView`: "gantt" | "sheet" | "resources" | "usage" |
//    "calendar" | "reports", and since M79 (D-088) "tracking" (Tracking
//    Gantt) | "leveling" (Leveling Gantt) | "network" (Network Diagram) |
//    "taskUsage". The Gantt Chart Format tab is the ribbon context
//    "ganttFormat" while a Gantt view is shown; each sheet view remembers
//    its own table (Tracking Gantt opens on Tracking, Leveling Gantt on
//    Delay, as in Project). The print source follows the view: the Gantt in
//    the view's style, the Network Diagram, or the report on screen.
//  * Timescale zoom is `zoom` (View ▸ Zoom In/Out/Reset), a multiplier the
//    QML applies to PlanService::dayScale.
//
// Files: plan_host.cpp (document, view, printing), plan_host_actions.cpp
// (commands), plan_host_views.cpp (M79: views, tables, filters, sorts,
// baselines shown, tracking and leveling commands), plan_host_menus.cpp
// (menus and enablement), plan_host_schedule.cpp (M83: task types, splits,
// recurring tasks, custom fields, Reschedule Uncompleted Work, the import).

#include "document_host.h"
#include "plan_service.h"

#include <QDate>
#include <QHash>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Apps::Plan {

class PlanDocument;

// AGENT-NOTE: not `final` — QML_ELEMENT derives a creation helper from it.
class PlanHost : public Quick::DocumentHost {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application.")
    Q_PROPERTY(QindaOffice::Apps::Plan::PlanService *plan READ plan CONSTANT)
    Q_PROPERTY(QUrl preferencesPageUrl READ preferencesPage WRITE setPreferencesPage NOTIFY preferencesPageChanged)
    Q_PROPERTY(QString currentView READ currentView WRITE setCurrentView NOTIFY currentViewChanged)
    Q_PROPERTY(QString reportId READ reportId WRITE setReportId NOTIFY reportIdChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY zoomChanged)
    // The last text Find looked for (Find Next/Previous repeat it).
    Q_PROPERTY(QString findText READ findText WRITE setFindText NOTIFY findTextChanged)
    Q_PROPERTY(bool showTimeline READ showTimeline NOTIFY chromeStateChanged)
    // Gantt page setup: the printed date range (ISO dates; empty = the
    // whole project) and how many pages wide the timescale spans.
    Q_PROPERTY(QString printFrom READ printFrom WRITE setPrintFrom NOTIFY printSetupChanged)
    Q_PROPERTY(QString printTo READ printTo WRITE setPrintTo NOTIFY printSetupChanged)
    Q_PROPERTY(int printPagesWide READ printPagesWide WRITE setPrintPagesWide NOTIFY printSetupChanged)
public:
    PlanHost();
    ~PlanHost() override;

    [[nodiscard]] PlanService *plan() const { return m_service; }
    [[nodiscard]] PlanDocument *planDocument() const { return m_document; }

    [[nodiscard]] QString currentView() const { return m_view; }
    void setCurrentView(const QString &view);
    [[nodiscard]] QString reportId() const { return m_reportId; }
    void setReportId(const QString &id);
    [[nodiscard]] double zoom() const { return m_zoom; }
    void setZoom(double zoom);
    [[nodiscard]] bool showTimeline() const;
    [[nodiscard]] QString findText() const { return m_findText; }
    void setFindText(const QString &text);
    [[nodiscard]] QString printFrom() const { return m_printFrom; }
    void setPrintFrom(const QString &iso);
    [[nodiscard]] QString printTo() const { return m_printTo; }
    void setPrintTo(const QString &iso);
    [[nodiscard]] int printPagesWide() const { return m_printPagesWide; }
    void setPrintPagesWide(int pages);
    // Pages the current print source makes on the default paper (for the
    // page setup dialog's summary).
    Q_INVOKABLE int ganttPageCount() const;
    // Export PDF of the current print source (the File ▸ Export command's
    // dialog calls this with the chosen path).
    Q_INVOKABLE QVariantMap exportPdfTo(const QUrl &url);

    [[nodiscard]] static QStringList viewKeys();

signals:
    void currentViewChanged();
    void reportIdChanged();
    void zoomChanged();
    void findTextChanged();
    void chromeStateChanged();
    void printSetupChanged();
    // Requests the QML answers with dialogs.
    void taskInfoRequested(const QString &taskId, const QString &page); // page: "general" | "notes" | …
    void projectInfoRequested();
    void workingTimeRequested(int calendarUid);
    void baselineRequested(bool clear);
    void statusDateRequested(bool update);
    void assignResourcesRequested();
    void levelRequested();
    void resourceInfoRequested(int resourceUid);
    void pageSetupRequested();
    // M79: Using Resource… / Date Range… ask for their value; Update Tasks.
    void filterRequested(const QString &filterId);
    void updateTasksRequested();
    void levelingOptionsRequested();
    // M83 (D-092): the dialogs of Split Task, Recurring Task (taskId empty =
    // Insert), Custom Fields and Update Project's Reschedule Uncompleted Work.
    void splitTaskRequested();
    void recurringTaskRequested(const QString &taskId);
    void customFieldsRequested();
    void rescheduleRequested();
    void findRequested(bool replace);
    // "pdf" | "csv" | "xlsx" (M63) | "mspdi" | "png" | "import" | "importTasks" (M83)
    void fileRequested(const QString &what);
    void scrollToTaskRequested(const QString &taskId);
    void goToTodayRequested();

protected:
    [[nodiscard]] QString filterString() const override;
    [[nodiscard]] QString saveFilterString() const override;
    void documentReplaced() override;
    [[nodiscard]] Quick::PrintRenderCallback printRenderCallback() const override;
    [[nodiscard]] bool printPrefersLandscape() const override;

private:
    // plan_host_actions.cpp
    void createActions();
    void wireEditCommands();
    void report(const QVariantMap &result, const QString &doneMessage = {});
    // plan_host_views.cpp (M79)
    void createViewActions();
    void createTrackingActions();
    // plan_host_schedule.cpp (M83)
    void createScheduleActions();
    void syncScheduleState();
    // plan_host_groups.cpp (M89): Group By, Highlight, Second Baseline
    void createGroupActions();
    [[nodiscard]] QList<Quick::MenuNode> groupMenuNodes() const;
    void syncGroupChecks();
    [[nodiscard]] static QString viewActionId(const QString &view);
    [[nodiscard]] bool isGanttView() const;
    void switchTable(const QString &from, const QString &to);
    // plan_host_menus.cpp
    void insertMenus();
    void syncEnablement();
    void syncChecks();

    PlanDocument *m_document = nullptr;
    PlanService *m_service = nullptr;
    QString m_view = QStringLiteral("gantt");
    QString m_reportId = QStringLiteral("overview");
    double m_zoom = 1.0;
    QString m_findText;
    QString m_printFrom;
    QString m_printTo;
    int m_printPagesWide = 1;
    QHash<QString, QString> m_tableOfView; // M79: each sheet view's table
};

} // namespace QindaOffice::Apps::Plan
