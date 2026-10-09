# SPDX-License-Identifier: GPL-3.0-or-later

# show_model (plan section 7.5; M62, D-070): the QindaShow slide model
# (shapes of every Office kind, text with its full formatting, tables,
# charts from cached values, groups, placeholders, masters and layouts,
# builds, transitions, notes), the renderer every view paints through, and
# the format mappings: flat ODP (.fodp) and native PowerPoint (.pptx) over
# office_ooxml. M67 (D-076): table editing (table_edit.h), chart data and
# kind editing (chart_edit.h) written into the chart part and its embedded
# workbook through calc_io. M75 (D-084): design, masters, sections,
# comments and media — themes (show_theme.h), the Slide Master view's
# model (master_edit.h), deck-wide re-resolution and slide size
# (pptx_design.h), sections (sections.h), comments (show_comment.h,
# pptx_review.h), audio and video (show_media.h). Qt Core/Gui + KArchive
# + office_ooxml + calc_io; no Quick, no desktop archives.
add_library(show_model STATIC
    src/chart_edit.cpp
    src/chart_edit.h
    src/chart_painter.cpp
    src/chart_workbook.cpp
    src/chart_workbook.h
    src/custom_geometry.cpp
    src/fodp_design.cpp
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
    src/master_edit.cpp
    src/master_edit.h
    src/odf_dom.cpp
    src/odf_dom.h
    src/pptx_context.cpp
    src/pptx_context.h
    src/pptx_design.cpp
    src/pptx_design.h
    src/pptx_origin.h
    src/pptx_read_chart.cpp
    src/pptx_read_common.cpp
    src/pptx_read_frames.cpp
    src/pptx_read_masters.cpp
    src/pptx_read_masters.h
    src/pptx_read_review.cpp
    src/pptx_read_shapes.cpp
    src/pptx_read_shapes.h
    src/pptx_read_table.cpp
    src/pptx_read_text.cpp
    src/pptx_read_text.h
    src/pptx_reader.cpp
    src/pptx_reader.h
    src/pptx_resize.cpp
    src/pptx_resize.h
    src/pptx_review.h
    src/pptx_text_style.cpp
    src/pptx_timing.cpp
    src/pptx_timing.h
    src/pptx_timing_builder.h
    src/pptx_timing_media.cpp
    src/pptx_write_chart.cpp
    src/pptx_write_chart_kind.cpp
    src/pptx_write_context.cpp
    src/pptx_write_context.h
    src/pptx_write_generate.cpp
    src/pptx_write_masters.cpp
    src/pptx_write_masters.h
    src/pptx_write_parts.cpp
    src/pptx_write_parts.h
    src/pptx_write_review.cpp
    src/pptx_write_shapes.cpp
    src/pptx_write_shapes.h
    src/pptx_write_slide.cpp
    src/pptx_write_slide.h
    src/pptx_write_styles.cpp
    src/pptx_write_styles.h
    src/pptx_write_table.cpp
    src/pptx_write_text.cpp
    src/pptx_write_text.h
    src/pptx_writer.cpp
    src/pptx_writer.h
    src/presentation.cpp
    src/presentation.h
    src/preset_geometry.cpp
    src/preset_geometry.h
    src/preset_geometry_lines.cpp
    src/preset_geometry_parts.h
    src/render_parts.h
    src/sections.cpp
    src/sections.h
    src/shape_json.cpp
    src/shape_json.h
    src/shape_json_parts.cpp
    src/shape_json_parts.h
    src/shape_painter.cpp
    src/show_chart.h
    src/show_comment.h
    src/show_media.cpp
    src/show_media.h
    src/show_style.h
    src/show_table.h
    src/show_text.h
    src/show_theme.cpp
    src/show_theme.h
    src/show_theme_xml.cpp
    src/show_theme_xml.h
    src/slide_renderer.cpp
    src/slide_renderer.h
    src/table_edit.cpp
    src/table_edit.h
    src/table_edit_look.cpp
    src/table_painter.cpp
    src/text_painter.cpp
    src/text_painter.h
)

target_include_directories(show_model
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

# calc_io (M67, D-076): an edited chart's embedded workbook is written
# through the suite's own xlsx writer (chart_workbook.h) — a library-to-
# library link like books_io's and plan_output's; still no Quick, no
# desktop archives.
target_link_libraries(show_model PUBLIC Qt6::Core Qt6::Gui KF6::Archive office_ooxml PRIVATE calc_io)

qqo_set_strict_warnings(show_model)

