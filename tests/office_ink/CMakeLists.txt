# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(tst_office_ink tst_office_ink.cpp)
target_link_libraries(tst_office_ink PRIVATE office_ink Qt6::Test)
qqo_set_strict_warnings(tst_office_ink)
add_test(NAME tst_office_ink COMMAND tst_office_ink)
set_tests_properties(tst_office_ink PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_stroke_edit tst_stroke_edit.cpp)
target_link_libraries(tst_stroke_edit PRIVATE office_ink Qt6::Test)
qqo_set_strict_warnings(tst_stroke_edit)
add_test(NAME tst_stroke_edit COMMAND tst_stroke_edit)
set_tests_properties(tst_stroke_edit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
