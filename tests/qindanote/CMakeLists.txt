# SPDX-License-Identifier: GPL-3.0-or-later
# QindaNote's gates over the QtQuick app: the notebook engine and the host
# (tst_qindanote), blocks (tst_note_blocks), pen/eraser input through the
# platform tablet path (tst_note_input), the window's chrome (tst_note_ui)
# and the panel export through the desktop's own validator
# (tst_note_menu_export). Every test links the app's static QML module, so
# a source added to apps/qindanote/CMakeLists.txt is in every test.
foreach(name IN ITEMS tst_qindanote tst_note_blocks tst_note_input tst_note_ui tst_note_menu_export)
    add_executable(${name} ${name}.cpp note_test_support.h)
    target_link_libraries(${name} PRIVATE qindanote_app qindanote_appplugin
        Qt6::Test Qt6::GuiPrivate)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures")
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()

# Live notes (D-057): the session against a local SSE model server.
add_executable(tst_note_live tst_note_live.cpp note_test_support.h
    ${PROJECT_SOURCE_DIR}/tests/office_ai/mock_sse_server.cpp
    ${PROJECT_SOURCE_DIR}/tests/office_ai/mock_sse_server.h)
target_link_libraries(tst_note_live PRIVATE qindanote_app qindanote_appplugin Qt6::Test Qt6::GuiPrivate)
target_include_directories(tst_note_live PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
target_compile_definitions(tst_note_live PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures")
qqo_set_strict_warnings(tst_note_live)
qqo_desktop_runtime(tst_note_live)
add_test(NAME tst_note_live COMMAND tst_note_live)
set_tests_properties(tst_note_live PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QQO_NO_SECRET_SERVICE=1")
