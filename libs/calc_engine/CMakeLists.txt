# SPDX-License-Identifier: GPL-3.0-or-later

# calc_engine: sparse spreadsheet model, hand-written formula lexer/Pratt
# parser, evaluator with dependency-tracked recalculation, number formats
# and RFC 4180 CSV. M69 (D-078): structured references and Excel tables as
# they are edited (structured_ref, table_refs, table_edit,
# structure_tables), array constants, lifting and implicit intersection
# (eval_lift), legacy array formulas (spill.cpp). Qt Core only - never
# links Qt Widgets or the desktop archives (AGENTS.md rules 8/9); every
# behavior here is testable offscreen.
add_library(calc_engine STATIC
    src/cell.cpp
    src/cell.h
    src/csv.cpp
    src/csv.h
    src/dependencies.cpp
    src/dependencies.h
    src/eval.cpp
    src/eval.h
    src/eval_array.cpp
    src/eval_array.h
    src/eval_lift.cpp
    src/eval_lift.h
    src/eval_refs.cpp
    src/eval_refs.h
    src/formula_print.cpp
    src/hyperlink.cpp
    src/hyperlink.h
    src/image_spec.cpp
    src/image_spec.h
    src/functions/array_more.cpp
    src/functions/array_shape.cpp
    src/functions/criteria.cpp
    src/functions/database.cpp
    src/functions/engineering.cpp
    src/functions/stats_m69.cpp
    src/functions/date.cpp
    src/functions/date_more.cpp
    src/functions/dynamic_array.cpp
    src/functions/financial.cpp
    src/functions/financial_more.cpp
    src/functions/functions.cpp
    src/functions/functions.h
    src/functions/logical.cpp
    src/chart_spec.cpp
    src/chart_spec.h
    src/functions/lookup.cpp
    src/functions/lookup_more.cpp
    src/functions/conditional_agg.cpp
    src/functions/math_extra.cpp
    src/functions/math_more.cpp
    src/functions/regression.cpp
    src/functions/statistical.cpp
    src/functions/stats_more.cpp
    src/functions/subtotal.cpp
    src/functions/text.cpp
    src/functions/text_more.cpp
    src/lexer.cpp
    src/lexer.h
    src/number_format.cpp
    src/number_format.h
    src/outline.cpp
    src/outline.h
    src/parser.cpp
    src/parser.h
    src/protection.cpp
    src/protection.h
    src/sheet.cpp
    src/sheet.h
    src/sheet_rules.cpp
    src/sheet_rules.h
    src/sheet_rules_eval.cpp
    src/sheet_source.cpp
    src/sheet_source.h
    src/spill.cpp
    src/structure.cpp
    src/structure.h
    src/structure_features.cpp
    src/structure_internal.h
    src/structure_sheets.cpp
    src/structure_tables.cpp
    src/structured_ref.cpp
    src/structured_ref.h
    src/table_edit.cpp
    src/table_edit.h
    src/table_refs.cpp
    src/table_refs.h
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

