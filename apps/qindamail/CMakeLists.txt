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
    qml/PeopleWorkspace.qml
    qml/ContactEditor.qml
    qml/ContactValueList.qml
    qml/ContactGroupEditor.qml
    qml/PstImportDialog.qml
    qml/AddressBookSection.qml
    # M81 (D-090)
    qml/AutoRepliesDialog.qml
    qml/CategoriesDialog.qml
    qml/CategorizeMenu.qml
    qml/CategoryField.qml
    qml/FollowUpDialog.qml
    qml/MailDateField.qml
    qml/MessageTagsBar.qml
    qml/RuleActionList.qml
    qml/RuleConditionList.qml
    qml/RuleEditor.qml
    qml/RulesDialog.qml
    qml/RunRulesDialog.qml
    qml/SearchFolderDialog.qml
    qml/TaskDialog.qml
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
        src/imap_source_fetcher.cpp src/imap_source_fetcher.h
        src/launch_arguments.cpp src/launch_arguments.h
        src/mail_host.cpp src/mail_host.h
        src/mail_host_commands.cpp
        src/mail_service.cpp src/mail_service.h
        src/mail_service_accounts.cpp
        src/mail_service_calendar.cpp
        src/mail_service_compose.cpp
        src/mail_service_ical.cpp
        src/ics_tasks.cpp src/ics_tasks.h
        src/mail_service_folders.cpp
        src/mail_service_messages.cpp
        src/mail_service_sync.cpp
        src/mail_service_tasks.cpp
        src/mail_service_source.cpp
        src/mail_service_files.cpp
        src/mail_service_contacts.cpp
        src/mail_service_meetings.cpp
        src/mail_service_invitations.cpp
        src/mail_service_outlook.cpp
        src/outlook_records.cpp src/outlook_records.h
        src/pst_import.cpp src/pst_import.h
        src/message_document.cpp src/message_document.h
        src/message_list_model.cpp src/message_list_model.h
        src/message_print.cpp src/message_print.h
        src/message_view.cpp src/message_view.h
        src/provider_calls.cpp src/provider_calls.h
        src/recipient_completer.cpp src/recipient_completer.h
        # M81 (D-090): rules, categories, flags, search folders, automatic replies
        src/imap_extras.cpp src/imap_extras.h
        src/mail_host_organizer.cpp
        src/mail_organizer.cpp src/mail_organizer.h
        src/mail_organizer_actions.cpp
        src/mail_organizer_board.cpp
        src/mail_organizer_conversations.cpp
        src/mail_organizer_replies.cpp
        src/mail_organizer_rules.cpp
        src/mail_organizer_search.cpp
        src/mail_organizer_tags.cpp
        src/smtp_redirector.cpp src/smtp_redirector.h
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
