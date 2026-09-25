# SPDX-License-Identifier: GPL-3.0-or-later

# mail_exchange tests (M43, D-044): the EWS and Microsoft Graph calendar
# adapters against in-process servers on 127.0.0.1 only (no real network),
# plus the pure mapping pieces. Every test name contains "exchange" so
# `ctest -R exchange` runs the lane.
add_library(mail_exchange_test_support STATIC
    fake_http_server.cpp
    fake_http_server.h
    ews_fake_server.cpp
    ews_fake_server.h
    graph_fake_server.cpp
    graph_fake_server.h
)
target_include_directories(mail_exchange_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mail_exchange_test_support PUBLIC mail_exchange Qt6::Network Qt6::Test)
qqo_set_strict_warnings(mail_exchange_test_support)

foreach(test_name mapping ews ews_writes graph graph_writes)
    set(target tst_mail_exchange_${test_name})
    add_executable(${target} tst_mail_exchange_${test_name}.cpp)
    target_link_libraries(${target} PRIVATE mail_exchange_test_support)
    qqo_set_strict_warnings(${target})
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
endforeach()
