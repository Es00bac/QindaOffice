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

add_executable(tst_calc_window tst_calc_window.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_clipboard.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_find_bar.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_format_controller.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_recovery.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_structure_actions.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_structure_menus.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_structure_ops.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_undo.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_window.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_window_commands.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_window_ui.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/chart_dialog.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/spreadsheet_document.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/spreadsheet_view.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/spreadsheet_view_input.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/spreadsheet_view_paint.cpp
)
target_link_libraries(tst_calc_window PRIVATE office_shell office_ai calc_io Qt6::Test)
if(QQO_WITH_CHARTS)
    target_link_libraries(tst_calc_window PRIVATE Qt6::Charts)
    target_compile_definitions(tst_calc_window PRIVATE QQO_WITH_CHARTS)
endif()
target_include_directories(tst_calc_window PRIVATE ${PROJECT_SOURCE_DIR}/apps)
qqo_set_strict_warnings(tst_calc_window)
add_test(NAME tst_calc_window COMMAND tst_calc_window)
set_tests_properties(tst_calc_window PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_calc_ui tst_calc_ui.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_clipboard.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_find_bar.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_format_controller.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_recovery.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_structure_actions.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_structure_menus.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_structure_ops.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_undo.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_window.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_window_commands.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/calc_window_ui.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/chart_dialog.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/spreadsheet_document.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/spreadsheet_view.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/spreadsheet_view_input.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindacalc/src/spreadsheet_view_paint.cpp
)
target_link_libraries(tst_calc_ui PRIVATE office_shell office_ai calc_io Qt6::Test)
if(QQO_WITH_CHARTS)
    target_link_libraries(tst_calc_ui PRIVATE Qt6::Charts)
    target_compile_definitions(tst_calc_ui PRIVATE QQO_WITH_CHARTS)
endif()
target_include_directories(tst_calc_ui PRIVATE ${PROJECT_SOURCE_DIR}/apps)
qqo_set_strict_warnings(tst_calc_ui)
add_test(NAME tst_calc_ui COMMAND tst_calc_ui)
set_tests_properties(tst_calc_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
