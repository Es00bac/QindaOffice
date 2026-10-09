# SPDX-License-Identifier: GPL-3.0-or-later
# QindaWrite's gates over the QtQuick app: the editing control on its own
# (tst_write_control), the paged item against the PDF exporter
# (tst_write_view), the strips, entries, find/replace and fixtures through
# the real window (tst_write_ui), the typography set (tst_write_typography),
# footnotes / TOC / tracked changes / ruler (tst_write_review), the panel
# export through the desktop's validator (tst_write_menu_export), and the
# format matrix and pagination probe. Every test links the app's static QML
# module, so a source added to apps/qindawrite/CMakeLists.txt is in every test.
find_package(PkgConfig REQUIRED)
pkg_check_modules(POPPLER_QT6 REQUIRED poppler-qt6)

foreach(name IN ITEMS tst_write_control tst_write_view tst_write_ui tst_write_typography
                      tst_write_review tst_write_menu_export tst_write_formats tst_write_probe_pagination
                      tst_write_tools tst_write_docx tst_write_pagination tst_write_story tst_write_picture
                      tst_write_comments tst_write_tables tst_write_formatting tst_write_controls)
    add_executable(${name} ${name}.cpp write_test_support.h)
    target_link_libraries(${name} PRIVATE qindawrite_app qindawrite_appplugin Qt6::Test)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/apps ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE QQO_FIXTURE_DIR="${PROJECT_SOURCE_DIR}/tests/qml_fixtures")
    qqo_set_strict_warnings(${name})
    qqo_desktop_runtime(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
endforeach()
target_link_libraries(tst_write_view PRIVATE ${POPPLER_QT6_LIBRARIES})
target_include_directories(tst_write_view PRIVATE ${POPPLER_QT6_INCLUDE_DIRS})
target_compile_options(tst_write_view PRIVATE ${POPPLER_QT6_CFLAGS_OTHER})
