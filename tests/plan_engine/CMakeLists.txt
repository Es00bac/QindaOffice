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
