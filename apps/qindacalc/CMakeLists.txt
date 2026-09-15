# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(qindacalc
    main.cpp
    src/calc_window.cpp
    src/chart_dialog.cpp
    src/calc_window.h
    src/spreadsheet_document.cpp
    src/spreadsheet_document.h
    src/spreadsheet_view.cpp
    src/spreadsheet_view.h
)
target_link_libraries(qindacalc PRIVATE office_shell calc_io)
if(QQO_WITH_CHARTS)
    target_link_libraries(qindacalc PRIVATE Qt6::Charts)
    target_compile_definitions(qindacalc PRIVATE QQO_WITH_CHARTS)
endif()
target_compile_definitions(qindacalc PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaCalc"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindacalc)
install(TARGETS qindacalc RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/templates/QindaCalc
    DESTINATION ${CMAKE_INSTALL_DATADIR}/qindaoffice/templates)
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaCalc.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
