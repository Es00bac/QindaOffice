# SPDX-License-Identifier: GPL-3.0-or-later

# Every calc_io test links office_ooxml and sees ${PROJECT_SOURCE_DIR}/tests:
# the M61 gate (D-069) validates each package a test writes against the
# ECMA-376 schemas through support/ooxml_schema.h (calc_io_schema.h).
function(qqo_add_calc_io_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE calc_io office_ooxml KF6::Archive Qt6::Test ${ARGN})
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/tests)
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_calc_io_test(tst_calc_io)
qqo_add_calc_io_test(tst_calc_io_styles)
qqo_add_calc_io_test(tst_calc_io_rules)
qqo_add_calc_io_test(tst_calc_io_charts)
qqo_add_calc_io_test(tst_calc_io_tables)
qqo_add_calc_io_test(tst_calc_io_view)
# Images need Qt6::Gui for QImage: the test encodes real PNG/JPEG bytes so
# the byte-for-byte media assertion means something.
qqo_add_calc_io_test(tst_calc_io_images Qt6::Gui)
# M61 (D-069): Excel-shaped packages built by hand in the tests
# (excel_package.h) — typed cells, future-function prefixes, honest strings,
# unparseable and shared formulas — and the package-level interchange
# (theme/properties, scoped names, quoting, tables, long lists, VML, drawings).
qqo_add_calc_io_test(tst_calc_io_excel)
qqo_add_calc_io_test(tst_calc_io_package Qt6::Gui)
# M66 (D-075): hyperlinks, outlines, protection, the parts carried from the
# source package (carry_fixtures.h builds pivot tables, slicers, query
# tables, threaded comments, controls and a VBA project by hand), and
# dynamic arrays with their cell metadata.
qqo_add_calc_io_test(tst_calc_io_links)
qqo_add_calc_io_test(tst_calc_io_outline)
qqo_add_calc_io_test(tst_calc_io_protect)
qqo_add_calc_io_test(tst_calc_io_carry)
qqo_add_calc_io_test(tst_calc_io_carry_sheet)
qqo_add_calc_io_test(tst_calc_io_arrays)
# M69 (D-078): Excel tables, structured references and array formulas as
# Excel stores them; chart sheets carried whole.
qqo_add_calc_io_test(tst_calc_io_structured)
qqo_add_calc_io_test(tst_calc_io_chartsheets)
# M69 (D-078): the real-workbook gate, tst_docx_corpus's twin — open, save
# and re-open every workbook listed in the file QQO_XLSX_CORPUS names,
# aggregate counts only (the owner's files stay on the laptop); skips
# without the variable.
qqo_add_calc_io_test(tst_calc_io_corpus Qt6::Gui)
# The second reader: LibreOffice re-opens the M66 outputs (converted to flat
# ODS headless, svp, private profile) and keeps links, outlines,
# protection, spills and the carried pivot table. Skips without soffice.
qqo_add_calc_io_test(tst_calc_io_libre)
set_tests_properties(tst_calc_io_libre PROPERTIES TIMEOUT 900)
# M78 (D-087): every print setting read, written and read back from an
# Excel-shaped package (schema-valid, printer settings carried and linked),
# and LibreOffice printing a sheet with print titles and Fit to one page
# wide to PDF (skips without soffice / pdftotext).
qqo_add_calc_io_test(tst_calc_io_print)
set_tests_properties(tst_calc_io_print PROPERTIES TIMEOUT 600)

# Not a test: the M61 measurement tool (see calc_io_survey.cpp) — prints
# aggregate counts for a list of real workbooks, never their content.
add_executable(calc_io_survey calc_io_survey.cpp)
target_link_libraries(calc_io_survey PRIVATE calc_io)
qqo_set_strict_warnings(calc_io_survey)
# M74 (D-083): Excel PivotTables into the model and back (pivot_fixtures.h).
qqo_add_calc_io_test(tst_calc_io_pivot)
# M78 (D-087): scenarios and data tables as Excel writes them, read into the
# model, written back and re-opened by LibreOffice (skips without soffice).
qqo_add_calc_io_test(tst_calc_io_whatif)
set_tests_properties(tst_calc_io_whatif PROPERTIES TIMEOUT 600)
# M78 (D-087): every conditional-format and validation kind in Excel's
# markup, and the rules the model cannot hold kept byte for byte with their
# dxfs and priorities (kept_fixtures.h).
qqo_add_calc_io_test(tst_calc_io_kept)
# M82 (D-091): Excel's Custom Views read, written schema-valid in their
# schema slots and read back equal.
qqo_add_calc_io_test(tst_calc_io_views)
# M90 (D-098): Excel's Solver model as its hidden solver_* names — read from
# an Excel-shaped package, written back schema-valid, made here as Excel's set.
qqo_add_calc_io_test(tst_calc_io_solver)
# M90 (D-098): sparklines as Excel writes them (x14:sparklineGroups) read
# into the model, written back schema-valid after the x14 rules, moved by
# structural edits, and made here as Excel's.
qqo_add_calc_io_test(tst_calc_io_sparklines)

qqo_add_calc_io_test(tst_calc_io_pivot_m90)
