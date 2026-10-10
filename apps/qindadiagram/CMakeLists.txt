# SPDX-License-Identifier: GPL-3.0-or-later
# QindaDiagram (M50, D-050): flowcharts, org charts and network diagrams —
# one .qdiagram drawing per window over diagram_engine. The C++ side is the
# host (commands, menus, the print callback), DiagramService (the QML API),
# the canvas adapter (QindaTK's canvas boundary over the current page) and
# the page view (the engine's painter under the canvas); the QML module is
# the window, the shapes panel, the canvas input and the inspector.

set(qindadiagram_qml
    qml/DiagramWindow.qml
    qml/DiagramRibbonLayout.qml
    qml/ShapesPanel.qml
    qml/DiagramCanvasView.qml
    qml/CanvasInput.qml
    qml/TextEditorOverlay.qml
    qml/InspectorPanel.qml
    qml/InspectorShape.qml
    qml/InspectorText.qml
    qml/InspectorData.qml
    qml/InspectorPage.qml
    qml/PageBar.qml
    qml/PageSetupDialog.qml
    qml/ColorDialog.qml
    qml/ReplaceDialog.qml
    # M87 (D-095): Visio's layers, links, comments, Re-Layout options, data.
    qml/LayersDialog.qml
    qml/AssignLayersDialog.qml
    qml/LinkDialog.qml
    qml/LayoutDialog.qml
    qml/LinkDataDialog.qml
    qml/ExternalDataDialog.qml
    qml/CommentsPane.qml
    qml/CommentEntry.qml
)
foreach(qml_file IN LISTS qindadiagram_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

# AGENT-CONTRACT: the app's QML module is a STATIC library so the tests can
# instantiate DiagramWindow.qml and the host; the executable is main.cpp.
qt_add_library(qindadiagram_app STATIC)
qt_add_qml_module(qindadiagram_app
    URI QindaOffice.Diagram.App
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK QindaOffice.Shell
    SOURCES
        src/canvas_adapter.cpp src/canvas_adapter.h
        src/diagram_host.cpp src/diagram_host.h
        src/diagram_host_actions.cpp
        src/diagram_host_menus.cpp
        src/diagram_host_m87.cpp
        src/diagram_service.cpp src/diagram_service.h
        src/diagram_service_data.cpp
        src/diagram_service_edit.cpp
        src/diagram_service_pages.cpp
        src/diagram_service_io.cpp
        src/diagram_service_review.cpp
        src/diagram_service_structure.cpp
        src/page_view.cpp src/page_view.h
    QML_FILES ${qindadiagram_qml}
)
target_include_directories(qindadiagram_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qindadiagram_app PUBLIC
    office_quick office_quickplugin diagram_engine Qt6::PrintSupport)
target_compile_definitions(qindadiagram_app PRIVATE QT_NO_CAST_FROM_ASCII)
qqo_set_strict_warnings(qindadiagram_app)

qt_add_executable(qindadiagram main.cpp)
target_link_libraries(qindadiagram PRIVATE qindadiagram_app qindadiagram_appplugin)
target_compile_definitions(qindadiagram PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaDiagram"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindadiagram)
qqo_desktop_runtime(qindadiagram)
install(TARGETS qindadiagram RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaDiagram.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
