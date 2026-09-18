# SPDX-License-Identifier: GPL-3.0-or-later

# calc_io: the QXlsx <-> calc_engine bridge (plan section 7.2). Reads and
# writes formulas (shared formulas reconstructed by shifting the master's
# AST), values, merges, column widths and number formats; xlsx_package
# normalises packages QXlsx cannot read as they are. Qt Core+Gui + KArchive.
add_library(calc_io STATIC
    src/xlsx_bridge.cpp
    src/xlsx_bridge.h
    src/xlsx_package.cpp
    src/xlsx_package.h
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

