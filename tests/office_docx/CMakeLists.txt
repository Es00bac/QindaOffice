# SPDX-License-Identifier: GPL-3.0-or-later
# office_docx gates (M60, D-068): hand-built WordprocessingML packages read
# into the text model (tst_docx_read), our own output re-read and validated
# against the ECMA-376 schemas (tst_docx_write), and the real-document corpus
# gate (tst_docx_corpus: aggregate counts only; skips without QQO_DOCX_CORPUS), and
# applying a document's own paragraph style (tst_docx_restyle), and Word's
# table, paragraph and font formatting (M70: tst_docx_tables,
# tst_docx_paragraphs, which renders a sample through LibreOffice when present),
# and Word's lists and styles (M73: tst_docx_lists, tst_docx_styles), and
# M77 (D-086): mail merge, envelopes and labels, endnotes and floating
# pictures (tst_docx_mailmerge, tst_docx_envelopes, tst_docx_notes,
# tst_docx_floats; each renders a sample through LibreOffice when present), and
# M88 (D-096): Word sections (tst_docx_sections: the section model, linked
# headers, untouched sections kept, LibreOffice's page size per page).
foreach(name IN ITEMS tst_docx_corpus tst_docx_read tst_docx_write tst_docx_restyle tst_docx_comments
                      tst_docx_tables tst_docx_paragraphs tst_docx_controls tst_docx_lists tst_docx_styles
                      tst_docx_mailmerge tst_docx_envelopes tst_docx_notes tst_docx_floats tst_docx_sections)
    add_executable(${name} ${name}.cpp docx_fixture.h docx_roundtrip.h)
    target_link_libraries(${name} PRIVATE office_docx office_ooxml Qt6::Test Qt6::Gui)
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
