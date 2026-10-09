# SPDX-License-Identifier: GPL-3.0-or-later
# QindaShow on QtQuick + QindaTK (docs/plans/qindatk-conversion.md §5.3).
# The document, the canvas item, the controllers and the host are one
# STATIC library with the app's QML module (URI QindaOffice.Show) so the
# tests instantiate the real window; the executable is main.cpp over it.
set(qindashow_qml
    qml/ApplyMasterDialog.qml
    qml/ChartDataDialog.qml
    qml/CommentEntry.qml
    qml/CommentsPane.qml
    qml/FormatBackgroundPane.qml
    qml/InsertTableDialog.qml
    qml/LayoutGallery.qml
    qml/MasterDialog.qml
    qml/MasterPane.qml
    qml/NotesPane.qml
    qml/PlayerMedia.qml
    qml/PresenterView.qml
    qml/ReuseSlidesDialog.qml
    qml/ShowPlayer.qml
    qml/ShowWindow.qml
    qml/ShowRibbonLayout.qml
    qml/SlideCanvasView.qml
    qml/SlideEditor.qml
    qml/SlidePanel.qml
    qml/SlideSizeDialog.qml
    qml/SlideSorter.qml
    qml/SplitCellsDialog.qml
    qml/TextStylesPane.qml
)
foreach(qml_file IN LISTS qindashow_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

qt_add_library(qindashow_app STATIC)
qt_add_qml_module(qindashow_app
    URI QindaOffice.Show
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK QindaOffice.Shell QindaOffice.Ai
    SOURCES
        src/pdf_export.cpp src/pdf_export.h
        src/presentation_controller.cpp src/presentation_controller.h
        src/show_ai.cpp src/show_ai.h
        src/show_arrange.cpp src/show_arrange.h
        src/show_arrange_menus.cpp
        src/show_background.cpp src/show_background.h
        src/show_chart_data.cpp src/show_chart_data.h
        src/show_clipboard.cpp src/show_clipboard.h
        src/show_commands.cpp src/show_commands.h
        src/show_comments.cpp src/show_comments.h
        src/show_design_actions.cpp src/show_design_actions.h
        src/show_design_tab.cpp src/show_design_tab.h
        src/show_document.cpp src/show_document.h
        src/show_host.cpp src/show_host.h
        src/show_host_ai.cpp
        src/show_host_design.cpp
        src/show_host_editing.cpp
        src/show_tools.cpp src/show_tools.h
        src/slide_factory.h
        src/show_host_menus.cpp
        src/show_host_slides.cpp
        src/show_master_actions.cpp
        src/show_master_view.cpp src/show_master_view.h
        src/show_media_actions.cpp src/show_media_actions.h
        src/show_sections.cpp src/show_sections.h
        src/show_slide_layouts.cpp src/show_slide_layouts.h
        src/show_table_actions.cpp src/show_table_actions.h
        src/show_toolbox.cpp src/show_toolbox.h
        src/slide_canvas_item.cpp src/slide_canvas_item.h
        src/slide_canvas_item_input.cpp
        src/slide_canvas_item_marks.cpp
        src/slide_canvas_item_order.cpp
        src/slide_canvas_item_snap.cpp
        src/slide_canvas_item_table.cpp
        src/slide_items.cpp src/slide_items.h
        src/text_bridge.cpp src/text_bridge.h
    QML_FILES ${qindashow_qml}
)
# M75 (D-084): the media player QML is a plain resource loaded at run time
# (show_media_actions.h), NOT a module file — the app must build and run
# without Qt Multimedia, which only that file imports.
qt_add_resources(qindashow_app "qindashow_media"
    PREFIX "/qindashow/media"
    BASE media
    FILES media/MediaSurface.qml
)
target_include_directories(qindashow_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qindashow_app PUBLIC office_quick office_quickplugin office_ai office_aiplugin show_model)
target_compile_definitions(qindashow_app PRIVATE QT_NO_CAST_FROM_ASCII)
qqo_set_strict_warnings(qindashow_app)

qt_add_executable(qindashow main.cpp)
target_link_libraries(qindashow PRIVATE qindashow_app qindashow_appplugin)
target_compile_definitions(qindashow PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaShow"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindashow)
qqo_desktop_runtime(qindashow)
install(TARGETS qindashow RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/templates/QindaShow
    DESTINATION ${CMAKE_INSTALL_DATADIR}/qindaoffice/templates)
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaShow.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
