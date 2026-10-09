# SPDX-License-Identifier: GPL-3.0-or-later

# office_docx (M60, D-068): Word documents natively — WordprocessingML ⇄
# QTextDocument plus WordParts (page layout, rich headers/footers, footnotes,
# pictures, the source package). Formatting is mapped where the text model
# has it and preserved verbatim where it does not. Qt Core/Gui + office_ooxml;
# no Quick, no desktop archives, so its tests run anywhere.
add_library(office_docx STATIC
    src/docx_format.cpp
    src/docx_format.h
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
    src/docx_metrics.cpp
    src/docx_comments.cpp
    src/docx_text.h
    src/docx_writer.cpp
    src/docx_writer.h
    src/docx_writer_body.cpp
    src/docx_writer_picture.cpp
    src/docx_writer_format.cpp
    src/docx_writer_numbering.cpp
    src/docx_writer_p.h
    src/docx_writer_styles.cpp
    src/docx_writer_table.cpp
)

target_include_directories(office_docx
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)

target_link_libraries(office_docx PUBLIC office_ooxml Qt6::Core Qt6::Gui)

qqo_set_strict_warnings(office_docx)
