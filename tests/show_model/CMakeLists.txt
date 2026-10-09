# SPDX-License-Identifier: GPL-3.0-or-later
# show_model gates: the model, renderer and shape clipboard codec
# (tst_show_model, which also drives LibreOffice when present), and the
# flat-ODP mapping on its own (tst_fodp: lossless round trip, files the
# pre-M62 writer produced, LibreOffice's styled output, structural
# validity), and native PowerPoint over a package built by hand
# (pptx_fixture*.h): the reader (tst_pptx_read) and the writer
# (tst_pptx_write: equal round trips, schema validation when
# QQO_OOXML_SCHEMA_DIR is set, PowerPoint's id/relationship rules,
# LibreOffice's PDF of what we write). M67 (D-076): editing — tables
# (tst_pptx_tables: the model operations, an edited table written through
# its source, a new table, edited notes) and chart data (tst_pptx_charts:
# caches, formulas and the embedded workbook read back through calc_io,
# kind changes, LibreOffice) and more of PowerPoint's effects
# (tst_show_effects: Wipe/Split entrances, Spin/Grow emphasis, Wipe/Split
# transitions, read, written, drawn). M75 (D-084): Design (tst_pptx_design:
# themes, variants, a theme from a file, slide size, backgrounds), the
# Slide Master view's model (tst_pptx_masters), sections and comments
# (tst_pptx_review), audio and video (tst_pptx_media, with LibreOffice).
# M82 (D-091): New Slide on the deck's layouts, Layout, Reset, Duplicate,
# Reuse Slides and a template's masters (tst_pptx_slides, with LibreOffice).
foreach(name IN ITEMS tst_show_model tst_fodp tst_pptx_read tst_pptx_write tst_pptx_tables tst_pptx_charts tst_show_effects
                      tst_pptx_design tst_pptx_masters tst_pptx_review tst_pptx_media tst_pptx_slides
                      tst_pptx_chart_combo)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE show_model office_ooxml Qt6::Test Qt6::Gui)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    target_compile_definitions(${name} PRIVATE
        QQO_SHOW_TEMPLATE_DIR="${PROJECT_SOURCE_DIR}/data/templates/QindaShow")
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
target_link_libraries(tst_pptx_charts PRIVATE calc_io)
# M82 (D-091): combo charts, multi-level categories and kind changes that
# keep point colours and data labels, with their embedded workbooks.
target_link_libraries(tst_pptx_chart_combo PRIVATE calc_io)
