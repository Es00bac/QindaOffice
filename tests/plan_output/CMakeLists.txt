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
