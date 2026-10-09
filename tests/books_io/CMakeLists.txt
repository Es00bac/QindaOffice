# SPDX-License-Identifier: GPL-3.0-or-later

# books_io tests (M46): printed forms, report PDF/CSV, bank statement
# import, CSV/IIF export, list import, backups. Headless (offscreen);
# Poppler reads the PDFs back (page count, text) and rasterises the
# validation-gallery PNGs.
function(qqo_add_books_io_test name)
    add_executable(${name} ${name}.cpp books_io_fixture.h)
    target_link_libraries(${name} PRIVATE books_io Qt6::Test Qt6::PrintSupport PkgConfig::POPPLER_QT6)
    target_compile_definitions(${name} PRIVATE BOOKS_IO_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
    qqo_set_strict_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qqo_add_books_io_test(tst_books_io_forms)
qqo_add_books_io_test(tst_books_io_reports)
# M63: the Excel export is read back with QXlsx (through calc_io).
target_link_libraries(tst_books_io_reports PRIVATE calc_io)
qqo_add_books_io_test(tst_books_io_bank_import)
qqo_add_books_io_test(tst_books_io_export)
# M63 (D-071): QuickBooks IIF import (lists, transactions, preview, mapping,
# idempotence, the export round trip) and QuickBooks Online list CSVs.
qqo_add_books_io_test(tst_books_io_iif)
# M63 (D-071 item 7): voucher check printing (words, builder, refusals,
# one check per page with the values where the stock expects them).
qqo_add_books_io_test(tst_books_io_checks)
# M68 (D-077): the purchase-order form, QuickBooks purchase orders (IIF),
# durations in CSV and Excel (read back with QXlsx through calc_io).
qqo_add_books_io_test(tst_books_io_orders)
target_link_libraries(tst_books_io_orders PRIVATE calc_io)
# M71 (D-081): a paycheck's pay stub on voucher stock, the Form 941
# worksheet through the report painter, the EFW2 W-2 file's records.
qqo_add_books_io_test(tst_books_io_payroll)
