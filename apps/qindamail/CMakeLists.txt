# SPDX-License-Identifier: GPL-3.0-or-later
# QindaMail (M44, D-042; docs/mail.md#qindamail-app): mail, calendar and
# tasks over QindaTK's LocalProductivityStore and protocol providers, the
# accounts of office_mail and the Exchange/Graph calendar adapters of
# mail_exchange. One main window is a MailHost (a DocumentHost without a
# Core::Document); every main window of the process shares ONE MailService.
#
# AGENT-CONTRACT: the QML module is QindaOffice.Mail.App (static, like the
# other apps) so the tests instantiate the real window. Its QML imports
# QindaOffice.Shell, QindaOffice.Mail and QindaTK. The QML file list below
# is fixed by the app lead's lane (docs/plans/ribbon-and-mail.md); a file
# added there is added here in the same change.
set(qindamail_qml
    qml/MailWindow.qml
    qml/MailRibbonLayout.qml
    qml/MailWorkspace.qml
    qml/FolderPane.qml
    qml/MessageList.qml
    qml/MessageRow.qml
    qml/ReadingPane.qml
    qml/CalendarWorkspace.qml
    qml/EventDialog.qml
    qml/TasksWorkspace.qml
    qml/ComposerWindow.qml
    qml/AccountDialog.qml
    qml/MailPreferencesPage.qml
)
foreach(qml_file IN LISTS qindamail_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

qt_add_library(qindamail_app STATIC)
qt_add_qml_module(qindamail_app
    URI QindaOffice.Mail.App
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK QindaOffice.Shell QindaOffice.Mail
    SOURCES
        src/account_runtime.cpp src/account_runtime.h
        src/account_runtime_credentials.cpp
        src/draft_builder.cpp src/draft_builder.h
        src/folder_model.cpp src/folder_model.h
        src/launch_arguments.cpp src/launch_arguments.h
        src/mail_host.cpp src/mail_host.h
        src/mail_host_commands.cpp
        src/mail_service.cpp src/mail_service.h
        src/mail_service_accounts.cpp
        src/mail_service_calendar.cpp
        src/mail_service_compose.cpp
        src/mail_service_ical.cpp
        src/mail_service_folders.cpp
        src/mail_service_messages.cpp
        src/mail_service_sync.cpp
        src/mail_service_tasks.cpp
        src/message_list_model.cpp src/message_list_model.h
        src/provider_calls.cpp src/provider_calls.h
    QML_FILES ${qindamail_qml}
)
target_include_directories(qindamail_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qindamail_app PUBLIC
    office_quick office_quickplugin
    office_mail office_mailplugin
    mail_exchange
    Qt6::Network)
target_compile_definitions(qindamail_app PRIVATE QT_NO_CAST_FROM_ASCII)
qqo_set_strict_warnings(qindamail_app)

qt_add_executable(qindamail main.cpp)
target_link_libraries(qindamail PRIVATE qindamail_app qindamail_appplugin)
target_compile_definitions(qindamail PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaMail"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindamail)
qqo_desktop_runtime(qindamail)
install(TARGETS qindamail RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaMail.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
