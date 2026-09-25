# SPDX-License-Identifier: GPL-3.0-or-later

# office_mail tests (M43). Headless and offline: every server is a local
# FakeHttpServer on 127.0.0.1, and the secrets backend is in memory
# (QQO_MAIL_SECRET_BACKEND=memory also keeps a default-constructed
# MailSecrets away from the user's real keyring).
function(qqo_add_mail_test name)
    add_executable(${name} ${name}.cpp fake_http_server.cpp fake_http_server.h secret_sink.h oauth_harness.h)
    target_link_libraries(${name} PRIVATE office_mail office_mailplugin Qt6::Test Qt6::Network)
    target_compile_definitions(${name} PRIVATE
        QQO_MAIL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QQO_MAIL_SECRET_BACKEND=memory")
endfunction()

qqo_add_mail_test(tst_mail_accounts)
qqo_add_mail_test(tst_mail_autoconfig)
qqo_add_mail_test(tst_mail_oauth)
