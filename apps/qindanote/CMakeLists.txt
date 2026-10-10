# SPDX-License-Identifier: GPL-3.0-or-later
# QindaNote on QtQuick + QindaTK (docs/plans/qindatk-conversion.md §5.4).
# The engine files (notebook, blocks layout, renderer, history) are shared
# with the tests through QINDANOTE_ENGINE_SOURCES; the QML module is the
# window and its parts.
set(QINDANOTE_ENGINE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/navigator_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/navigator_model.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_blocks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_blocks.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_document.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_document.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_host.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_host.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_host_ai.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_host_ink.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_host_live.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_host_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_live_extras.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_live_session.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_live_session.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_live_settings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_markdown.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_markdown.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_markdown_style.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_markdown_style.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_host_structure.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_host_text.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_page_history.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_page_history.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_page_item.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_page_item.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_page_item_blocks.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_page_item_ink.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_page_render.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_page_render.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_tools.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_tools.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_agent.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_agent_state.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_agent_actions.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_evidence.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_evidence.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/note_session_drafts.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notebook.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notebook.h
    PARENT_SCOPE)

set(qindanote_qml
    qml/NoteLinkDialog.qml
    qml/NoteSearchPanel.qml
    qml/NoteTableDialog.qml
    qml/NoteTagsDialog.qml
    qml/NoteTagsPanel.qml
    qml/NoteTextSlots.qml
    qml/NoteLivePanel.qml
    qml/NoteLiveSettingsDialog.qml
    qml/NoteLiveSetupDialog.qml
    qml/NoteNavigator.qml
    qml/NoteSessionPanel.qml
    qml/NoteFollowUp.qml
    qml/NotePage.qml
    qml/NotePreferencesPage.qml
    qml/NoteWindow.qml
    qml/NoteRibbonLayout.qml
)
foreach(qml_file IN LISTS qindanote_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

# AGENT-CONTRACT: the app's QML module is a STATIC library so the tests can
# instantiate NoteWindow.qml too; the executable is main.cpp over it.
qt_add_library(qindanote_app STATIC)
qt_add_qml_module(qindanote_app
    URI QindaOffice.Note
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK QindaOffice.Shell QindaOffice.Ai
    SOURCES
        src/navigator_model.cpp src/navigator_model.h
        src/note_blocks.cpp src/note_blocks.h
        src/note_document.cpp src/note_document.h
        src/note_host.cpp src/note_host.h
        src/note_host_ai.cpp
        src/note_host_ink.cpp
        src/note_host_live.cpp
        src/note_host_session.cpp
        src/note_live_extras.cpp
        src/note_live_session.cpp src/note_live_session.h
        src/note_live_settings.cpp
        src/note_markdown.cpp src/note_markdown.h
        src/note_markdown_style.cpp src/note_markdown_style.h
        src/note_word_export.cpp src/note_word_export.h
        src/note_host_structure.cpp
        src/note_host_text.cpp
        src/note_page_history.cpp src/note_page_history.h
        src/note_page_item.cpp src/note_page_item.h
        src/note_page_item_blocks.cpp
        src/note_page_item_ink.cpp
        src/note_page_render.cpp src/note_page_render.h
        src/note_tools.cpp src/note_tools.h
        src/note_agent.h src/note_agent_state.cpp src/note_agent_actions.cpp
        src/note_evidence.h src/note_evidence.cpp src/note_session_drafts.cpp
        src/notebook.cpp src/notebook.h
        src/note_clipboard.cpp src/note_clipboard.h
        src/note_host_format.cpp src/note_host_pages.cpp src/note_host_tags.cpp
        src/note_import.cpp src/note_import.h
        src/note_page_item_rich.cpp src/note_page_item_tags.cpp
        src/note_rich_export.h src/note_rich_export_md.cpp src/note_rich_export_word.cpp
        src/note_rich_import.cpp src/note_rich_import.h
        src/note_rich_render.cpp src/note_rich_render.h
        src/note_rich_style.cpp src/note_rich_style.h
        src/note_rich_text.cpp src/note_rich_text.h
        src/note_search.cpp src/note_search.h
        src/note_tag_summary.cpp src/note_tag_summary.h
        src/note_tags.cpp src/note_tags.h
        src/note_templates.cpp src/note_templates.h
    QML_FILES ${qindanote_qml}
)
target_include_directories(qindanote_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(Qt6 REQUIRED COMPONENTS Multimedia)
# poppler-qt6 reads prep-material PDFs (M56); QtMultimedia plays the session
# recording in the Session panel.
target_link_libraries(qindanote_app PUBLIC office_quick office_quickplugin office_ai office_aiplugin office_ink
    office_speech office_docx Qt6::Multimedia PkgConfig::POPPLER_QT6)
target_compile_definitions(qindanote_app PRIVATE QT_NO_CAST_FROM_ASCII)
qqo_set_strict_warnings(qindanote_app)

qt_add_executable(qindanote main.cpp)
target_link_libraries(qindanote PRIVATE qindanote_app qindanote_appplugin)
target_compile_definitions(qindanote PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaNote"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindanote)
qqo_desktop_runtime(qindanote)
install(TARGETS qindanote RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaNote.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
