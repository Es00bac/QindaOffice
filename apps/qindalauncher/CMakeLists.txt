# SPDX-License-Identifier: GPL-3.0-or-later
# The suite's launcher (docs/plans/qindatk-conversion.md §5.5). It hosts no
# document, so it links office_quick only for the desktop theme bridge and
# the toolkit import root — a static library hands over the objects that
# are used and nothing else, so the document frame and the global-menu
# archives stay out of this binary.
set_source_files_properties(qml/LauncherWindow.qml PROPERTIES QT_RESOURCE_ALIAS LauncherWindow.qml)
qt_add_library(qindalauncher_app STATIC)
qt_add_qml_module(qindalauncher_app
    URI QindaOffice.Launcher
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK
    SOURCES
        src/launcher_catalog.cpp src/launcher_catalog.h
        src/launcher_controller.cpp src/launcher_controller.h
    QML_FILES qml/LauncherWindow.qml
)
target_include_directories(qindalauncher_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qindalauncher_app PUBLIC office_quick office_core)
target_compile_definitions(qindalauncher_app PRIVATE QT_NO_CAST_FROM_ASCII)
qqo_set_strict_warnings(qindalauncher_app)

qt_add_executable(qindaoffice main.cpp)
target_link_libraries(qindaoffice PRIVATE qindalauncher_app qindalauncher_appplugin)
target_compile_definitions(qindaoffice PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaOffice"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindaoffice)
qqo_desktop_runtime(qindaoffice)
install(TARGETS qindaoffice RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaOffice.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
