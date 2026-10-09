# SPDX-License-Identifier: GPL-3.0-or-later

function(qqo_add_calc_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE calc_engine Qt6::Test)
    target_compile_definitions(${name} PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_calc_test(tst_calc_parser)
qqo_add_calc_test(tst_calc_functions)
qqo_add_calc_test(tst_calc_recalc)
qqo_add_calc_test(tst_calc_numfmt)
qqo_add_calc_test(tst_calc_csv)
qqo_add_calc_test(tst_calc_structure)
qqo_add_calc_test(tst_calc_rules)
# M66 (D-075): dynamic arrays spill.
qqo_add_calc_test(tst_calc_spill)
# M66 (D-075): links, outlines, protection and the source journal.
qqo_add_calc_test(tst_calc_features)
# M69 (D-078): Excel tables and structured references; array constants,
# lifting, implicit intersection, legacy array formulas, relative names.
qqo_add_calc_test(tst_calc_tables)
qqo_add_calc_test(tst_calc_arrays)
# M74 (D-083): PivotTables — layouts, summaries and Show Values As against
# hand-worked grids (pivot_fixture.h), grouping, sort, filters, refresh,
# GETPIVOTDATA, structural edits, Show Details and the styles.
qqo_add_calc_test(tst_calc_pivot)
qqo_add_calc_test(tst_calc_pivot_values)
qqo_add_calc_test(tst_calc_pivot_edit)
# M78 (D-087): the data tools — Text to Columns, Remove Duplicates and
# Consolidate (tst_calc_datatools) and Flash Fill's patterns
# (tst_calc_flashfill).
qqo_add_calc_test(tst_calc_datatools)
qqo_add_calc_test(tst_calc_flashfill)
# M78 (D-087): Goal Seek and the Scenario Manager; data tables.
qqo_add_calc_test(tst_calc_whatif)
qqo_add_calc_test(tst_calc_datatables)
# M78 (D-087): sheet printing — pages cut by Excel's rules (paper, margins,
# scale, Fit to, breaks, titles, headings, page order, print areas), the
# Breaks commands, print settings through structural edits, and the header
# and footer codes.
qqo_add_calc_test(tst_calc_pagelayout)
# M78 (D-087): every conditional-format kind (cellIs with formula operands,
# text, blanks, errors, Top 10, averages, duplicates, dates occurring, colour
# scales, Excel 2007 and 2010 data bars, icon sets) over a rule's whole
# sqref, and every validation kind and operator.
qqo_add_calc_test(tst_calc_condformat)
qqo_add_calc_test(tst_calc_validation)
