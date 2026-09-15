# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(tst_office_ink tst_office_ink.cpp)
target_link_libraries(tst_office_ink PRIVATE office_ink Qt6::Test)
qqo_set_strict_warnings(tst_office_ink)
add_test(NAME tst_office_ink COMMAND tst_office_ink)
set_tests_properties(tst_office_ink PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
