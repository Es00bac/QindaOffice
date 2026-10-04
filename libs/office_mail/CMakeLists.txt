# SPDX-License-Identifier: GPL-3.0-or-later

# office_mail (M43, D-043; docs/mail.md): mail/calendar ACCOUNTS for
# QindaMail — the account list (accounts.json, no secrets), provider presets,
# Thunderbird-format autoconfig, OAuth 2.0 (PKCE + loopback redirect), the
# client-ID registry, keyring secrets (QtKeychain plus a client-owned native
# imported Password reader, D-067) and XOAUTH2 helpers.
# Qt Core/Network/Qml/DBus + qt6keychain + QindaTK; no Quick, no desktop archives
# (AGENTS.md rules 8/9). Qt Gui only for QDesktopServices (opening the
# browser) and through office_core.
#
# AGENT-CONTRACT: URI QindaOffice.Mail; the QML types are the C++
# QML_ELEMENTs below (AccountStore, AutoConfig, OAuth2Session, MailSecrets).
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
