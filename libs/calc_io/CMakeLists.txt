# SPDX-License-Identifier: GPL-3.0-or-later

# calc_io: the QXlsx <-> calc_engine bridge (plan section 7.2). Reads and
# writes formulas (shared formulas reconstructed by shifting the master's
# AST), values, merges, column widths, number formats and the full style
# set; xlsx_package normalises packages QXlsx cannot read as they are.
# Split in M33: xlsx_read / xlsx_write / xlsx_style. Qt Core+Gui + KArchive.
add_library(calc_io STATIC
    src/xlsx_bridge.h
    src/xlsx_manifest.h
    src/xlsx_package.cpp
    src/xlsx_package.h
    src/xlsx_parts.cpp
    src/xlsx_parts.h
    src/xlsx_post.cpp
    src/xlsx_post.h
    src/xlsx_read.cpp
    src/xlsx_read_charts.cpp
    src/xlsx_read_parts.cpp
    src/xlsx_read_xml.cpp
    src/xlsx_read_xml.h
    src/xlsx_style.cpp
    src/xlsx_style.h
    src/xlsx_write.cpp
    src/xlsx_write_charts.cpp
    src/xlsx_write_charts.h
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
)

qqo_set_strict_warnings(calc_io)

