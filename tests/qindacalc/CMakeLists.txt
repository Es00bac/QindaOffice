# SPDX-License-Identifier: GPL-3.0-or-later
# QindaCalc's gates over the QtQuick app: the document and host
# (tst_calc_window), the grid, strips, clipboard, find/replace and
# structural commands through the real window (tst_calc_ui), the rule
# commands (tst_calc_rules_ui) and the panel export through the desktop's
# validator (tst_calc_menu_export). Every test links the app's static QML
# module, so a source added to apps/qindacalc/CMakeLists.txt is in every test.
foreach(name IN ITEMS tst_calc_window tst_calc_ui tst_calc_rules_ui tst_calc_menu_export)
    add_executable(${name} ${name}.cpp calc_test_support.h)
    target_link_libraries(${name} PRIVATE qindacalc_app qindacalc_appplugin Qt6::Test)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures")
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()
