# SPDX-License-Identifier: GPL-3.0-or-later
# QindaCalc's gates over the QtQuick app: the document and host
# (tst_calc_window), the grid, strips, clipboard, find/replace and
# structural commands through the real window (tst_calc_ui), the rule
# commands (tst_calc_rules_ui), links / outlines / protection / spills
# (tst_calc_features_ui, M66), tables, array formulas and chart sheets
# (tst_calc_tables_ui, M69), PivotTables (tst_calc_pivot_ui, M74), M78's
# what-if, printing, data tools and conditional formats (tst_calc_whatif_ui,
# tst_calc_print_ui, tst_calc_datatools_ui, tst_calc_condformat_ui), M82's
# text overflow on screen and paper (tst_calc_overflow_ui), Split, New
# Window and Custom Views (tst_calc_views_ui), M89's Formula AutoComplete
# (tst_calc_completion_ui) and the
# panel export through the desktop's validator (tst_calc_menu_export). Every test links the app's static QML
# module, so a source added to apps/qindacalc/CMakeLists.txt is in every test.
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_QT6 REQUIRED poppler-qt6)

foreach(name IN ITEMS tst_calc_window tst_calc_sample tst_calc_ui tst_calc_rules_ui
                      tst_calc_menu_export tst_calc_tools tst_calc_agent tst_calc_features_ui
                      tst_calc_tables_ui tst_calc_pivot_ui
                      tst_calc_whatif_ui tst_calc_print_ui tst_calc_datatools_ui tst_calc_condformat_ui
                      tst_calc_overflow_ui tst_calc_views_ui tst_calc_completion_ui)
    add_executable(${name} ${name}.cpp calc_test_support.h calc_m78_support.h)
    target_link_libraries(${name} PRIVATE qindacalc_app qindacalc_appplugin Qt6::Test)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures")
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()

if(QQO_AGENT_SOURCE_DIR)
    set(QQO_AGENT_CLI "${QQO_AGENT_SOURCE_DIR}/tools/agent/qinda-agent")
else()
    find_program(QQO_AGENT_CLI NAMES qinda-agent REQUIRED)
endif()
target_compile_definitions(tst_calc_agent PRIVATE QQO_AGENT_CLI="${QQO_AGENT_CLI}")
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME calc_agent_process
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/agent_process_smoke.py
            $<TARGET_FILE:qindacalc> ${QQO_AGENT_CLI})
set_tests_properties(calc_agent_process PROPERTIES TIMEOUT 120)

# M69 (D-078): tables, array formulas and chart sheets through the window;
# its chart-sheet workbook is built with calc_io's Excel package builder.
target_link_libraries(tst_calc_tables_ui PRIVATE office_ooxml KF6::Archive)
# M74 (D-083): the hand-built Excel PivotTables come from the same builder.
target_link_libraries(tst_calc_pivot_ui PRIVATE office_ooxml KF6::Archive)

# The print-path gates in tst_calc_window and tst_calc_print_ui (M78) and
# the overflow on paper (tst_calc_overflow_ui, M82) read the produced PDF back.
foreach(name IN ITEMS tst_calc_window tst_calc_print_ui tst_calc_overflow_ui)
    target_link_libraries(${name} PRIVATE ${POPPLER_QT6_LIBRARIES})
    target_include_directories(${name} PRIVATE ${POPPLER_QT6_INCLUDE_DIRS})
    target_compile_options(${name} PRIVATE ${POPPLER_QT6_CFLAGS_OTHER})
endforeach()
