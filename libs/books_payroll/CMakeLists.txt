# SPDX-License-Identifier: GPL-3.0-or-later

# books_payroll (M71, D-081): QindaBooks' payroll and estimated-tax
# arithmetic — federal withholding by IRS Publication 15-T, social security,
# Medicare and FUTA, Colorado withholding by DR 1098, Colorado UI, FAMLI and
# local Occupational Privilege Taxes, the Form 1040-ES / DR 0104EP planner
# for a Schedule C or partnership owner, and the deposit and filing
# calendar. Pure computation: Qt Core and books_money only — no database,
# no Gui (the company file side is books_engine's payroll module).
#
# AGENT-CONTRACT (data): every rate and limit comes from the per-year data
# file resources/tables/payroll_<year>.json (schema version in
# payroll_tables.h), documented in docs/payroll-tax-tables.md. Adding a tax
# year = adding its file (and its row in the .qrc) with every value sourced.
qt_add_resources(BOOKS_PAYROLL_RESOURCES resources/payroll_tables.qrc)

add_library(books_payroll STATIC
    ${BOOKS_PAYROLL_RESOURCES}
    src/colorado_payroll.cpp
    src/colorado_payroll.h
    src/estimated_tax.cpp
    src/estimated_tax.h
    src/federal_withholding.cpp
    src/federal_withholding.h
    src/paycheck_calculator.cpp
    src/paycheck_calculator.h
    src/payroll_calendar.cpp
    src/payroll_calendar.h
    src/payroll_tables.cpp
    src/payroll_tables.h
    src/payroll_tables_reader.cpp
    src/payroll_tables_reader_p.h
    src/payroll_types.cpp
    src/payroll_types.h
)

target_include_directories(books_payroll
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(books_payroll PUBLIC books_money Qt6::Core)

qqo_set_strict_warnings(books_payroll)
