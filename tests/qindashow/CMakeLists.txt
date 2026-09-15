# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(tst_qindashow tst_qindashow.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/pdf_export.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/presentation_controller.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/presenter_window.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_ai.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_clipboard.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_commands.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_document.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_toolbox.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_window.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_window_actions.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/slide_canvas.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/slide_player.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/text_bridge.cpp
)
target_link_libraries(tst_qindashow PRIVATE office_shell office_ai show_model Qt6::Test)
target_include_directories(tst_qindashow PRIVATE ${PROJECT_SOURCE_DIR}/apps/qindashow)
target_compile_definitions(tst_qindashow PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaShow"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(tst_qindashow)
add_test(NAME tst_qindashow COMMAND tst_qindashow)

add_executable(tst_show_ui tst_show_ui.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/pdf_export.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/presentation_controller.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/presenter_window.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_ai.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_clipboard.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_commands.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_document.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_toolbox.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_window.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/show_window_actions.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/slide_canvas.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/slide_player.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindashow/src/text_bridge.cpp
)
target_link_libraries(tst_show_ui PRIVATE office_shell office_ai show_model Qt6::Test)
target_include_directories(tst_show_ui PRIVATE ${PROJECT_SOURCE_DIR}/apps/qindashow)
target_compile_definitions(tst_show_ui PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaShow"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(tst_show_ui)
add_test(NAME tst_show_ui COMMAND tst_show_ui)
set_tests_properties(tst_show_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(tst_qindashow PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
