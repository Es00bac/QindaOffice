# SPDX-License-Identifier: GPL-3.0-or-later
# QindaBooks' gates over the app (M47): BooksService's QML API over a real
# company file (tst_qindabooks_service, tst_qindabooks_banking), the host's
# commands, workspace, Save As and print source (tst_qindabooks_host), and
# the panel export through the desktop's own validator
# (tst_qindabooks_menu_export). Every test links the app's static QML
# module, so a source added to apps/qindabooks/CMakeLists.txt is in all.
foreach(name IN ITEMS tst_qindabooks_service tst_qindabooks_banking tst_qindabooks_host tst_qindabooks_menu_export tst_qindabooks_ui)
    add_executable(${name} ${name}.cpp books_app_test_support.h)
    target_link_libraries(${name} PRIVATE qindabooks_app qindabooks_appplugin Qt6::Test)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures")
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()
