# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(tst_office_ai tst_office_ai.cpp
    ${PROJECT_SOURCE_DIR}/tests/office_ai/mock_sse_server.cpp
    ${PROJECT_SOURCE_DIR}/tests/office_ai/mock_sse_server.h
)
target_link_libraries(tst_office_ai PRIVATE office_ai Qt6::Test)
qqo_set_strict_warnings(tst_office_ai)
add_test(NAME tst_office_ai COMMAND tst_office_ai)
set_tests_properties(tst_office_ai PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Live notes and the OpenCode-style provider plumbing (D-057).
foreach(name IN ITEMS tst_live_notes tst_ai_providers)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE office_ai Qt6::Test)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QQO_NO_SECRET_SERVICE=1")
endforeach()
