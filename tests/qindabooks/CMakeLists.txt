# SPDX-License-Identifier: GPL-3.0-or-later
# QindaBooks' gates over the app (M47): BooksService's QML API over a real
# company file (tst_qindabooks_service, tst_qindabooks_banking), the host's
# commands, workspace, Save As and print source (tst_qindabooks_host), and
# the panel export through the desktop's own validator
# (tst_qindabooks_menu_export). Every test links the app's static QML
# module, so a source added to apps/qindabooks/CMakeLists.txt is in all.
# tst_qindabooks_connections (M48) drives the Mercury/Stripe API against
# tests/books_connect/fake_bank_api.h; every test runs with the in-memory
# keyring (QQO_BOOKS_SECRET_BACKEND=memory), never the user's.
# M63 (D-071): tst_qindabooks_essentials (the service calls that made the
# engine's statements, recurring schedules, budgets, sales-tax payments,
# logo, list transfer and reports-to-Excel reachable) and
# tst_qindabooks_dialogs (their dialogs through the real window,
# books_window_scene.h). M68 (D-077): tst_qindabooks_m68 (purchase orders,
# employees and time, the check queue and alignment through the service
# and the host) and tst_qindabooks_m68_dialogs (their dialogs at 1280 and
# 520 px). M71 (D-081): tst_qindabooks_payroll (PayrollService: setup,
# employees, Run Payroll, liabilities, owner pay, the planner, reminders)
# and tst_qindabooks_payroll_ui (the Payroll workspace and dialogs through
# the real window at 1280 and 520 px).
foreach(name IN ITEMS tst_qindabooks_service tst_qindabooks_banking tst_qindabooks_host tst_qindabooks_menu_export
                      tst_qindabooks_ui tst_qindabooks_connections tst_qindabooks_essentials tst_qindabooks_dialogs
                      tst_qindabooks_m68 tst_qindabooks_m68_dialogs tst_qindabooks_payroll tst_qindabooks_payroll_ui)
    add_executable(${name} ${name}.cpp books_app_test_support.h books_window_scene.h)
    target_link_libraries(${name} PRIVATE qindabooks_app qindabooks_appplugin Qt6::Test Qt6::Network)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures")
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QQO_BOOKS_SECRET_BACKEND=memory;TZ=UTC")
endforeach()
