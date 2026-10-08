// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// BooksHost: one QindaBooks window = one company file (M47, D-047; rule 1).
// The host owns the BooksDocument (books_document.h: never dirty once it
// has a path, Save = no-op, Save As = copy and switch), the window's
// BooksService (`books`), the commands and menus, and the ONE print source
// Print / Print Preview / Export PDF render.
//
// AGENT-CONTRACT (with the lead's QML, qml/BooksWindow.qml and friends):
//  * Commands are ActionEntries with the app-scoped ids below (rule 5 —
//    never rename one). Each handler only emits a request signal the QML
//    answers with a dialog or a view; the host never opens UI itself.
//  * The workspace is a radio group (booksView…Action, Ctrl+1…6) mirrored
//    by `currentView` ("dashboard" | "sales" | "expenses" | "banking" |
//    "accounting" | "reports").
//  * A window that has no company when it first reaches the event loop
//    emits companySetupRequested() once (File ▸ New Company opens such a
//    window; Open into a pristine window loads instead).
//  * Printing: QML names what is printable now with setPrintSource();
//    Print, Print Preview and Export PDF all render that source through
//    one books_io painter wrapped by asPrintCallback (D-046). Without a
//    source the print commands are disabled.
//
// Files: books_host.cpp (document, view, printing), books_host_actions.cpp
// (commands and menus).

#include "books_service.h"
#include "document_host.h"

#include <QPointer>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace QindaOffice::Books {
class BooksDocument;
}

namespace QindaOffice::Apps::Books {

// AGENT-NOTE: not `final` — QML_ELEMENT derives a creation helper from it.
class BooksHost : public Quick::DocumentHost {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the application.")
    Q_PROPERTY(QindaOffice::Apps::Books::BooksService *books READ books CONSTANT)
    // Settable twin of DocumentHost::preferencesPage: the window names its
    // page (BooksPreferencesPage.qml) from QML.
    Q_PROPERTY(QUrl preferencesPageUrl READ preferencesPage WRITE setPreferencesPage NOTIFY preferencesPageChanged)
    Q_PROPERTY(QString currentView READ currentView WRITE setCurrentView NOTIFY currentViewChanged)
    // The document selected in a list (Void / E-mail / Print Document act on it).
    Q_PROPERTY(qint64 selectedDocumentId READ selectedDocumentId WRITE setSelectedDocumentId NOTIFY
                   selectedDocumentIdChanged)
    // "report" | "document" | "statement" | "check" | "" and whether a report prints better wide.
    Q_PROPERTY(QString printSourceKind READ printSourceKind NOTIFY printSourceChanged)
    Q_PROPERTY(bool printLandscape READ printLandscape NOTIFY printSourceChanged)
public:
    BooksHost();
    ~BooksHost() override;

    [[nodiscard]] BooksService *books() const { return m_service; }
    [[nodiscard]] ::QindaOffice::Books::BooksDocument *booksDocument() const { return m_document; }

    [[nodiscard]] QString currentView() const { return m_view; }
    void setCurrentView(const QString &view);
    [[nodiscard]] qint64 selectedDocumentId() const { return m_selectedDocumentId; }
    void setSelectedDocumentId(qint64 id);

    // kind "report": id = report id, options = runReport options.
    // kind "document": id = document id.
    // kind "statement" (M63): id = customer id, options = {from, to}.
    // kind "check" (M63): id = a check or bill payment drawn on a bank
    // account (check_print.h), printed onto voucher check stock; anything
    // else clears the source. A "document" that is such a payment becomes
    // "check" — File ▸ Print on a selected check prints the check.
    // Anything else clears the source.
    Q_INVOKABLE void setPrintSource(const QString &kind, const QVariant &id, const QVariantMap &options = {});
    Q_INVOKABLE void clearPrintSource() { setPrintSource(QString(), {}); }
    [[nodiscard]] QString printSourceKind() const { return m_printKind; }
    [[nodiscard]] bool printLandscape() const { return m_printLandscape; }

    // The workspace keys, in radio-group (and Ctrl+N) order.
    [[nodiscard]] static QStringList viewKeys();

signals:
    void currentViewChanged();
    void selectedDocumentIdChanged();
    void printSourceChanged();
    // Requests the QML answers (kinds: books_format.h document kinds;
    // party kinds "customer" | "vendor"; id 0 = new).
    void documentEditorRequested(const QString &kind, qint64 id);
    void paymentDialogRequested(const QString &kind); // "receive" | "payBills"
    void partyDialogRequested(const QString &kind, qint64 id);
    void itemDialogRequested(qint64 id);
    void accountDialogRequested(qint64 id);
    void journalEntryRequested();
    void transferRequested();
    void depositRequested();
    void importStatementRequested();
    // Banking ▸ Connections… (M48): the QML opens ConnectionsDialog.qml.
    void connectionsRequested();
    void reconcileRequested();
    void bankRulesRequested();
    void reportRequested(const QString &reportId); // "" = let the user pick
    void companySetupRequested();
    void companySettingsRequested();
    void closeBooksRequested();
    // "reportPdf" | "reportCsv" | "reportXlsx" (M63) | "iif" | "journalCsv" | "backup"
    void exportRequested(const QString &what);
    // File ▸ Import and Export ▸ Import / Export Lists (M63): ListsDialog.qml
    // asks which list and which file.
    void importListsRequested();
    void exportListsRequested();
    // File ▸ Import and Export ▸ Import from QuickBooks (IIF) (M63):
    // IifImportDialog.qml previews, maps accounts, then imports.
    void importIifRequested();
    // M63 (D-071): the engine features the app did not reach. Each opens
    // its dialog (LogoDialog, StatementDialog, RecurringDialog,
    // BudgetDialog, SalesTaxPaymentDialog).
    void companyLogoRequested();
    void statementsRequested();
    void recurringRequested();
    void budgetsRequested();
    void salesTaxPaymentRequested();
    void voidRequested(qint64 documentId);

protected:
    [[nodiscard]] QString filterString() const override;
    void documentReplaced() override;
    [[nodiscard]] Quick::PrintRenderCallback printRenderCallback() const override;

private:
    // books_host_actions.cpp
    void createActions();
    void insertMenus();
    void syncEnablement();

    ::QindaOffice::Books::BooksDocument *m_document = nullptr;
    BooksService *m_service = nullptr;
    QString m_view = QStringLiteral("dashboard");
    qint64 m_selectedDocumentId = 0;
    QString m_printKind;
    QString m_printReportId;
    qint64 m_printDocumentId = 0; // the document or check, or the statement's customer
    QVariantMap m_printOptions;
    bool m_printLandscape = false;
};

} // namespace QindaOffice::Apps::Books
