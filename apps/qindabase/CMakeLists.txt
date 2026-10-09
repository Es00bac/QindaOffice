# SPDX-License-Identifier: GPL-3.0-or-later
# QindaBase (M49, D-049): the suite's database — one .qbase SQLite file per
# window over base_engine. The C++ side is the document adapter, the host
# (commands, menus, print source) and BaseService (the QML API); the QML
# module is the window, the navigation pane and the views and designers,
# built on QindaTK's EditableGrid, FormDesigner, ReportDesigner,
# DatabaseSchemaView, QueryBuilder/PivotView and DiagramCanvas.
#
# AGENT-NOTE (runtime dependency, optional): Access import runs the
# mdb-tools programs when they are on PATH (base_access.h); nothing links
# them and the command says what to install when they are missing. Excel
# import and export go through calc_io (base_xlsx_import.h,
# base_xlsx_export.h).

set(qindabase_qml
    qml/BaseWindow.qml
    qml/BaseRibbonLayout.qml
    qml/BaseCommands.qml
    qml/NavigationPane.qml
    qml/StartView.qml
    qml/DatasheetView.qml
    qml/DatasheetFooter.qml
    qml/PivotPanel.qml
    qml/TableDesignView.qml
    qml/FieldProperties.qml
    qml/QueryDesignView.qml
    qml/QueryGridColumn.qml
    qml/FormView.qml
    qml/ReportView.qml
    qml/RelationshipsView.qml
    qml/SqlView.qml
    qml/PlanDialog.qml
    qml/RelationshipDialog.qml
    qml/ParametersDialog.qml
    qml/ImportCsvDialog.qml
    qml/AccessImportDialog.qml
    qml/FileFieldPanel.qml
    qml/FormWizardDialog.qml
    qml/ReportWizardDialog.qml
    qml/BasePreferencesPage.qml
)
foreach(qml_file IN LISTS qindabase_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

# AGENT-CONTRACT: the app's QML module is a STATIC library so the tests can
# instantiate BaseWindow.qml and the host; the executable is main.cpp.
qt_add_library(qindabase_app STATIC)
qt_add_qml_module(qindabase_app
    URI QindaOffice.Base.App
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK QindaOffice.Shell
    SOURCES
        src/base_document.cpp src/base_document.h
        src/base_host.cpp src/base_host.h
        src/base_host_actions.cpp
        src/base_service.cpp src/base_service.h
        src/base_service_files.cpp
        src/base_service_forms.cpp
        src/base_service_import.cpp
        src/base_service_io.cpp
        src/base_service_queries.cpp
        src/base_service_reports.cpp
        src/base_service_tables.cpp
        src/base_xlsx_export.cpp src/base_xlsx_export.h
        src/base_xlsx_import.cpp src/base_xlsx_import.h
        src/design_files.h
        src/datasheet_edit.cpp
        src/datasheet_model.cpp src/datasheet_model.h
        src/datasheet_source.cpp src/datasheet_source.h
        src/datasheet_totals.cpp
        src/pivot_source.cpp src/pivot_source.h
    QML_FILES ${qindabase_qml}
)
target_include_directories(qindabase_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qindabase_app PUBLIC
    office_quick office_quickplugin base_engine calc_io Qt6::PrintSupport)
target_compile_definitions(qindabase_app PRIVATE QT_NO_CAST_FROM_ASCII)
qqo_set_strict_warnings(qindabase_app)

qt_add_executable(qindabase main.cpp)
target_link_libraries(qindabase PRIVATE qindabase_app qindabase_appplugin)
target_compile_definitions(qindabase PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaBase"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindabase)
qqo_desktop_runtime(qindabase)
install(TARGETS qindabase RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaBase.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
