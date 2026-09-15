# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(qindawrite
    main.cpp
    src/page_geometry.cpp
    src/page_geometry.h
    src/page_text_view.cpp
    src/page_text_view.h
    src/spell_checker.cpp
    src/format_io.cpp
    src/pdf_exporter.cpp
    src/pdf_exporter.h
    src/style_catalog.cpp
    src/style_catalog.h
    src/write_document.cpp
    src/write_document.h
    src/write_window.cpp
    src/write_window.h
)
target_link_libraries(qindawrite PRIVATE office_shell office_ai Qt6::WidgetsPrivate KF6::SonnetCore)
target_compile_definitions(qindawrite PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaWrite"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindawrite)
install(TARGETS qindawrite RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaWrite.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
