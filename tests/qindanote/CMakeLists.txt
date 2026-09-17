# SPDX-License-Identifier: GPL-3.0-or-later
# AGENT-GUARD: the qindanote test targets compile the app sources directly
# (there is no app library). Every new app source file must be added to the
# app target AND here, or the tests silently link against yesterday's code.
set(QINDANOTE_APP_SOURCES
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_find_bar.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_ink_actions.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_page_render.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_blocks.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_page_blocks.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_page_history.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_page_input.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_page_view.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_preferences_page.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_structure.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_tool_strip.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_window.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/note_window_ui.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindanote/src/notebook.cpp
)

add_executable(tst_qindanote tst_qindanote.cpp ${QINDANOTE_APP_SOURCES})
target_link_libraries(tst_qindanote PRIVATE office_shell office_ai office_ink Qt6::Test)
target_include_directories(tst_qindanote PRIVATE ${PROJECT_SOURCE_DIR}/apps)
qqo_set_strict_warnings(tst_qindanote)
add_test(NAME tst_qindanote COMMAND tst_qindanote)
set_tests_properties(tst_qindanote PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_note_blocks tst_note_blocks.cpp ${QINDANOTE_APP_SOURCES})
target_link_libraries(tst_note_blocks PRIVATE office_shell office_ai office_ink Qt6::Test)
target_include_directories(tst_note_blocks PRIVATE ${PROJECT_SOURCE_DIR}/apps)
qqo_set_strict_warnings(tst_note_blocks)
add_test(NAME tst_note_blocks COMMAND tst_note_blocks)
set_tests_properties(tst_note_blocks PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_note_input tst_note_input.cpp ${QINDANOTE_APP_SOURCES})
target_link_libraries(tst_note_input PRIVATE office_shell office_ai office_ink Qt6::Test)
target_include_directories(tst_note_input PRIVATE ${PROJECT_SOURCE_DIR}/apps)
qqo_set_strict_warnings(tst_note_input)
add_test(NAME tst_note_input COMMAND tst_note_input)
set_tests_properties(tst_note_input PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_note_ui tst_note_ui.cpp ${QINDANOTE_APP_SOURCES})
target_link_libraries(tst_note_ui PRIVATE office_shell office_ai office_ink Qt6::Test)
target_include_directories(tst_note_ui PRIVATE ${PROJECT_SOURCE_DIR}/apps)
target_compile_definitions(tst_note_ui PRIVATE
    QINDA_ICONS_ROOT="${PROJECT_SOURCE_DIR}/data/icons")
qqo_set_strict_warnings(tst_note_ui)
add_test(NAME tst_note_ui COMMAND tst_note_ui)
set_tests_properties(tst_note_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
