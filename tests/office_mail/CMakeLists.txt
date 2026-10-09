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

# M64 (D-072): message files, contacts, safe HTML and Outlook .msg. No
# network, no servers: every fixture is written by the test itself (a CFB
# writer builds the .msg files, cfb_writer.h). Qt Gui only for QImage and a
# QTextDocument that checks what sanitized HTML asks to load.
function(qqo_add_mail_unit_test name)
    add_executable(${name} ${name}.cpp ${ARGN})
    target_link_libraries(${name} PRIVATE office_mail Qt6::Test Qt6::Gui)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QQO_MAIL_SECRET_BACKEND=memory")
endfunction()

qqo_add_mail_unit_test(tst_mail_messages)
qqo_add_mail_unit_test(tst_mail_contacts)
qqo_add_mail_unit_test(tst_mail_contact_csv)
qqo_add_mail_unit_test(tst_mail_contact_store)
qqo_add_mail_unit_test(tst_mail_contact_groups)
qqo_add_mail_unit_test(tst_mail_html tst_mail_html_support.h tst_mail_html_samples.h)
qqo_add_mail_unit_test(tst_mail_msg tst_mail_msg_parts.cpp tst_mail_msg_cfb.cpp tst_mail_msg_rtf.cpp
                       tst_mail_msg.h tst_mail_msg_support.h cfb_writer.h cfb_writer_msg.h)
# M76 (D-085): Outlook data files, built in-process by pst_builder.h (no
# .pst writer exists on Linux) — formats, encodings, items, corruption, size.
qqo_add_mail_unit_test(tst_mail_pst tst_mail_pst_items.cpp tst_mail_pst_corrupt.cpp tst_mail_pst.h
                       pst_builder.h pst_builder_ltp.h pst_builder_ndb.h pst_fixture.h cfb_writer.h cfb_writer_msg.h)
# M76 (D-085): CardDAV against a local fake server (fake_carddav_server.h).
qqo_add_mail_unit_test(tst_mail_carddav fake_carddav_server.h)
# M81 (D-090): the rule engine, categories / follow-up flags / search
# folders, and automatic replies over Sieve against a local fake
# ManageSieve server (fake_managesieve_server.h).
qqo_add_mail_unit_test(tst_mail_rules)
qqo_add_mail_unit_test(tst_mail_organize)
qqo_add_mail_unit_test(tst_mail_sieve fake_managesieve_server.h)
add_subdirectory(native_secrets)
