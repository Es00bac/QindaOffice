# SPDX-License-Identifier: GPL-3.0-or-later
# QindaShow's gates over the QtQuick app: the deck engine, the canvas item
# and the host (tst_qindashow), the strips, clipboard, find, canvas
# painting and arrange commands through the real window (tst_show_ui), the
# design layer — masters, animations, snapping, grouping (tst_show_design)
# — and the panel export through the desktop's validator
# (tst_show_menu_export). Every test links the app's static QML module, so
# a source added to apps/qindashow/CMakeLists.txt is in every test.
foreach(name IN ITEMS tst_qindashow tst_show_ui tst_show_design tst_show_menu_export)
    add_executable(${name} ${name}.cpp show_test_support.h)
    target_link_libraries(${name} PRIVATE qindashow_app qindashow_appplugin Qt6::Test)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures")
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()
