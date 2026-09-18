# SPDX-License-Identifier: GPL-3.0-or-later

# office_quick: the suite's document shell on QtQuick + QindaTK (plan
# docs/plans/qindatk-conversion.md §3.2); replaces office_shell app by app.
# It is the ONLY library that links the QindaQt desktop's static archives
# (AGENT-CONTRACT: every other lib and their tests build without the
# desktop installed) and the only one that links QtPrintSupport.
find_library(QINDAQT_MENU_PROTOCOL_LIB NAMES qindaqt_global_menu_protocol)
find_library(QINDAQT_MENU_EXPORTER_LIB NAMES qindaqt_global_menu_exporter)
find_library(QINDAQT_MENU_DBUSMENU_LIB NAMES qindaqt_global_menu_dbusmenu)
find_path(QINDAQT_MENU_INCLUDE_DIR qindaqt/shell/global_menu/protocol/menu_tree.h)

foreach(dependency IN ITEMS
        QINDAQT_MENU_PROTOCOL_LIB QINDAQT_MENU_EXPORTER_LIB
        QINDAQT_MENU_DBUSMENU_LIB QINDAQT_MENU_INCLUDE_DIR)
    if(NOT ${dependency})
        message(FATAL_ERROR "office_quick requires the QindaQt desktop libraries (${dependency}). "
                            "Install gui-wm/qindaqt-desktop first.")
    endif()
endforeach()

qt_add_library(office_quick STATIC)

set(office_quick_qml
    qml/AboutDialog.qml
    qml/CloseConsentDialog.qml
    qml/FindBar.qml
    qml/MenuBarFromModel.qml
    qml/MenuFromNode.qml
    qml/OfficeStatusBar.qml
    qml/OfficeWindow.qml
    qml/PreferencesDialog.qml
    qml/PrintDialog.qml
    qml/PrintPreview.qml
    qml/ThemeBridge.qml
    qml/ToolBarFromModel.qml
)
# Published flat (OfficeWindow.qml, not qml/OfficeWindow.qml), like QindaTK.
foreach(qml_file IN LISTS office_quick_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

# AGENT-CONTRACT: URI QindaOffice.Shell; QML types are the C++ QML_ELEMENTs
# below plus the QML files. Consumers link office_quick AND
# office_quickplugin (static module: the plugin's init object registers
# the types).
qt_add_qml_module(office_quick
    URI QindaOffice.Shell
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK
    SOURCES
        src/action_model.cpp src/action_model.h
        src/app_runner.cpp src/app_runner.h
        src/app_services.h
        src/desktop_settings.cpp src/desktop_settings.h
        src/document_host.cpp src/document_host.h
        src/document_host_persistence.cpp
        src/global_menu_export.cpp src/global_menu_export.h
        src/menu_source.cpp src/menu_source.h
        src/menu_tree.cpp src/menu_tree.h
        src/office_application.cpp src/office_application.h
        src/preferences.cpp src/preferences.h
        src/print_service.cpp src/print_service.h
        src/single_instance.cpp src/single_instance.h
        src/standard_actions.cpp src/standard_actions.h
        src/template_loader.cpp src/template_loader.h
    QML_FILES ${office_quick_qml}
)

# The QML import ROOT the toolkit's modules live under (the directory that
# contains QindaTK/qmldir and QindaTK/QindaQt/qmldir). AGENT-GUARD: the
# package config hands out the module directory itself; adding THAT as an
# import path resolved `import QindaTK` only through the linked library's
# resources and left `QindaTK.QindaQt` (the desktop's token bridge, a
# separate library) "not installed" — the chrome silently fell back to the
# preset on the desktop.
set(QindaTK_QML_ROOT "${QindaTK_QML_DIR}")
get_filename_component(_qindatk_leaf "${QindaTK_QML_DIR}" NAME)
if(_qindatk_leaf STREQUAL "QindaTK")
    get_filename_component(QindaTK_QML_ROOT "${QindaTK_QML_DIR}" DIRECTORY)
endif()

target_compile_definitions(office_quick
    PUBLIC
        QQO_VERSION="${PROJECT_VERSION}"
        # Tests and apps add it to their engine's import path (the system
        # Qt path needs no help, a development prefix does).
        QQO_QINDATK_QML_ROOT="${QindaTK_QML_ROOT}"
    PRIVATE
        QQO_HAVE_POPPLER
        QT_NO_CAST_FROM_ASCII
)

target_include_directories(office_quick
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        ${QINDAQT_MENU_INCLUDE_DIR}
)

# GuiPrivate: QtGui/private/qdesktopunixservices_p.h drives the per-window
# Wayland appmenu announcement (Qt 6.11 private API).
target_link_libraries(office_quick
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::DBus
        Qt6::PrintSupport
        QindaTK::qindatk
        office_core
    PRIVATE
        Qt6::GuiPrivate
        PkgConfig::POPPLER_QT6
        # AGENT-NOTE: static-archive link order matters - dependents before
        # dependencies (QindaQt_Apps pattern).
        ${QINDAQT_MENU_DBUSMENU_LIB}
        ${QINDAQT_MENU_EXPORTER_LIB}
        ${QINDAQT_MENU_PROTOCOL_LIB}
)

qqo_set_strict_warnings(office_quick)
