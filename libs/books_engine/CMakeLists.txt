# SPDX-License-Identifier: GPL-3.0-or-later

# books_engine: the QindaBooks double-entry ledger (M45, D-045) — company
# file (SQLite), money, chart of accounts, journal, sales, purchases,
# banking, reconciliation, sales tax, reports and financial analysis;
# purchase orders, time tracking and the check print queue (M68, D-077).
# Qt Core + Qt Sql (QSQLITE) only: no Gui, no Quick, no desktop archives
# (AGENTS.md rules 8/9), so every behavior is testable offscreen.
# AGENT-GUARD: never add Gui here — books_document (Core::Document adapter)
# and books_io (PDF/import/export) are the Gui-side targets.
find_package(Qt6 REQUIRED COMPONENTS Sql)

qt_add_resources(BOOKS_ENGINE_RESOURCES resources/books_templates.qrc)

# books_money (M71, D-081): Money and Decimal on their own, Qt Core only, so
# books_payroll (pure tax arithmetic, no database) shares the engine's exact
# money types without linking Qt Sql. The sources stay in this directory;
# books_engine links it PUBLIC, so nothing that includes money.h changes.
add_library(books_money STATIC
    src/decimal.cpp
    src/decimal.h
    src/money.cpp
    src/money.h
)
target_include_directories(books_money
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)
target_link_libraries(books_money PUBLIC Qt6::Core)
qqo_set_strict_warnings(books_money)

add_library(books_engine STATIC
    ${BOOKS_ENGINE_RESOURCES}
    src/account_types.cpp
    src/accounts.cpp
    src/accounts.h
    src/analysis.cpp
    src/analysis.h
    src/bank_feed.cpp
    src/banking.cpp
    src/banking.h
    src/books_types.cpp
    src/books_types.h
    src/chart_templates.cpp
    src/chart_templates.h
    src/check_queue.cpp
    src/check_queue.h
    src/company_file.cpp
    src/company_file.h
    src/company_settings.cpp
    src/company_settings.h
    src/connections.cpp
    src/connections.h
    src/documents.cpp
    src/documents.h
    src/documents_write.cpp
    src/employees.cpp
    src/employees.h
    src/journal.cpp
    src/journal.h
    src/ledger_query.cpp
    src/ledger_query.h
    src/lists.cpp
    src/lists.h
    src/owner_pay.cpp
    src/owner_pay.h
    src/payroll_forms_colorado.cpp
    src/payroll_forms_federal.cpp
    src/payroll_forms_p.h
    src/payroll_forms_w2.cpp
    src/payroll_ledger.cpp
    src/payroll_ledger.h
    src/payroll_liabilities.cpp
    src/payroll_liabilities.h
    src/payroll_records.cpp
    src/payroll_records.h
    src/payroll_reports.cpp
    src/payroll_reports.h
    src/purchase_orders.cpp
    src/purchase_orders.h
    src/purchases.cpp
    src/purchases.h
    src/reconciliation.cpp
    src/reconciliation.h
    src/recurring.cpp
    src/recurring.h
    src/report_table.cpp
    src/report_table.h
    src/reports.h
    src/reports_aging.cpp
    src/reports_cashflow.cpp
    src/reports_financial.cpp
    src/reports_ledger.cpp
    src/reports_orders_time.cpp
    src/reports_sales.cpp
    src/reports_util.cpp
    src/reports_util.h
    src/sales.cpp
    src/sales.h
    src/sales_payments.cpp
    src/schema_migrations.cpp
    src/schema_migrations.h
    src/sql_util.cpp
    src/sql_util.h
    src/tax.cpp
    src/tax.h
    src/time_sheet.cpp
    src/time_tracking.cpp
    src/time_tracking.h
)

target_include_directories(books_engine
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

# M71 (D-081): payroll's records, ledger and forms compute through
# books_payroll (pure tax arithmetic, Qt Core).
target_link_libraries(books_engine PUBLIC books_money books_payroll Qt6::Core Qt6::Sql)

qqo_set_strict_warnings(books_engine)
