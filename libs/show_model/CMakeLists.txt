# SPDX-License-Identifier: GPL-3.0-or-later

# show_model (plan section 7.5; M62, D-070): the QindaShow slide model
# (shapes of every Office kind, text with its full formatting, tables,
# charts from cached values, groups, placeholders, masters and layouts,
# builds, transitions, notes), the renderer every view paints through, and
# the format mappings: flat ODP (.fodp) and native PowerPoint (.pptx) over
# office_ooxml. Qt Core/Gui + KArchive + office_ooxml; no Quick, no
# desktop archives.
add_library(show_model STATIC
    src/chart_painter.cpp
    src/custom_geometry.cpp
    src/fodp_extended.cpp
    src/fodp_extended.h
    src/fodp_read_styles.cpp
    src/fodp_read_styles.h
    src/fodp_read_text.cpp
    src/fodp_read_text.h
    src/fodp_reader.cpp
    src/fodp_reader.h
    src/fodp_styles.cpp
    src/fodp_styles.h
    src/fodp_writer.cpp
    src/fodp_writer.h
    src/odf_dom.cpp
    src/odf_dom.h
    src/pptx_context.cpp
    src/pptx_context.h
    src/pptx_origin.h
    src/pptx_read_chart.cpp
    src/pptx_read_common.cpp
    src/pptx_read_frames.cpp
    src/pptx_read_shapes.cpp
    src/pptx_read_shapes.h
    src/pptx_read_table.cpp
    src/pptx_read_text.cpp
    src/pptx_read_text.h
    src/pptx_reader.cpp
    src/pptx_reader.h
    src/pptx_text_style.cpp
    src/pptx_timing.cpp
    src/pptx_timing.h
    src/pptx_write_context.cpp
    src/pptx_write_context.h
    src/pptx_write_generate.cpp
    src/pptx_write_parts.cpp
    src/pptx_write_parts.h
    src/pptx_write_shapes.cpp
    src/pptx_write_shapes.h
    src/pptx_write_slide.cpp
    src/pptx_write_slide.h
    src/pptx_write_text.cpp
    src/pptx_write_text.h
    src/pptx_writer.cpp
    src/pptx_writer.h
    src/preset_geometry.cpp
    src/preset_geometry.h
    src/preset_geometry_lines.cpp
    src/preset_geometry_parts.h
    src/presentation.cpp
    src/presentation.h
    src/render_parts.h
    src/shape_json.cpp
    src/shape_json.h
    src/shape_json_parts.cpp
    src/shape_json_parts.h
    src/shape_painter.cpp
    src/show_chart.h
    src/show_style.h
    src/show_table.h
    src/show_text.h
    src/slide_renderer.cpp
    src/slide_renderer.h
    src/table_painter.cpp
    src/text_painter.cpp
    src/text_painter.h
)

target_include_directories(show_model
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(show_model PUBLIC Qt6::Core Qt6::Gui KF6::Archive office_ooxml)

qqo_set_strict_warnings(show_model)

