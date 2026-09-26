# SPDX-License-Identifier: GPL-3.0-or-later
# QindaDiagram's gates over the app (M50, D-050): the host's commands,
# service, printing and clipboard (tst_qindadiagram_host), the panel export
# through the desktop's own validator (tst_qindadiagram_menu_export), and
# the real window with seeded drawings, the ribbon gate and fixtures
# (tst_qindadiagram_ui). Every test links the app's static QML module.
foreach(name IN ITEMS tst_qindadiagram_host tst_qindadiagram_menu_export tst_qindadiagram_ui)
    add_executable(${name} ${name}.cpp diagram_app_test_support.h)
    target_link_libraries(${name} PRIVATE qindadiagram_app qindadiagram_appplugin Qt6::Test)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures")
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()
