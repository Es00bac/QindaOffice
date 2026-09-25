# SPDX-License-Identifier: GPL-3.0-or-later
# QindaMail's gates (M44, docs/verification.md#m44-gates): the service over a
# temp tree with an in-memory keyring, drafts and mailto, accounts and the
# protocol objects they build, calendar and tasks, the host's commands and
# panel export, and send/sync against tiny local POP3 and SMTP servers on
# 127.0.0.1 (tst_qindamail_flow). No real network. Every test links the
# app's static QML module, so a source added to apps/qindamail is in every
# test.
foreach(name IN ITEMS tst_qindamail_drafts tst_qindamail_service tst_qindamail_accounts
                      tst_qindamail_calendar tst_qindamail_host tst_qindamail_flow tst_qindamail_ui)
    add_executable(${name} ${name}.cpp mail_test_support.h fake_mail_server.h)
    target_link_libraries(${name} PRIVATE qindamail_app qindamail_appplugin
        Qt6::Test Qt6::Network Qt6::GuiPrivate)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures")
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QQO_MAIL_SECRET_BACKEND=memory")
endforeach()
