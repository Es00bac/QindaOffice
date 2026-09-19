# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(tst_calc_io tst_calc_io.cpp)
target_link_libraries(tst_calc_io PRIVATE calc_io KF6::Archive Qt6::Test)
qqo_set_strict_warnings(tst_calc_io)
add_test(NAME tst_calc_io COMMAND tst_calc_io)
set_tests_properties(tst_calc_io PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_calc_io_styles tst_calc_io_styles.cpp)
target_link_libraries(tst_calc_io_styles PRIVATE calc_io KF6::Archive Qt6::Test)
qqo_set_strict_warnings(tst_calc_io_styles)
add_test(NAME tst_calc_io_styles COMMAND tst_calc_io_styles)
set_tests_properties(tst_calc_io_styles PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
