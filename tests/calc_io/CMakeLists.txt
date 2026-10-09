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
# The second reader: LibreOffice re-opens the M66 outputs (converted to flat
# ODS headless, svp, private profile) and keeps links, outlines,
# protection, spills and the carried pivot table. Skips without soffice.
qqo_add_calc_io_test(tst_calc_io_libre)
set_tests_properties(tst_calc_io_libre PROPERTIES TIMEOUT 900)

# Not a test: the M61 measurement tool (see calc_io_survey.cpp) — prints
# aggregate counts for a list of real workbooks, never their content.
add_executable(calc_io_survey calc_io_survey.cpp)
target_link_libraries(calc_io_survey PRIVATE calc_io)
qqo_set_strict_warnings(calc_io_survey)
