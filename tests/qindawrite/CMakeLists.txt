# SPDX-License-Identifier: GPL-3.0-or-later

function(qqo_add_write_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE office_shell Qt6::Test Qt6::WidgetsPrivate KF6::SonnetCore)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_write_test(tst_write_probe_pagination)
qqo_add_write_test(tst_write_probe_control)
qqo_add_write_test(tst_write_formats)
target_sources(tst_write_formats PRIVATE
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/style_catalog.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/format_io.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/spell_checker.cpp)

find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_QT6 REQUIRED poppler-qt6)
add_executable(tst_write_view tst_write_view.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/find_replace_bar.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/format_controller.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_actions.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/page_geometry.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/page_setup_io.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/table_actions.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/page_text_accessibility.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/page_text_view.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/spell_checker.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/format_io.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/pdf_exporter.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/style_catalog.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_document.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_window.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_window_ui.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_typography.cpp
)
target_link_libraries(tst_write_view PRIVATE office_shell office_ai Qt6::Test Qt6::WidgetsPrivate
    ${POPPLER_QT6_LIBRARIES} KF6::SonnetCore)
target_include_directories(tst_write_view PRIVATE ${POPPLER_QT6_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/apps)
target_compile_options(tst_write_view PRIVATE ${POPPLER_QT6_CFLAGS_OTHER})
qqo_set_strict_warnings(tst_write_view)
add_test(NAME tst_write_view COMMAND tst_write_view)
set_tests_properties(tst_write_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_write_typography tst_write_typography.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/find_replace_bar.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/format_controller.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_actions.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/page_geometry.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/page_setup_io.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/table_actions.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/page_text_accessibility.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/page_text_view.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/spell_checker.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/format_io.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/pdf_exporter.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/style_catalog.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_document.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_window.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_window_ui.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_typography.cpp
)
target_link_libraries(tst_write_typography PRIVATE office_shell office_ai Qt6::Test
    Qt6::WidgetsPrivate KF6::SonnetCore)
target_include_directories(tst_write_typography PRIVATE ${PROJECT_SOURCE_DIR}/apps)
qqo_set_strict_warnings(tst_write_typography)
add_test(NAME tst_write_typography COMMAND tst_write_typography)
set_tests_properties(tst_write_typography PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(tst_write_ui tst_write_ui.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/find_replace_bar.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/format_controller.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_actions.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/page_geometry.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/page_setup_io.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/table_actions.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/page_text_accessibility.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/page_text_view.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/spell_checker.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/format_io.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/pdf_exporter.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/style_catalog.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_document.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_window.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_window_ui.cpp
    ${PROJECT_SOURCE_DIR}/apps/qindawrite/src/write_typography.cpp
)
target_link_libraries(tst_write_ui PRIVATE office_shell office_ai Qt6::Test Qt6::WidgetsPrivate
    KF6::SonnetCore)
target_include_directories(tst_write_ui PRIVATE ${PROJECT_SOURCE_DIR}/apps)
qqo_set_strict_warnings(tst_write_ui)
add_test(NAME tst_write_ui COMMAND tst_write_ui)
set_tests_properties(tst_write_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
