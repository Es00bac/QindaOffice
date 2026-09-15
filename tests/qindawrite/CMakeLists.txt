# SPDX-License-Identifier: GPL-3.0-or-later

function(qqo_add_write_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE office_shell Qt6::Test Qt6::WidgetsPrivate)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_write_test(tst_write_probe_pagination)
qqo_add_write_test(tst_write_probe_control)
