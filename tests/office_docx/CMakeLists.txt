# SPDX-License-Identifier: GPL-3.0-or-later
# office_docx gates (M60, D-068): hand-built WordprocessingML packages read
# into the text model (tst_docx_read), our own output re-read and validated
# against the ECMA-376 schemas (tst_docx_write), and the real-document corpus
# gate (tst_docx_corpus: aggregate counts only; skips without QQO_DOCX_CORPUS), and
# applying a document's own paragraph style (tst_docx_restyle), and Word's
# table, paragraph and font formatting (M70: tst_docx_tables,
# tst_docx_paragraphs, which renders a sample through LibreOffice when present),
# and Word's lists and styles (M73: tst_docx_lists, tst_docx_styles).
foreach(name IN ITEMS tst_docx_corpus tst_docx_read tst_docx_write tst_docx_restyle tst_docx_comments
                      tst_docx_tables tst_docx_paragraphs tst_docx_controls tst_docx_lists tst_docx_styles)
    add_executable(${name} ${name}.cpp docx_fixture.h)
    target_link_libraries(${name} PRIVATE office_docx office_ooxml Qt6::Test Qt6::Gui)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
