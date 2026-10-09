# SPDX-License-Identifier: GPL-3.0-or-later

# office_mail (M43, D-043; docs/mail.md): mail/calendar ACCOUNTS for
# QindaMail — the account list (accounts.json, no secrets), provider presets,
# Thunderbird-format autoconfig, OAuth 2.0 (PKCE + loopback redirect), the
# client-ID registry, keyring secrets (QtKeychain plus a client-owned native
# imported Password reader, D-067) and XOAUTH2 helpers. M64 (D-072,
# docs/mail-interchange.md): the address book (contacts, vCard, Outlook CSV),
# message files (RFC 5322 reader/writer, mbox, Outlook .msg over an MS-CFB
# reader and compressed RTF) and safe HTML for display (sanitizer, renderer).
# M76 (D-085, docs/mail-interchange.md): Outlook data files (.pst/.ost: the
# NDB, LTP and messaging layers of MS-PST), the MAPI mapping .msg and .pst
# share (mail, contacts, contact groups, appointments, tasks), contact
# groups in vCard, and CardDAV address-book sync.
# Qt Core/Network/Qml/DBus + qt6keychain + QindaTK; no Quick, no desktop archives
# (AGENTS.md rules 8/9). Qt Gui only for QDesktopServices (opening the
# browser) and through office_core.
#
# AGENT-CONTRACT: URI QindaOffice.Mail; the QML types are the C++
# QML_ELEMENTs below (AccountStore, AutoConfig, OAuth2Session, MailSecrets,
# ContactStore, CardDavProvider).
# The app lane (M44) adds its QML files to this module. Consumers link
# office_mail AND office_mailplugin (static module: the plugin's init object
# registers the types).
qt_add_library(office_mail STATIC)

qt_add_qml_module(office_mail
    URI QindaOffice.Mail
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK
    SOURCES
        src/account_store.cpp src/account_store.h
        src/autoconfig.cpp src/autoconfig.h
        src/bounded_http.cpp src/bounded_http.h
        src/client_config_parser.cpp src/client_config_parser.h
        src/loopback_receiver.cpp src/loopback_receiver.h
        src/mail_account.cpp src/mail_account.h
        src/mail_account_validation.cpp
        src/mail_result.h
        src/mail_secrets.cpp src/mail_secrets.h
        src/cfb_reader.cpp src/cfb_reader.h
        src/carddav_dialogue_p.h
        src/carddav_jobs.cpp src/carddav_jobs.h
        src/carddav_provider.cpp src/carddav_provider.h
        src/carddav_sync.cpp
        src/carddav_xml.cpp src/carddav_xml.h
        src/contact.cpp src/contact.h
        src/contact_rules.cpp
        src/contact_csv.cpp src/contact_csv.h src/contact_csv_p.h
        src/contact_csv_row.cpp
        src/contact_store.cpp src/contact_store.h
        src/contact_store_io.cpp
        src/contact_store_photos.cpp
        src/contact_store_query.cpp
        src/contact_store_groups.cpp
        src/contact_store_sync.cpp
        src/html_sanitizer.cpp src/html_sanitizer.h src/html_sanitizer_p.h
        src/html_sanitizer_css.cpp
        src/html_sanitizer_hidden.cpp
        src/html_sanitizer_tags.cpp
        src/html_sanitizer_tree.cpp
        src/html_sanitizer_urls.cpp
        src/html_tokenizer.cpp src/html_tokenizer.h
        src/html_tokenizer_entities.cpp
        src/mbox.cpp src/mbox.h
        src/mapi_mail.cpp src/mapi_mail.h
        src/mapi_named.cpp src/mapi_named.h
        src/mapi_object.h
        src/msg_headers.cpp src/msg_headers.h
        src/msg_objects.cpp src/msg_objects.h
        src/msg_properties.cpp src/msg_properties.h
        src/msg_reader.cpp src/msg_reader.h
        src/outlook_items.h
        src/outlook_calendar.cpp
        src/outlook_contact.cpp
        src/pst_crypt.cpp src/pst_crypt.h
        src/pst_ltp.cpp src/pst_ltp.h
        src/pst_ndb.cpp src/pst_ndb.h
        src/pst_objects.cpp src/pst_objects.h
        src/pst_reader.cpp src/pst_reader.h
        src/plain_text_html.cpp src/plain_text_html.h
        src/rtf_codepage.cpp src/rtf_codepage.h
        src/rtf_compressed.cpp src/rtf_compressed.h
        src/rtf_text.cpp src/rtf_text.h
        src/vcard.h src/vcard_p.h
        src/vcard_reader.cpp src/vcard_reader_p.h
        src/vcard_reader_properties.cpp
        src/vcard_text.cpp
        src/vcard_writer.cpp
        src/message_content.h
        src/message_render.cpp src/message_render.h
        src/mime_header.cpp src/mime_header.h
        src/rfc822_reader.cpp src/rfc822_reader.h
        src/rfc822_writer.cpp src/rfc822_writer.h
        src/imported_password_backend.cpp src/imported_password_backend.h
        src/imported_password_codec.cpp src/imported_password_codec.h
        src/imported_password_reader.cpp src/imported_password_reader.h
        src/imported_password_wire.h
        src/net_policy.cpp src/net_policy.h
        src/oauth2_profiles.cpp src/oauth2_profiles.h
        src/oauth2_session.cpp src/oauth2_session.h src/oauth2_session_p.h
        src/oauth2_tokens.cpp
        src/oauth_client_registry.cpp src/oauth_client_registry.h
        src/pkce.cpp src/pkce.h
        src/provider_presets.cpp src/provider_presets.h
        src/secret_backend.cpp src/secret_backend.h
        src/xoauth2.cpp src/xoauth2.h
)

target_include_directories(office_mail
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_compile_definitions(office_mail PRIVATE QT_NO_CAST_FROM_ASCII)

target_link_libraries(office_mail
    PUBLIC
        Qt6::Core
        Qt6::Network
        Qt6::Qml
        QindaTK::qindatk
        office_core
    PRIVATE
        Qt6::Gui
        Qt6::DBus
        # Same dependency as office_ai's SecretStore (dev-libs/qtkeychain,
        # Qt 6 build); linked by library name, headers <qt6keychain/…>.
        qt6keychain
)

qqo_set_strict_warnings(office_mail)
