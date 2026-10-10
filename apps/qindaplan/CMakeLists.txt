# SPDX-License-Identifier: GPL-3.0-or-later
# QindaPlan (M51, D-051): project scheduling — tasks, links, working
# calendars, Gantt, resources, baselines, reports — one project per window
# over plan_engine / plan_output. The C++ side is the host (commands,
# menus, print source), PlanService (the QML API and the view models the
# toolkit controls draw) and the document; the QML module is the window,
# its views and dialogs.

set(qindaplan_qml
    qml/PlanWindow.qml
    qml/PlanRibbonLayout.qml
    qml/GanttWorkspace.qml
    qml/TaskSheet.qml
    qml/TimelineStrip.qml
    qml/ResourceSheetView.qml
    qml/ResourceUsageView.qml
    qml/CalendarWorkspace.qml
    qml/ReportsView.qml
    qml/ReportKpi.qml
    qml/TaskInfoDialog.qml
    qml/TaskInfoGeneral.qml
    qml/ProjectInfoDialog.qml
    qml/WorkingTimeDialog.qml
    qml/BaselineDialog.qml
    qml/StatusDateDialog.qml
    qml/AssignResourcesDialog.qml
    qml/LevelDialog.qml
    qml/PageSetupDialog.qml
    qml/ResourceInfoDialog.qml
    qml/PlanFileDialogs.qml
    qml/FindTaskDialog.qml
    # M79 (D-088): the Network Diagram and Task Usage views, Update Tasks,
    # the two filters that ask a question.
    qml/NetworkView.qml
    qml/TaskUsageView.qml
    qml/UpdateTasksDialog.qml
    qml/FilterDialog.qml
    # M83 (D-092): custom fields, recurring tasks and calendar recurrences,
    # Split Task, the Excel/CSV import, Task Information's new pages.
    qml/CustomFieldsDialog.qml
    qml/ImportTasksDialog.qml
    qml/RecurrencePatternEditor.qml
    qml/RecurringTaskDialog.qml
    qml/SplitTaskDialog.qml
    qml/TaskInfoAssignments.qml
    qml/TaskInfoCustom.qml
    qml/WorkingTimeRecurring.qml
    qml/WorkingTimeWeeks.qml
)
foreach(qml_file IN LISTS qindaplan_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

# AGENT-CONTRACT: the app's QML module is a STATIC library so the tests can
# instantiate PlanWindow.qml and the host; the executable is main.cpp.
qt_add_library(qindaplan_app STATIC)
qt_add_qml_module(qindaplan_app
    URI QindaOffice.Plan.App
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK QindaOffice.Shell
    SOURCES
        src/plan_command_table.h
        src/plan_calendar_text.h
        src/plan_document.cpp src/plan_document.h
        src/plan_gantt_adapter.cpp src/plan_gantt_adapter.h
        src/plan_host.cpp src/plan_host.h
        src/plan_host_actions.cpp
        src/plan_host_menus.cpp
        src/plan_host_schedule.cpp
        src/plan_host_views.cpp
        src/plan_network_item.cpp src/plan_network_item.h
        src/plan_service.cpp src/plan_service.h
        src/plan_service_columns.cpp
        src/plan_service_display.cpp
        src/plan_service_fields.cpp
        src/plan_service_output.cpp
        src/plan_service_project.cpp
        src/plan_service_resources.cpp
        src/plan_service_schedule.cpp
        src/plan_service_tasks.cpp
        src/plan_service_tracking.cpp
        src/plan_service_views.cpp
        src/plan_timeline_item.cpp src/plan_timeline_item.h
        src/sheet_sources.cpp src/sheet_sources.h
    QML_FILES ${qindaplan_qml}
)
target_include_directories(qindaplan_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qindaplan_app PUBLIC
    office_quick office_quickplugin plan_engine plan_output QindaTK::qindatk Qt6::PrintSupport)
target_compile_definitions(qindaplan_app PRIVATE QT_NO_CAST_FROM_ASCII)
qqo_set_strict_warnings(qindaplan_app)

qt_add_executable(qindaplan main.cpp)
target_link_libraries(qindaplan PRIVATE qindaplan_app qindaplan_appplugin)
target_compile_definitions(qindaplan PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaPlan"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindaplan)
qqo_desktop_runtime(qindaplan)
install(TARGETS qindaplan RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaPlan.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
