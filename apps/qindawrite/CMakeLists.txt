# SPDX-License-Identifier: GPL-3.0-or-later
# QindaWrite on QtQuick + QindaTK (docs/plans/qindatk-conversion.md §5.1).
# The document, the editing control, the controllers and the host are one
# STATIC library with the app's QML module (URI QindaOffice.Write) so the
# tests instantiate the real window; the executable is main.cpp over it.
set(qindawrite_qml
    qml/BordersShadingDialog.qml
    qml/DefineBulletDialog.qml
    qml/DefineMultilevelDialog.qml
    qml/DefineNumberDialog.qml
    qml/ListIndentsDialog.qml
    qml/NavigationPanel.qml
    qml/NumberingValueDialog.qml
    qml/StyleDialog.qml
    qml/StylesPanel.qml
    qml/FontDialog.qml
    qml/ParagraphDialog.qml
    qml/SortDialog.qml
    qml/TablePropertiesDialog.qml
    qml/TabsDialog.qml
    qml/TextToTableDialog.qml
    qml/FootnoteDialog.qml
    qml/HyperlinkDialog.qml
    qml/PageSetupDialog.qml
    qml/PictureDialog.qml
    qml/CommentsPanel.qml
    qml/CommentEntry.qml
    qml/FormFieldPopup.qml
    qml/SymbolDialog.qml
    qml/TableDialog.qml
    qml/WritePage.qml
    qml/WritePreferencesPage.qml
    qml/WriteRuler.qml
    qml/WriteWindow.qml
    qml/WriteRibbonLayout.qml
)
foreach(qml_file IN LISTS qindawrite_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

qt_add_library(qindawrite_app STATIC)
qt_add_qml_module(qindawrite_app
    URI QindaOffice.Write
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK QindaOffice.Shell QindaOffice.Ai
    SOURCES
        src/format_controller.cpp src/format_controller.h
        src/format_io.cpp src/format_io.h
        src/page_geometry.cpp src/page_geometry.h
        src/page_setup_io.cpp src/page_setup_io.h
        src/page_picture_tool.cpp src/page_picture_tool.h
        src/page_story_editor.cpp src/page_story_editor.h
        src/page_text_accessible.cpp src/page_text_accessible.h
        src/page_text_control.cpp src/page_text_control.h
        src/page_text_control_clipboard.cpp
        src/page_text_item.cpp src/page_text_item.h
        src/pdf_exporter.cpp src/pdf_exporter.h
        src/spell_checker.cpp src/spell_checker.h
        src/style_catalog.cpp src/style_catalog.h
        src/table_actions.cpp src/table_actions.h
        src/table_data.cpp src/table_data.h
        src/table_layout.cpp src/table_layout.h
        src/write_borders.cpp src/write_borders.h
        src/write_font.cpp src/write_font.h
        src/write_lists.cpp src/write_lists.h src/write_lists_dialogs.cpp
        src/write_navigation.cpp src/write_navigation.h
        src/write_painter.cpp src/write_painter.h
        src/write_style_draft.cpp src/write_style_draft.h
        src/write_styles.cpp src/write_styles.h src/write_styles_edit.cpp
        src/write_paragraph.cpp src/write_paragraph.h
        src/write_selection.h
        src/write_actions.cpp src/write_actions.h
        src/write_document.cpp src/write_document.h
        src/write_document_io.cpp
        src/write_comments.cpp src/write_comments.h
        src/write_controls.cpp src/write_controls.h
        src/write_footnotes.cpp src/write_footnotes.h
        src/write_host.cpp src/write_host.h
        src/write_host_ai.cpp
        src/write_tools.cpp src/write_tools.h
        src/write_host_menus.cpp
        src/write_host_text.cpp
        src/write_pagination.cpp src/write_pagination.h
        src/write_review_actions.cpp src/write_review_actions.h
        src/write_revisions.cpp src/write_revisions.h
        src/write_toc.cpp src/write_toc.h
        src/write_typography.cpp src/write_typography.h
    QML_FILES ${qindawrite_qml}
)
target_include_directories(qindawrite_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
# QuickPrivate: QAccessibleQuickItem (page_text_accessible.h) is a private
# header, exported with the Qt_6_PRIVATE_API tag; version-locked like the
# rest of the suite (ADR-0116).
target_link_libraries(qindawrite_app PUBLIC office_quick office_quickplugin office_ai office_aiplugin
    office_docx Qt6::QuickPrivate KF6::SonnetCore)
target_compile_definitions(qindawrite_app PRIVATE QT_NO_CAST_FROM_ASCII)
qqo_set_strict_warnings(qindawrite_app)

qt_add_executable(qindawrite main.cpp)
target_link_libraries(qindawrite PRIVATE qindawrite_app qindawrite_appplugin)
target_compile_definitions(qindawrite PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaWrite"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindawrite)
qqo_desktop_runtime(qindawrite)
install(TARGETS qindawrite RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/templates/QindaWrite
    DESTINATION ${CMAKE_INSTALL_DATADIR}/qindaoffice/templates)
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaWrite.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
