# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(qindashow
    main.cpp
    src/show_document.cpp
    src/show_document.h
    src/show_commands.cpp
    src/show_commands.h
    src/show_window.cpp
    src/show_window.h
    src/slide_canvas.cpp
    src/slide_canvas.h
    src/slide_player.cpp
    src/slide_player.h
    src/text_bridge.cpp
    src/text_bridge.h
)
target_link_libraries(qindashow PRIVATE office_shell show_model)
target_compile_definitions(qindashow PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaShow"
    QT_NO_CAST_FROM_ASCII
)
target_include_directories(qindashow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
qqo_set_strict_warnings(qindashow)
install(TARGETS qindashow RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaShow.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
