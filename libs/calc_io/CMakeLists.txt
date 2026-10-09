# SPDX-License-Identifier: GPL-3.0-or-later

# calc_io: the QXlsx <-> calc_engine bridge (plan section 7.2). Reads and
# writes formulas (shared formulas reconstructed by shifting the master's
# AST), values, merges, column widths, number formats and the full style
# set; xlsx_package normalises packages QXlsx cannot read as they are.
# Split in M33: xlsx_read / xlsx_write / xlsx_style. M61 (D-069): typed
# cells straight from the package, Excel's function prefixes, the honest
# writer and its workbook-level post-pass; office_ooxml (D-068) supplies the
# document-properties writer and the XML tree. M66 (D-075): hyperlinks,
# outlines, protection and dynamic arrays (xlsx_read_features,
# xlsx_write_features, xlsx_post_sheet), and the carry stage that keeps the
# parts the engine does not model (xlsx_carry*, through office_ooxml's
# RelationshipCopier). M69 (D-078): structured references and '@' in
# Excel's spelling (xlsx_formula_tokens), table parts with calculated
# columns and totals rows, legacy array formulas, chart sheets carried
# whole (xlsx_carry_chartsheet). Qt Core+Gui + KArchive.
add_library(calc_io STATIC
    src/xlsx_bridge.h
    src/xlsx_carry.h
    src/xlsx_carry_book.cpp
    src/xlsx_carry_chartsheet.cpp
    src/xlsx_carry_context.cpp
    src/xlsx_carry_context.h
    src/xlsx_carry_drawing.cpp
    src/xlsx_carry_phases.h
    src/xlsx_carry_read.cpp
    src/xlsx_carry_sheet.cpp
    src/xlsx_carry_types.h
    src/xlsx_carry_write.cpp
    src/xlsx_carry_xml.cpp
    src/xlsx_carry_xml.h
    src/xlsx_formula.cpp
    src/xlsx_formula.h
    src/xlsx_formula_tokens.cpp
    src/xlsx_formula_tokens.h
    src/xlsx_manifest.h
    src/xlsx_package.cpp
    src/xlsx_package.h
    src/xlsx_parts.cpp
    src/xlsx_parts.h
    src/xlsx_post.cpp
    src/xlsx_post_book.cpp
    src/xlsx_post_book.h
    src/xlsx_post_cells.cpp
    src/xlsx_post_cells.h
    src/xlsx_post_images.cpp
    src/xlsx_post_images.h
    src/xlsx_post_sheet.cpp
    src/xlsx_post_sheet.h
    src/xlsx_post.h
    src/xlsx_read.cpp
    src/xlsx_read_book.cpp
    src/xlsx_read_cells.cpp
    src/xlsx_read_cells.h
    src/xlsx_read_drawings.cpp
    src/xlsx_read_features.cpp
    src/xlsx_read_features.h
    src/xlsx_read_parts.cpp
    src/xlsx_read_xml.cpp
    src/xlsx_read_xml.h
    src/xlsx_style.cpp
    src/xlsx_style.h
    src/xlsx_text.cpp
    src/xlsx_text.h
    src/xlsx_write.cpp
    src/xlsx_write_book.cpp
    src/xlsx_write_book.h
    src/xlsx_write_charts.cpp
    src/xlsx_write_charts.h
    src/xlsx_write_features.cpp
    src/xlsx_write_features.h
    src/xlsx_write_rules.cpp
    src/xlsx_write_rules.h
    src/xlsx_write_xml.cpp
    src/xlsx_write_xml.h
    src/xlsx_xml.cpp
    src/xlsx_xml.h
)

target_include_directories(calc_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(calc_io
    PUBLIC
        calc_engine
        QXlsx::QXlsx
        KF6::Archive
    PRIVATE
        office_ooxml
)

qqo_set_strict_warnings(calc_io)

