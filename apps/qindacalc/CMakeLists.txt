# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(qindacalc
    main.cpp
    src/calc_window.cpp
    src/calc_window.h
    src/spreadsheet_document.cpp
    src/spreadsheet_document.h
    src/spreadsheet_view.cpp
    src/spreadsheet_view.h
)
target_link_libraries(qindacalc PRIVATE office_shell calc_io)
target_compile_definitions(qindacalc PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaCalc"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindacalc)
install(TARGETS qindacalc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaCalc.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
