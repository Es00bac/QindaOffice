# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(tst_show_model tst_show_model.cpp)
target_link_libraries(tst_show_model PRIVATE show_model Qt6::Test Qt6::Gui)
target_include_directories(tst_show_model PRIVATE ${PROJECT_SOURCE_DIR}/tests)
qqo_set_strict_warnings(tst_show_model)
add_test(NAME tst_show_model COMMAND tst_show_model)
set_tests_properties(tst_show_model PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
