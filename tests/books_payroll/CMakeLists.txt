# SPDX-License-Identifier: GPL-3.0-or-later

# books_payroll tests (M71, D-081): pure arithmetic over the bundled 2026
# tables — no database, no Gui. Each expected value states its derivation
# in a comment; the publications' own worked examples come first where they
# print one (docs/verification.md "M71 payroll gates").
function(qqo_add_payroll_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE books_payroll Qt6::Test)
    target_compile_definitions(${name} PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_payroll_test(tst_payroll_tables)
qqo_add_payroll_test(tst_payroll_federal)
qqo_add_payroll_test(tst_payroll_colorado)
qqo_add_payroll_test(tst_payroll_paycheck)
qqo_add_payroll_test(tst_payroll_estimates)
qqo_add_payroll_test(tst_payroll_calendar)
qqo_add_payroll_test(tst_payroll_custom)
