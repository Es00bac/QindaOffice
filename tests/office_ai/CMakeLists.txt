# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(tst_office_ai tst_office_ai.cpp
    ${PROJECT_SOURCE_DIR}/tests/office_ai/mock_sse_server.cpp
    ${PROJECT_SOURCE_DIR}/tests/office_ai/mock_sse_server.h
)
target_link_libraries(tst_office_ai PRIVATE office_ai Qt6::Test)
qqo_set_strict_warnings(tst_office_ai)
add_test(NAME tst_office_ai COMMAND tst_office_ai)
set_tests_properties(tst_office_ai PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
