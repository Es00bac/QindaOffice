# SPDX-License-Identifier: GPL-3.0-or-later

# calc_engine: sparse spreadsheet model, hand-written formula lexer/Pratt
# parser, evaluator with dependency-tracked recalculation, number formats
# and RFC 4180 CSV. Qt Core only - never links Qt Widgets or the desktop
# archives (AGENTS.md rules 8/9); every behavior here is testable offscreen.
add_library(calc_engine STATIC
    src/cell.cpp
    src/cell.h
    src/csv.cpp
    src/csv.h
    src/dependencies.cpp
    src/dependencies.h
    src/eval.cpp
    src/eval.h
    src/functions/date.cpp
    src/functions/financial.cpp
    src/functions/functions.cpp
    src/functions/functions.h
    src/functions/logical.cpp
    src/functions/lookup.cpp
    src/functions/text.cpp
    src/lexer.cpp
    src/lexer.h
    src/number_format.cpp
    src/number_format.h
    src/parser.cpp
    src/parser.h
    src/sheet.cpp
    src/sheet.h
    src/value.cpp
    src/value.h
    src/workbook.cpp
    src/workbook.h
)

target_include_directories(calc_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(calc_engine PUBLIC Qt6::Core)

qqo_set_strict_warnings(calc_engine)

install(TARGETS calc_engine ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice/calc_engine
    FILES_MATCHING PATTERN "*.h")
