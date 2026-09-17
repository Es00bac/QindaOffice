# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(qindashow
    main.cpp
    src/show_document.cpp
    src/show_find_bar.cpp
    src/show_find_bar.h
    src/show_document.h
    src/pdf_export.cpp
    src/pdf_export.h
    src/presentation_controller.cpp
    src/presentation_controller.h
    src/presenter_window.cpp
    src/presenter_window.h
    src/show_ai.cpp
    src/show_ai.h
    src/show_clipboard.cpp
    src/show_clipboard.h
    src/show_arrange.cpp
    src/show_arrange.h
    src/show_arrange_menus.cpp
    src/show_design_actions.cpp
    src/show_design_actions.h
    src/show_master_dialog.cpp
    src/show_master_dialog.h
    src/show_commands.cpp
    src/show_commands.h
    src/show_toolbox.cpp
    src/show_toolbox.h
    src/show_window.cpp
    src/show_window_actions.cpp
    src/show_window_ui.cpp
    src/show_window.h
    src/slide_canvas.cpp
    src/slide_canvas_input.cpp
    src/slide_canvas_order.cpp
    src/slide_canvas_snap.cpp
    src/slide_canvas.h
    src/slide_player.cpp
    src/slide_player.h
    src/text_bridge.cpp
    src/text_bridge.h
)
target_link_libraries(qindashow PRIVATE office_shell office_ai show_model)
target_compile_definitions(qindashow PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaShow"
    QT_NO_CAST_FROM_ASCII
)
target_include_directories(qindashow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
qqo_set_strict_warnings(qindashow)
install(TARGETS qindashow RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/templates/QindaShow
    DESTINATION ${CMAKE_INSTALL_DATADIR}/qindaoffice/templates)
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaShow.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
