# SPDX-License-Identifier: GPL-3.0-or-later

# plan_output tests (M51): pagination, PDF files, the timeline image.
# QQO_GRAB_DIR saves PNG renders of the pages for review.
add_executable(tst_plan_output tst_plan_output.cpp)
# calc_io (M63): the task-sheet workbook is read back with QXlsx.
target_link_libraries(tst_plan_output PRIVATE plan_output calc_io Qt6::Test)
target_compile_definitions(tst_plan_output PRIVATE QT_NO_CAST_FROM_ASCII)
qqo_set_strict_warnings(tst_plan_output)
add_test(NAME tst_plan_output COMMAND tst_plan_output)
set_tests_properties(tst_plan_output PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# M83 (D-092): the task import from an Excel workbook (a task sheet written
# here and read back through calc_io), and the printed Gantt's split gaps and
# progress line.
add_executable(tst_plan_output_m83 tst_plan_output_m83.cpp)
target_link_libraries(tst_plan_output_m83 PRIVATE plan_output calc_io Qt6::Test)
target_compile_definitions(tst_plan_output_m83 PRIVATE QT_NO_CAST_FROM_ASCII)
qqo_set_strict_warnings(tst_plan_output_m83)
add_test(NAME tst_plan_output_m83 COMMAND tst_plan_output_m83)
set_tests_properties(tst_plan_output_m83 PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
