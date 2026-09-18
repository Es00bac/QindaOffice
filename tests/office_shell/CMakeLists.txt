# SPDX-License-Identifier: GPL-3.0-or-later

function(qqo_add_shell_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE office_shell Qt6::Test)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    # Widget tests run headless; the global-menu export must fail closed here.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_shell_test(tst_standard_actions)
qqo_add_shell_test(tst_template_loader)
qqo_add_shell_test(tst_desktop_settings)
qqo_add_shell_test(tst_document_window)
qqo_add_shell_test(tst_preferences)
qqo_add_shell_test(tst_office_application)
qqo_add_shell_test(tst_single_instance)

