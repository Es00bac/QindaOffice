# SPDX-License-Identifier: GPL-3.0-or-later

# diagram_engine: QindaDiagram's document model and everything that works on
# it without a UI (M50, D-050) — the stencil catalogue and its geometry,
# connector routing, the one painter (screen, print, PDF, PNG, SVG,
# thumbnails), edit operations, auto-layout, the .qdiagram file, the
# draw.io and org-chart CSV importers, Visio .vsdx/.vsdm/.vstx open and
# save (M65, D-073, over office_ooxml), and DrawingDocument (undo through
# the Core::Document's stack); M87 (D-095): layers, containers and pools,
# AutoConnect, more layouts, print tiles and external data. Qt Core/Gui (+ Svg for SVG export) over
# office_core and office_ooxml; never links Quick, office_quick or the
# desktop archives (AGENTS.md rules 8/9).
add_library(diagram_engine STATIC
    src/data_link.cpp
    src/data_link.h
    src/drawing.cpp
    src/drawing.h
    src/drawing_document.cpp
    src/drawing_document.h
    src/drawing_io.cpp
    src/drawing_io.h
    src/drawing_io_m87.cpp
    src/drawing_io_m87.h
    src/drawio_import.cpp
    src/export.cpp
    src/export.h
    src/geometry.cpp
    src/geometry.h
    src/importers.h
    src/layers.cpp
    src/layers.h
    src/layout.cpp
    src/layout.h
    src/layout_more.cpp
    src/layout_p.h
    src/ops.cpp
    src/ops.h
    src/ops_connect.cpp
    src/ops_containers.cpp
    src/ops_edit.cpp
    src/ops_lanes.cpp
    src/ops_layers.cpp
    src/ops_pool_p.h
    src/orgchart_csv.cpp
    src/painter.cpp
    src/painter.h
    src/print_tiles.cpp
    src/print_tiles.h
    src/routing.cpp
    src/routing.h
    src/routing_p.h
    src/routing_search.cpp
    src/shape_path.cpp
    src/shape_path.h
    src/stencil_glyphs.cpp
    src/stencil_paths.cpp
    src/stencils.cpp
    src/stencils.h
    src/text_paint.cpp
    src/text_paint.h
    src/themes.cpp
    src/themes.h
    # Microsoft Visio interchange (M65, D-073; docs/visio-interchange.md).
    src/visio_data.cpp
    src/visio_data.h
    src/visio_doc.cpp
    src/visio_doc.h
    src/visio_geometry.cpp
    src/visio_geometry.h
    src/visio_io.h
    src/visio_read.cpp
    src/visio_read_m87.cpp
    src/visio_read_p.h
    src/visio_read_shape.cpp
    src/visio_read_style.cpp
    src/visio_sheet.cpp
    src/visio_sheet.h
    src/visio_source.h
    src/visio_text.cpp
    src/visio_text.h
    src/visio_write.cpp
    src/visio_write_cells.cpp
    src/visio_write_cells.h
    src/visio_write_connector.cpp
    src/visio_write_document.cpp
    src/visio_write_emit.cpp
    src/visio_write_m87.cpp
    src/visio_write_m87.h
    src/visio_write_p.h
    src/visio_write_page.cpp
    src/visio_write_shape.cpp
    src/visio_xml.cpp
    src/visio_xml.h
)

target_include_directories(diagram_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

# M87 (D-095): external data from Excel workbooks reads through calc_io,
# the reader QindaCalc opens them with (data_link.cpp).
target_link_libraries(diagram_engine PUBLIC office_core office_ooxml Qt6::Core Qt6::Gui Qt6::Svg PRIVATE calc_io)
target_compile_definitions(diagram_engine PRIVATE QT_NO_CAST_FROM_ASCII)

qqo_set_strict_warnings(diagram_engine)
