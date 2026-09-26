# SPDX-License-Identifier: GPL-3.0-or-later
# office_quick's own contract: the action registry, the panel export
# (validated by the desktop's own validator, offscreen, no D-Bus) and the
# document host's asynchronous flows.
foreach(name IN ITEMS tst_action_model tst_menu_export tst_document_host tst_office_window tst_print_service tst_global_menu_hold)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE office_quick office_quickplugin Qt6::Test)
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()
