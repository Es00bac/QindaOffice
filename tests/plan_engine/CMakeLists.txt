# SPDX-License-Identifier: GPL-3.0-or-later

# plan_engine tests (M51): headless; the scheduler fixtures are computed by
# hand in the test sources (docs/plan.md explains each expected date).
function(qqo_add_plan_test name)
    add_executable(${name} ${name}.cpp plan_fixture.h)
    target_link_libraries(${name} PRIVATE plan_engine Qt6::Test ${ARGN})
    target_compile_definitions(${name} PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" QT_NO_CAST_FROM_ASCII)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_plan_test(tst_plan_calendar)
qqo_add_plan_test(tst_plan_schedule)
qqo_add_plan_test(tst_plan_edit)
qqo_add_plan_test(tst_plan_file)
qqo_add_plan_test(tst_plan_resources)
qqo_add_plan_test(tst_plan_mspdi)
# M63 (D-071): what Microsoft Project XML holds beyond the model is kept and
# written back in Project's order.
qqo_add_plan_test(tst_plan_mspdi_keep)
# M79 (D-088): Project's scheduling, tracking and views — the critical path
# with deadlines and the slack options, leveling options and delays, filters,
# sorts, the Network Diagram's layout, usage, reports and baselines 0–10,
# and their Microsoft Project XML.
qqo_add_plan_test(tst_plan_critical)
qqo_add_plan_test(tst_plan_leveling)
qqo_add_plan_test(tst_plan_views)
qqo_add_plan_test(tst_plan_mspdi_m79)
# M83 (D-092): task types and effort driving, custom fields, elapsed
# durations and lags, split and recurring tasks, actual work and cost, the
# progress line, Reschedule Uncompleted Work, recurring calendar exceptions
# and work weeks edited here, and the Excel/CSV task import.
qqo_add_plan_test(tst_plan_task_types)
qqo_add_plan_test(tst_plan_custom_fields)
qqo_add_plan_test(tst_plan_tracking_m83)
qqo_add_plan_test(tst_plan_recurring_import)
# M83: a Project-shaped file of all of it (data/project_m83_sample.xml).
qqo_add_plan_test(tst_plan_mspdi_m83)
