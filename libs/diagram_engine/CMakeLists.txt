# SPDX-License-Identifier: GPL-3.0-or-later

# diagram_engine: QindaDiagram's document model and everything that works on
# it without a UI (M50, D-050) — the stencil catalogue and its geometry,
# connector routing, the one painter (screen, print, PDF, PNG, SVG,
# thumbnails), edit operations, auto-layout, the .qdiagram file, the
# draw.io and org-chart CSV importers, and DrawingDocument (undo through
# the Core::Document's stack). Qt Core/Gui (+ Svg for SVG export) over
# office_core; never links Quick, office_quick or the desktop archives
# (AGENTS.md rules 8/9).
add_library(diagram_engine STATIC
    src/drawing.cpp
    src/drawing.h
    src/drawing_document.cpp
    src/drawing_document.h
    src/drawing_io.cpp
    src/drawing_io.h
    src/drawio_import.cpp
    src/export.cpp
    src/export.h
    src/geometry.cpp
    src/geometry.h
    src/importers.h
    src/layout.cpp
    src/layout.h
    src/ops.cpp
    src/ops.h
    src/ops_edit.cpp
    src/orgchart_csv.cpp
    src/painter.cpp
    src/painter.h
    src/routing.cpp
    src/routing.h
    src/stencil_glyphs.cpp
    src/stencil_paths.cpp
    src/stencils.cpp
    src/stencils.h
    src/text_paint.cpp
    src/text_paint.h
    src/themes.cpp
    src/themes.h
)

target_include_directories(diagram_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(diagram_engine PUBLIC office_core Qt6::Core Qt6::Gui Qt6::Svg)
target_compile_definitions(diagram_engine PRIVATE QT_NO_CAST_FROM_ASCII)

qqo_set_strict_warnings(diagram_engine)
