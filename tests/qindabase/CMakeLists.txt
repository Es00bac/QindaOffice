# SPDX-License-Identifier: GPL-3.0-or-later
# QindaBase's gates over the app (M49): BaseService's QML API over a real
# database (tst_qindabase_service), the host's commands, undo and print
# source (tst_qindabase_host), the panel export through the desktop's own
# validator (tst_qindabase_menu_export), the real window
# (tst_qindabase_ui) and Excel/Access/file-field interchange through the
# service (tst_qindabase_interchange, M65); M80 (D-089) through the service
# (tst_qindabase_m80) and the real window (tst_qindabase_ui_m80); M86
# (D-094) likewise (tst_qindabase_m86, tst_qindabase_ui_m86). Every test
# links the app's static QML module.
foreach(name IN ITEMS tst_qindabase_service tst_qindabase_interchange tst_qindabase_host tst_qindabase_menu_export
                      tst_qindabase_ui tst_qindabase_m80 tst_qindabase_ui_m80 tst_qindabase_m86 tst_qindabase_ui_m86)
    add_executable(${name} ${name}.cpp base_app_test_support.h)
    target_link_libraries(${name} PRIVATE qindabase_app qindabase_appplugin Qt6::Test)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures")
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()

# M89 (D-097): forms on QindaTK's positioned controls through the service
# (tst_qindabase_m89) and the real window (tst_qindabase_ui_m89).
foreach(name IN ITEMS tst_qindabase_m89 tst_qindabase_ui_m89 tst_qindabase_events)
    add_executable(${name} ${name}.cpp base_app_test_support.h)
    target_link_libraries(${name} PRIVATE qindabase_app qindabase_appplugin Qt6::Test)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures")
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()
