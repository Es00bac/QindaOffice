# SPDX-License-Identifier: GPL-3.0-or-later

# office_shell is the only library that links the QindaQt desktop's static
# archives (AGENT-CONTRACT: container-wm rule 11 - every other lib and their
# tests must build without the desktop installed). The desktop ships these
# archives and headers without CMake package configs, so resolve them
# explicitly, exactly like QindaQt_Apps/CMakeLists.txt does.
find_library(QINDAQT_MENU_PROTOCOL_LIB NAMES qindaqt_global_menu_protocol)
find_library(QINDAQT_MENU_EXPORTER_LIB NAMES qindaqt_global_menu_exporter)
find_library(QINDAQT_MENU_DBUSMENU_LIB NAMES qindaqt_global_menu_dbusmenu)
find_library(QINDAQT_MENU_WIDGETS_LIB NAMES qindaqt_global_menu_qt_widgets_adapter)
find_path(QINDAQT_MENU_INCLUDE_DIR qindaqt/shell/global_menu/protocol/menu_tree.h)

foreach(dependency IN ITEMS
        QINDAQT_MENU_PROTOCOL_LIB QINDAQT_MENU_EXPORTER_LIB
        QINDAQT_MENU_DBUSMENU_LIB QINDAQT_MENU_WIDGETS_LIB
        QINDAQT_MENU_INCLUDE_DIR)
    if(NOT ${dependency})
        message(FATAL_ERROR "office_shell requires the QindaQt desktop libraries (${dependency}). "
                            "Install gui-wm/qindaqt-desktop first.")
    endif()
endforeach()

add_library(office_shell STATIC
    src/document_window.cpp
    src/document_window.h
    src/global_menu_export.cpp
    src/global_menu_export.h
    src/office_application.cpp
    src/office_application.h
    src/standard_actions.cpp
    src/standard_actions.h
)

target_compile_definitions(office_shell
    PUBLIC
        QQO_VERSION="${PROJECT_VERSION}"
)

target_include_directories(office_shell
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
    PRIVATE
        ${QINDAQT_MENU_INCLUDE_DIR}
)

# GuiPrivate: QtGui/private/qdesktopunixservices_p.h drives the per-window
# Wayland appmenu announcement (Qt 6.11 private API, ebuild pins >=qtbase-6.11).
target_link_libraries(office_shell
    PUBLIC
        Qt6::Widgets
        Qt6::DBus
    PRIVATE
        Qt6::GuiPrivate
        # AGENT-NOTE: static-archive link order matters - dependents before
        # dependencies (QindaQt_Apps pattern).
        ${QINDAQT_MENU_WIDGETS_LIB}
        ${QINDAQT_MENU_DBUSMENU_LIB}
        ${QINDAQT_MENU_EXPORTER_LIB}
        ${QINDAQT_MENU_PROTOCOL_LIB}
)

qqo_set_strict_warnings(office_shell)

install(TARGETS office_shell ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice/office_shell
    FILES_MATCHING PATTERN "*.h")
