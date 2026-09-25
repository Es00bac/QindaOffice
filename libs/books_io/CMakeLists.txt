# SPDX-License-Identifier: GPL-3.0-or-later

# books_io: QindaBooks input/output (M46, D-046) — printed forms (invoice,
# estimate, sales receipt, credit memo, statement) and reports as pages for
# PDF and paper through ONE PagePainter, report CSV, bank statement import
# (CSV, OFX/QFX, QIF), CSV/IIF export, CSV list import, timestamped backups.
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
    src/csv_util.cpp
    src/csv_util.h
    src/form_renderer.cpp
    src/form_renderer.h
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
)

target_include_directories(books_io
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/qindaoffice>
)

target_link_libraries(books_io PUBLIC books_engine Qt6::Core Qt6::Gui Qt6::PrintSupport)

qqo_set_strict_warnings(books_io)
