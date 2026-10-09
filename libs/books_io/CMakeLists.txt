# SPDX-License-Identifier: GPL-3.0-or-later

# books_io: QindaBooks input/output (M46, D-046) — printed forms (invoice,
# estimate, sales receipt, credit memo, statement; M63: voucher checks) and
# reports as pages for PDF and paper through ONE PagePainter, report CSV,
# bank statement import (CSV, OFX/QFX, QIF), CSV/IIF export, CSV list
# import, QuickBooks IIF import (M63: lists and transactions, one batch, a
# dry run first), reports as xlsx (M63, through calc_io), timestamped
# backups; M68 (D-077): the purchase-order form, per-printer check
# alignment with its test page, QuickBooks purchase orders (PURCHORDR).
# Qt Core/Gui (+ PrintSupport for the device types) over books_engine;
# never links office_quick or the desktop archives (AGENTS.md rules 8/9).
add_library(books_io STATIC
    src/backups.cpp
    src/backups.h
    src/bank_import.h
    src/bank_import_csv.cpp
    src/bank_import_ofx.cpp
    src/bank_import_qif.cpp
    src/bank_import_util.cpp
    src/check_alignment.cpp
    src/check_alignment.h
    src/check_print.cpp
    src/check_print.h
    src/check_renderer.cpp
    src/csv_util.cpp
    src/csv_util.h
    src/form_renderer.cpp
    src/form_renderer.h
    src/iif_import.h
    src/iif_import_documents.cpp
    src/iif_import_lists.cpp
    src/iif_import_orders.cpp
    src/iif_import_p.h
    src/iif_import_transactions.cpp
    src/iif_import_tx_p.h
    src/iif_reader.cpp
    src/iif_reader.h
    src/ledger_export.cpp
    src/ledger_export.h
    src/list_import.cpp
    src/page_output.cpp
    src/page_output.h
    src/paint_util.h
    src/printable_document.cpp
    src/printable_document.h
    src/report_output.cpp
    src/report_output.h
    src/report_xlsx.cpp
    src/report_xlsx.h
)

target_include_directories(books_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

# calc_io (M63): reports as xlsx through the suite's own writer — a library,
# so the dependency still points away from the apps.
target_link_libraries(books_io PUBLIC books_engine Qt6::Core Qt6::Gui Qt6::PrintSupport PRIVATE calc_io)

qqo_set_strict_warnings(books_io)
