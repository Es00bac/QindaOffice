# SPDX-License-Identifier: GPL-3.0-or-later

# plan_output: QindaPlan's paged and image output (M51, D-051) — the Gantt
# chart fitted to pages over a date range, the reports, and the Timeline
# strip as a PNG — each painted by ONE painter for PDF, paper and the print
# preview (the books_io pattern, D-046) — and the task sheet as xlsx (M63). Qt Core/Gui over plan_engine;
# never links office_quick, Quick or the desktop archives (rules 8/9).
add_library(plan_output STATIC
    src/gantt_print.cpp
    src/gantt_print.h
    src/gantt_print_bars.cpp
    src/gantt_print_p.h
    src/plan_page_output.cpp
    src/plan_page_output.h
    src/plan_xlsx.cpp
    src/plan_xlsx.h
    src/report_print.cpp
    src/report_print.h
    src/timeline_render.cpp
    src/timeline_render.h
)

target_include_directories(plan_output
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

# calc_io (M63): the task sheet as xlsx through the suite's own writer.
target_link_libraries(plan_output PUBLIC plan_engine Qt6::Core Qt6::Gui PRIVATE calc_io)
target_compile_definitions(plan_output PRIVATE QT_NO_CAST_FROM_ASCII)

qqo_set_strict_warnings(plan_output)
