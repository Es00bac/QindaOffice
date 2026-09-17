# SPDX-License-Identifier: GPL-3.0-or-later
# The suite's launcher. It links office_core only — for the per-app config
# paths it reads recents from — and never office_shell: it hosts no
# document, so pulling in the document frame (and the desktop's global-menu
# archives with it) would be dependency for its own sake.
add_executable(qindaoffice
    main.cpp
    src/app_card.cpp
    src/app_card.h
    src/launcher_catalog.cpp
    src/launcher_catalog.h
    src/launcher_window.cpp
    src/launcher_window.h
)
target_link_libraries(qindaoffice PRIVATE Qt6::Widgets office_core)
target_compile_definitions(qindaoffice PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaOffice"
    QQO_VERSION="${PROJECT_VERSION}"
    QT_NO_CAST_FROM_ASCII
)
target_include_directories(qindaoffice PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
qqo_set_strict_warnings(qindaoffice)
install(TARGETS qindaoffice RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaOffice.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
