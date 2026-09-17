# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(tst_qindanote tst_qindanote.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_find_bar.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_page_render.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_blocks.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_page_blocks.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_page_view.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_structure.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_window.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_window_ui.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/notebook.cpp
)
target_link_libraries(tst_qindanote PRIVATE office_shell office_ai office_ink Qt6::Test)
target_include_directories(tst_qindanote PRIVATE ${PROJECT_SOURCE_DIR}/apps)
qqo_set_strict_warnings(tst_qindanote)
add_test(NAME tst_qindanote COMMAND tst_qindanote)
set_tests_properties(tst_qindanote PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
