# SPDX-License-Identifier: GPL-3.0-or-later

# plan_engine: the QindaPlan project model and scheduler (M51, D-051) —
# tasks, outline, links, working calendars, constraints, the critical-path
# passes, resources, leveling, baselines, editing operations, the .qplan
# file format, Microsoft Project XML (MSPDI) and CSV; since M79 (D-088) also
# Project's filters and sorts and the Network Diagram's layout. Qt Core only: no Gui,
# no Quick, no desktop archives (AGENTS.md rules 8/9), so every behaviour is
# testable offscreen. M83 (D-092): task types and effort driving, custom
# fields and their formulas, elapsed durations and lags, split and recurring
# tasks, actual work and cost, progress lines, Reschedule Uncompleted Work,
# editable recurring calendar exceptions and work weeks, Excel/CSV import.
# AGENT-GUARD: never add Gui here — painting (print, PDF, timeline PNG)
# lives in plan_output.
add_library(plan_engine STATIC
    src/baselines.cpp
    src/baselines.h
    src/calendar_edit.cpp
    src/calendar_edit.h
    src/custom_field_catalog.cpp
    src/custom_fields.cpp
    src/custom_fields.h
    src/custom_formula.cpp
    src/custom_formula_functions.cpp
    src/custom_formula_p.h
    src/custom_formula_parse.cpp
    src/leveling.cpp
    src/leveling.h
    src/mspdi.h
    src/mspdi_adopt.cpp
    src/mspdi_assemble.cpp
    src/mspdi_baselines.cpp
    src/mspdi_common.cpp
    src/mspdi_custom.cpp
    src/mspdi_custom.h
    src/mspdi_keep.cpp
    src/mspdi_keep.h
    src/mspdi_order.cpp
    src/mspdi_p.h
    src/mspdi_reader.cpp
    src/mspdi_reader_calendar.cpp
    src/mspdi_reader_p.h
    src/mspdi_recurrence.cpp
    src/mspdi_timephased.cpp
    src/mspdi_timephased.h
    src/mspdi_writer.cpp
    src/network_layout.cpp
    src/network_layout.h
    src/outline.cpp
    src/outline.h
    src/plan_clipboard.cpp
    src/plan_edit.h
    src/plan_field_edit.cpp
    src/plan_field_work.cpp
    src/plan_fields.cpp
    src/plan_fields_p.h
    src/plan_file.cpp
    src/plan_file.h
    src/plan_import.cpp
    src/plan_import.h
    src/plan_json.cpp
    src/plan_json.h
    src/plan_links.cpp
    src/plan_ops.cpp
    src/plan_reports.cpp
    src/plan_reports.h
    src/plan_reports_costs.cpp
    src/plan_reports_p.h
    src/plan_groups.cpp
    src/plan_groups.h
    src/plan_resource_fields.cpp
    src/plan_time.cpp
    src/plan_time.h
    src/plan_types.cpp
    src/plan_types.h
    src/plan_views.cpp
    src/plan_views.h
    src/recurring_tasks.cpp
    src/recurring_tasks.h
    src/resources.cpp
    src/resources.h
    src/scheduler.cpp
    src/scheduler.h
    src/scheduler_p.h
    src/scheduler_graph.cpp
    src/scheduler_rollup.cpp
    src/scheduler_time.cpp
    src/task_segments.cpp
    src/task_segments.h
    src/task_types.cpp
    src/task_types.h
    src/tracking.cpp
    src/tracking.h
    src/work_calendar.cpp
    src/work_calendar.h
)

target_include_directories(plan_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(plan_engine PUBLIC Qt6::Core)
target_compile_definitions(plan_engine PRIVATE QT_NO_CAST_FROM_ASCII)

qqo_set_strict_warnings(plan_engine)
