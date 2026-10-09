# SPDX-License-Identifier: GPL-3.0-or-later

# books_engine tests (M45): headless, each in its own temp dir. The engine
# is Qt Core + Sql only, so these tests link no Gui (except
# tst_books_document, which tests the Core::Document adapter).
function(qqo_add_books_test name)
    add_executable(${name} ${name}.cpp books_fixture.h)
    target_link_libraries(${name} PRIVATE books_engine Qt6::Test ${ARGN})
    target_compile_definitions(${name} PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_books_test(tst_books_money)
qqo_add_books_test(tst_books_company)
qqo_add_books_test(tst_books_ledger)
qqo_add_books_test(tst_books_sales)
qqo_add_books_test(tst_books_purchases)
qqo_add_books_test(tst_books_banking)
qqo_add_books_test(tst_books_reports)
qqo_add_books_test(tst_books_analysis)
qqo_add_books_test(tst_books_perf)
qqo_add_books_test(tst_books_document books_document)
# M68 (D-077): purchase orders, time tracking, the check print queue.
qqo_add_books_test(tst_books_orders)
qqo_add_books_test(tst_books_time)
qqo_add_books_test(tst_books_checks)
# M71 (D-081): paychecks, liabilities, owner pay, payroll reports and forms.
qqo_add_books_test(tst_books_payroll)
qqo_add_books_test(tst_books_owner_pay)
qqo_add_books_test(tst_books_payroll_forms)
qqo_add_books_test(tst_books_payroll_items)
