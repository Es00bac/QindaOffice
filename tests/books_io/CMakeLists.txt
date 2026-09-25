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
qqo_add_books_io_test(tst_books_io_bank_import)
qqo_add_books_io_test(tst_books_io_export)
