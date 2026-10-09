# SPDX-License-Identifier: GPL-3.0-or-later
# QindaCalc on QtQuick + QindaTK (docs/plans/qindatk-conversion.md §5.2).
# The engine, controllers and the host are one STATIC library with the
# app's QML module (URI QindaOffice.Calc) so the tests instantiate the
# real window; the executable is main.cpp over it.
set(qindacalc_qml
    qml/CalcGrid.qml
    qml/CalcSheetTabs.qml
    qml/CalcWindow.qml
    qml/CalcRibbonLayout.qml
    qml/ChartDialog.qml
    qml/ChartWindows.qml
    qml/CommentDialog.qml
    qml/CondFormatDialog.qml
    qml/FilterPopover.qml
    qml/LinkDialog.qml
    qml/ProtectDialog.qml
    qml/UnprotectDialog.qml
    qml/SizeDialog.qml
    qml/ValidationDialog.qml
)
foreach(qml_file IN LISTS qindacalc_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

qt_add_library(qindacalc_app STATIC)
qt_add_qml_module(qindacalc_app
    URI QindaOffice.Calc
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK QindaOffice.Shell QindaOffice.Ai
    SOURCES
        src/calc_clipboard.cpp src/calc_clipboard.h
        src/calc_host.cpp src/calc_host.h
        src/calc_host_ai.cpp
        src/calc_tools.cpp src/calc_tools.h
        src/calc_agent.h src/calc_agent_state.cpp src/calc_agent_actions.cpp
        src/calc_host_format.cpp
        src/calc_host_features.cpp
        src/calc_host_rules.cpp
        src/calc_host_structure.cpp
        src/calc_host_text.cpp
        src/calc_host_tables.cpp
        src/calc_recovery.cpp src/calc_recovery.h
        src/calc_recovery_rules.cpp src/calc_recovery_rules.h
        src/calc_recovery_features.cpp src/calc_recovery_features.h
        src/calc_rules_ops.cpp
        src/calc_object_ops.cpp
        src/calc_cell_ops.cpp
        src/calc_feature_ops.cpp
        src/calc_table_ops.cpp
    src/calc_structure_ops.cpp
        src/calc_undo.cpp src/calc_undo.h
        src/chart_item.cpp src/chart_item.h
        src/chart_series.cpp src/chart_series.h
        src/spreadsheet_document.cpp src/spreadsheet_document.h
        src/spreadsheet_item.cpp src/spreadsheet_item.h
        src/spreadsheet_item_input.cpp
        src/spreadsheet_item_paint.cpp
        src/spreadsheet_item_outline.cpp
    QML_FILES ${qindacalc_qml}
)
target_include_directories(qindacalc_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qindacalc_app PUBLIC office_quick office_quickplugin office_ai office_aiplugin calc_io)
target_compile_definitions(qindacalc_app PRIVATE QT_NO_CAST_FROM_ASCII)
if(QQO_WITH_CHARTS)
    # The chart surface is the custom ChartItem (D-036); C++ only needs the
    # flag, and QtCharts is not involved anywhere.
    target_compile_definitions(qindacalc_app PUBLIC QQO_WITH_CHARTS)
endif()
qqo_set_strict_warnings(qindacalc_app)

qt_add_executable(qindacalc main.cpp)
target_link_libraries(qindacalc PRIVATE qindacalc_app qindacalc_appplugin)
target_compile_definitions(qindacalc PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaCalc"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindacalc)
qqo_desktop_runtime(qindacalc)
install(TARGETS qindacalc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/templates/QindaCalc
    DESTINATION ${CMAKE_INSTALL_DATADIR}/qindaoffice/templates)
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaCalc.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
