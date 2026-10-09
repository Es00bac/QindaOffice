# SPDX-License-Identifier: GPL-3.0-or-later

# office_docx (M60, D-068): Word documents natively — WordprocessingML ⇄
# QTextDocument plus WordParts (page layout, rich headers/footers, footnotes,
# pictures, the source package). Formatting is mapped where the text model
# has it and preserved verbatim where it does not. M77 (D-086): mail merge
# (settings, data, fields, the merge), envelopes and labels, endnotes,
# floating pictures. Qt Core/Gui + office_ooxml; no Quick, no desktop
# archives, so its tests run anywhere.
add_library(office_docx STATIC
    src/docx_format.cpp
    src/docx_format.h
    src/docx_list_edit.cpp
    src/docx_list_edit.h
    src/docx_list_labels.cpp
    src/docx_list_labels.h
    src/docx_list_library.cpp
    src/docx_lists.cpp
    src/docx_lists.h
    src/docx_model.h
    src/docx_numbering.cpp
    src/docx_numbering.h
    src/docx_properties.h
    src/docx_reader.cpp
    src/docx_reader.h
    src/docx_reader_body.cpp
    src/docx_reader_runs.cpp
    src/docx_reader_objects.cpp
    src/docx_reader_p.h
    src/docx_reader_table.cpp
    src/docx_styles.cpp
    src/docx_styles.h
    src/docx_text.cpp
    src/docx_restyle.cpp
    src/docx_restyle_edit.cpp
    src/docx_style_edit.cpp
    src/docx_style_edit.h
    src/docx_metrics.cpp
    src/docx_comments.cpp
    src/docx_controls.cpp
    src/docx_text.h
    src/docx_writer.cpp
    src/docx_writer.h
    src/docx_writer_body.cpp
    src/docx_writer_picture.cpp
    src/docx_writer_controls.cpp
    src/docx_writer_format.cpp
    src/docx_writer_numbering.cpp
    src/docx_writer_p.h
    src/docx_writer_styles.cpp
    src/docx_writer_table.cpp
    src/docx_writer_table_props.cpp
    src/docx_writer_paragraph.cpp
    src/docx_borders.cpp
    src/docx_borders.h
    src/docx_format_snapshot.cpp
    src/docx_paragraph_format.cpp
    src/docx_paragraph_format.h
    src/docx_table_format.cpp
    src/docx_table_format.h
    src/docx_table_props.cpp
    src/docx_envelopes.cpp
    src/docx_envelopes.h
    src/docx_float.cpp
    src/docx_float.h
    src/docx_float_display.cpp
    src/docx_mailmerge.cpp
    src/docx_mailmerge.h
    src/docx_merge.cpp
    src/docx_merge.h
    src/docx_merge_address.cpp
    src/docx_merge_data.cpp
    src/docx_merge_data.h
    src/docx_merge_fields.cpp
    src/docx_merge_fields.h
    src/docx_merge_fields_p.h
    src/docx_merge_tokens.cpp
    src/docx_notes.cpp
    src/docx_parts.cpp
    src/docx_notes.h
    src/docx_writer_notes.cpp
    src/docx_writer_settings.cpp
)

target_include_directories(office_docx
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(office_docx PUBLIC office_ooxml Qt6::Core Qt6::Gui)

qqo_set_strict_warnings(office_docx)
