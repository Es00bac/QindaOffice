# SPDX-License-Identifier: GPL-3.0-or-later
# QindaBooks (M47, D-047): bookkeeping for one company per window over
# books_engine / books_document / books_io. The C++ side is the host
# (commands, menus, print source) and BooksService (the QML API over the
# engine); the QML module is the window and its views.
#
# AGENT-NOTE (runtime dependency): the company file is SQLite through Qt
# Sql's QSQLITE driver plugin. It is loaded at run time, so a Qt built
# without it links fine and fails on the first open — packaging must
# require qtbase[sql,sqlite] (docs/books.md "QindaBooks (app)").
find_package(Qt6 REQUIRED COMPONENTS Sql)

set(qindabooks_qml
    qml/BooksWindow.qml
    qml/BooksRibbonLayout.qml
    qml/CompanySetupDialog.qml
    qml/DashboardView.qml
    qml/KpiCard.qml
    qml/SalesView.qml
    qml/ExpensesView.qml
    qml/DocumentTable.qml
    qml/DocumentEditor.qml
    qml/LineItemsTable.qml
    qml/PaymentDialog.qml
    qml/PartyDialog.qml
    qml/ItemDialog.qml
    qml/BankingView.qml
    qml/ReconcileView.qml
    qml/AccountsView.qml
    qml/AccountDialog.qml
    qml/JournalEntryDialog.qml
    qml/ReportsView.qml
    qml/ReportTableView.qml
    qml/BooksPreferencesPage.qml
    qml/ConnectionsDialog.qml
    qml/ListsDialog.qml
    qml/LogoDialog.qml
    qml/StatementDialog.qml
    qml/RecurringDialog.qml
    qml/RecurringEditor.qml
    qml/BudgetDialog.qml
    qml/SalesTaxPaymentDialog.qml
    qml/IifImportDialog.qml
)
foreach(qml_file IN LISTS qindabooks_qml)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()

# AGENT-CONTRACT: the app's QML module is a STATIC library so the tests can
# instantiate BooksWindow.qml and the host; the executable is main.cpp.
qt_add_library(qindabooks_app STATIC)
qt_add_qml_module(qindabooks_app
    URI QindaOffice.Books.App
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    DEPENDENCIES QtQuick QindaTK QindaOffice.Shell
    SOURCES
        src/books_format.cpp src/books_format.h
        src/books_host.cpp src/books_host.h
        src/books_host_actions.cpp
        src/books_service.cpp src/books_service.h
        src/books_service_analysis.cpp
        src/books_service_banking.cpp
        src/books_service_connections.cpp
        src/books_service_documents.cpp
        src/books_service_journal.cpp
        src/books_service_lists.cpp
        src/books_service_migration.cpp
        src/books_service_payments.cpp
        src/books_service_reconcile.cpp
        src/books_service_recurring.cpp
        src/books_service_reports.cpp
        src/books_service_budgets.cpp
        src/books_service_statements.cpp
        src/books_service_tax.cpp
    QML_FILES ${qindabooks_qml}
)
target_include_directories(qindabooks_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qindabooks_app PUBLIC
    office_quick office_quickplugin books_document books_io books_connect Qt6::Sql Qt6::PrintSupport)
target_compile_definitions(qindabooks_app PRIVATE QT_NO_CAST_FROM_ASCII)
qqo_set_strict_warnings(qindabooks_app)

qt_add_executable(qindabooks main.cpp)
target_link_libraries(qindabooks PRIVATE qindabooks_app qindabooks_appplugin)
target_compile_definitions(qindabooks PRIVATE
    QQO_DESKTOP_ID="org.qindaqt.QindaBooks"
    QT_NO_CAST_FROM_ASCII
)
qqo_set_strict_warnings(qindabooks)
qqo_desktop_runtime(qindabooks)
install(TARGETS qindabooks RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${PROJECT_SOURCE_DIR}/data/org.qindaqt.QindaBooks.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
