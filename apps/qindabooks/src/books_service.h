// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// BooksService: the QindaBooks window's view of its company file for QML
// (M47, D-047) — a QVariant API over books_engine / books_io. Exposed as
// `BooksHost.books`; one service per window, never shared.
//
// AGENT-CONTRACT (QML API; the lead's QML calls these exact names — never
// rename one, add new ones instead; docs/books.md "QindaBooks (app)"):
//  * Money crosses as integer minor units + formatted text (books_format.h);
//    inputs are typed text or minor units. The only doubles are `…Chart`.
//  * Dates cross as ISO "yyyy-MM-dd" strings.
//  * Every mutating call returns {ok, id, error[, …]} and never throws or
//    blocks on a dialog; a failure is also reported through notify() only
//    where no caller can see the result (email, background generation).
//  * changed() fires after every committed mutation (CompanyFile::changed,
//    forwarded by BooksDocument), so views re-query; companyChanged() when
//    the window's company file was replaced (load, create, Save As).
//
// AGENT-GUARD (lifetime): the service NEVER caches a CompanyFile pointer —
// BooksDocument replaces it on Save As (books_document.h). Every call asks
// the document for the current one (file()), so the service follows the
// switch without being re-created.
//
// Files: books_service.cpp (company, settings, money, clock),
// books_service_lists.cpp, books_service_tax.cpp (terms, classes, tax),
// books_service_documents.cpp, books_service_payments.cpp (payments,
// recurring, PDF, e-mail), books_service_journal.cpp,
// books_service_banking.cpp (registers, import, feed, rules),
// books_service_reconcile.cpp, books_service_reports.cpp,
// books_service_analysis.cpp (dashboard, housekeeping),
// books_service_connections.cpp (Mercury / Stripe connections, M48).

#include "books_format.h"

#include <QDate>
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <optional>

namespace QindaOffice::Books::Connect {
class ConnectionsController;
}

namespace QindaOffice::Books {
class BooksDocument;
class CompanyFile;
struct BusinessDocument;
struct ReportTable;
} // namespace QindaOffice::Books

namespace QindaOffice::Apps::Books {

class BooksService : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The window's BooksHost owns its BooksService.")
    Q_PROPERTY(bool hasCompany READ hasCompany NOTIFY companyChanged)
    Q_PROPERTY(QString companyName READ companyName NOTIFY changed)
    Q_PROPERTY(QVariantMap settings READ settings NOTIFY changed)
    Q_PROPERTY(QString currency READ currency NOTIFY changed)
    Q_PROPERTY(QString lockDate READ lockDate NOTIFY changed)
    Q_PROPERTY(QString today READ today NOTIFY todayChanged)
    Q_PROPERTY(int dueRecurringCount READ dueRecurringCount NOTIFY changed)
    // Bumped on every change: a cheap binding dependency for views.
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    // A Mercury/Stripe sync is running (M48).
    Q_PROPERTY(bool syncingConnections READ syncingConnections NOTIFY connectionsChanged)
public:
    // `document` is the window's BooksDocument (owned by the host).
    explicit BooksService(::QindaOffice::Books::BooksDocument *document, QObject *parent = nullptr);

    // The open company, or null (no company yet / setup pending).
    [[nodiscard]] ::QindaOffice::Books::CompanyFile *file() const;
    [[nodiscard]] MoneyFormat moneyFormat() const;
    [[nodiscard]] QDate todayDate() const;
    // Tests and grabs pin "today" (QQO_BOOKS_TODAY=yyyy-MM-dd does the same).
    void setToday(const QDate &date);
    // Launches `program args` detached; tests replace it (email).
    using Launcher = std::function<bool(const QString &program, const QStringList &arguments)>;
    void setLauncher(Launcher launcher) { m_launcher = std::move(launcher); }
    // The window's bank/payment connections (tests point it at fake
    // servers with setEndpoints()).
    [[nodiscard]] ::QindaOffice::Books::Connect::ConnectionsController *connectionsController() const
    {
        return m_connections;
    }
    // A report built fresh from the books (the print source uses it).
    [[nodiscard]] std::optional<::QindaOffice::Books::ReportTable> buildReport(const QString &id,
                                                                             const QVariantMap &options,
                                                                             QString *error = nullptr) const;

    // ---- company (books_service.cpp) ----
    [[nodiscard]] bool hasCompany() const { return file() != nullptr; }
    [[nodiscard]] QString companyName() const;
    [[nodiscard]] QVariantMap settings() const;
    Q_INVOKABLE QVariantMap saveSettings(const QVariantMap &map);
    [[nodiscard]] QString currency() const;
    [[nodiscard]] QString lockDate() const;
    [[nodiscard]] QString today() const { return Format::isoDate(todayDate()); }
    [[nodiscard]] int revision() const { return m_revision; }
    Q_INVOKABLE QString formatMoney(qint64 minor) const;
    Q_INVOKABLE QString formatPlain(qint64 minor) const;
    Q_INVOKABLE QVariantMap parseMoney(const QString &text) const;
    Q_INVOKABLE QVariantList templates() const;
    Q_INVOKABLE QVariantList currencies() const;
    Q_INVOKABLE QVariantMap createCompany(const QUrl &fileUrl, const QVariantMap &settings,
                                          const QString &templateId);
    Q_INVOKABLE QVariantMap setLockDate(const QString &isoDate, const QString &reason);

    // ---- lists (books_service_lists.cpp; terms/classes/tax: books_service_tax.cpp) ----
    Q_INVOKABLE QVariantList accounts(bool includeInactive = false) const;
    Q_INVOKABLE QVariantList accountTypes() const;
    Q_INVOKABLE QVariantMap saveAccount(const QVariantMap &map);
    Q_INVOKABLE QVariantMap setAccountActive(qint64 id, bool active);
    Q_INVOKABLE QVariantList paymentAccounts() const;
    Q_INVOKABLE QVariantList expenseAccounts() const;
    Q_INVOKABLE QVariantList incomeAccounts() const;
    Q_INVOKABLE QVariantList parties(const QString &kind, bool includeInactive = false) const;
    Q_INVOKABLE QVariantMap party(qint64 id) const;
    Q_INVOKABLE QVariantMap saveParty(const QVariantMap &map);
    Q_INVOKABLE QVariantMap setPartyActive(qint64 id, bool active);
    Q_INVOKABLE QVariantList items(bool includeInactive = false) const;
    Q_INVOKABLE QVariantMap saveItem(const QVariantMap &map);
    Q_INVOKABLE QVariantList terms() const;
    Q_INVOKABLE QVariantList classes() const;
    Q_INVOKABLE QVariantMap saveClass(const QVariantMap &map);
    Q_INVOKABLE QVariantList taxRates() const;
    Q_INVOKABLE QVariantList taxAgencies() const;
    Q_INVOKABLE QVariantMap saveTaxRate(const QVariantMap &map);

    // ---- documents (books_service_documents.cpp) ----
    Q_INVOKABLE QVariantList documents(const QVariantMap &filter) const;
    Q_INVOKABLE QVariantMap document(qint64 id) const;
    Q_INVOKABLE QVariantMap computeTotals(const QVariantMap &draft) const;
    Q_INVOKABLE QVariantMap saveDocument(const QVariantMap &draft);
    Q_INVOKABLE QVariantMap postDraft(qint64 id);
    Q_INVOKABLE QVariantMap convertEstimate(qint64 id);
    Q_INVOKABLE QVariantMap voidDocument(qint64 id, const QString &reason, bool overrideLock = false);

    // ---- payments, recurring, forms (books_service_payments.cpp) ----
    Q_INVOKABLE QVariantMap receivePayment(const QVariantMap &map);
    Q_INVOKABLE QVariantMap payBills(const QVariantMap &map);
    Q_INVOKABLE QVariantList openInvoices(qint64 customerId) const;
    Q_INVOKABLE QVariantList unpaidBills(qint64 vendorId) const;
    Q_INVOKABLE QVariantMap exportDocumentPdf(qint64 id, const QUrl &fileUrl);
    Q_INVOKABLE QVariantMap emailDocument(qint64 id);
    Q_INVOKABLE QVariantList recurringTemplates() const;
    Q_INVOKABLE QVariantMap saveRecurring(const QVariantMap &map);
    Q_INVOKABLE int generateDueRecurring();
    [[nodiscard]] int dueRecurringCount() const;

    // ---- journal (books_service_journal.cpp) ----
    Q_INVOKABLE QVariantMap postJournalEntry(const QVariantMap &map);
    // The editor's live balance check; posts nothing.
    Q_INVOKABLE QVariantMap checkJournalEntry(const QVariantMap &map) const;
    Q_INVOKABLE QVariantMap transfer(const QVariantMap &map);
    Q_INVOKABLE QVariantMap deposit(const QVariantMap &map);
    Q_INVOKABLE QVariantList undepositedPayments() const;
    Q_INVOKABLE QVariantList auditTrail(const QString &entity, qint64 id) const;

    // ---- banking (books_service_banking.cpp) and reconciliation (books_service_reconcile.cpp) ----
    Q_INVOKABLE QVariantList bankAccounts() const;
    // `register` is a C++ keyword, so the brief's register() is accountRegister().
    Q_INVOKABLE QVariantList accountRegister(qint64 accountId, const QString &fromIso, const QString &toIso) const;
    Q_INVOKABLE QVariantMap importStatement(qint64 accountId, const QUrl &fileUrl,
                                            const QVariantMap &mapping = {});
    Q_INVOKABLE QVariantList feed(qint64 accountId, const QString &status) const;
    Q_INVOKABLE QVariantMap acceptSuggestion(qint64 lineId);
    Q_INVOKABLE QVariantMap categorize(qint64 lineId, const QVariantMap &map);
    Q_INVOKABLE QVariantMap exclude(qint64 lineId);
    Q_INVOKABLE QVariantMap restore(qint64 lineId);
    Q_INVOKABLE QVariantList rules() const;
    Q_INVOKABLE QVariantMap saveRule(const QVariantMap &map);
    Q_INVOKABLE QVariantMap deleteRule(qint64 id);
    Q_INVOKABLE int applyRules(qint64 accountId);
    Q_INVOKABLE QVariantMap startReconciliation(qint64 accountId, const QString &statementDateIso,
                                                const QString &endingBalanceText);
    Q_INVOKABLE QVariantMap reconciliationInProgress(qint64 accountId) const;
    Q_INVOKABLE QVariantList reconcileCandidates(qint64 sessionId) const;
    Q_INVOKABLE QVariantMap markCleared(qint64 sessionId, const QVariantList &lineIds, bool cleared);
    Q_INVOKABLE QVariantMap reconcileState(qint64 sessionId) const;
    Q_INVOKABLE QVariantMap finishReconciliation(qint64 sessionId);
    Q_INVOKABLE QVariantMap cancelReconciliation(qint64 sessionId);
    Q_INVOKABLE QVariantMap undoLastReconciliation(qint64 accountId);

    // ---- reports (books_service_reports.cpp) ----
    Q_INVOKABLE QVariantList reportCatalog() const;
    Q_INVOKABLE QVariantMap runReport(const QString &id, const QVariantMap &options) const;
    Q_INVOKABLE QVariantList drillDown(const QVariantMap &drill) const;
    Q_INVOKABLE QVariantMap exportReport(const QString &id, const QVariantMap &options, const QUrl &fileUrl,
                                         const QString &format);
    Q_INVOKABLE QVariantMap rangePreset(const QString &name) const;

    // ---- analysis and housekeeping (books_service_analysis.cpp) ----
    Q_INVOKABLE QVariantMap kpis(const QString &asOfIso) const;
    Q_INVOKABLE QVariantList monthlyTrends(int months) const;
    Q_INVOKABLE QVariantList expenseBreakdown(const QString &fromIso, const QString &toIso) const;
    Q_INVOKABLE QVariantList ratios(const QString &asOfIso) const;
    Q_INVOKABLE QVariantMap backupNow();
    Q_INVOKABLE QVariantMap backupTo(const QUrl &fileUrl);
    Q_INVOKABLE QVariantMap exportIif(const QUrl &fileUrl);
    Q_INVOKABLE QVariantMap exportJournalCsv(const QUrl &fileUrl);
    Q_INVOKABLE QVariantMap exportListCsv(const QString &kind, const QUrl &fileUrl);
    Q_INVOKABLE QVariantMap importListCsv(const QString &kind, const QUrl &fileUrl);

    // ---- bank and payment connections (books_service_connections.cpp, M48, D-048) ----
    // AGENT-CONTRACT (the lead's ConnectionsDialog.qml): providers are
    // "mercury" | "stripe"; status is disconnected | connected | syncing |
    // error | needsToken. connectMercury/connectStripe/disconnect return
    // their result directly (they wait for the provider and the keyring
    // with user input blocked, ≤ 30 s); sync and payment links answer
    // through the signals below. No call ever returns a token.
    Q_INVOKABLE QVariantList connections() const;
    Q_INVOKABLE QVariantMap connectMercury(const QString &token);
    Q_INVOKABLE QVariantMap linkMercuryAccount(const QString &mercuryAccountId, qint64 booksAccountId);
    Q_INVOKABLE QVariantList mercuryLinks() const;
    Q_INVOKABLE QVariantMap connectStripe(const QString &key);
    Q_INVOKABLE QVariantMap stripeSettings() const;
    Q_INVOKABLE QVariantMap saveStripeSettings(const QVariantMap &map);
    Q_INVOKABLE QString syncConnections();
    // Removes the token and the link settings; imported history stays.
    Q_INVOKABLE QVariantMap disconnect(const QString &provider);
    Q_INVOKABLE QString createPaymentLink(qint64 invoiceId);
    [[nodiscard]] bool syncingConnections() const;

signals:
    void changed();
    void companyChanged();
    void todayChanged();
    // variant: "info" | "success" | "warning" | "danger"
    void notify(const QString &title, const QString &text, const QString &variant);
    // M48: connection state changed (status, secret, settings, syncing).
    void connectionsChanged();
    // result: {ok, imported, applied, matched, posted, errors[]}
    void connectionSyncFinished(const QString &requestId, const QVariantMap &result);
    // result: {ok, url, error, reused}
    void paymentLinkFinished(const QString &requestId, const QVariantMap &result);

private:
    // Shared helpers for the split implementation files.
    [[nodiscard]] QVariantMap noCompany() const;
    [[nodiscard]] QString nameOfAccount(qint64 id) const;
    [[nodiscard]] QString nameOfParty(qint64 id) const;
    // Draft map → engine document (books_service_documents.cpp). False
    // with `error` when a field does not parse.
    bool documentFromMap(const QVariantMap &map, ::QindaOffice::Books::BusinessDocument *doc,
                         QString *error) const;
    [[nodiscard]] QVariantMap documentRow(const ::QindaOffice::Books::BusinessDocument &doc) const;
    [[nodiscard]] static QString localPath(const QUrl &url);
    bool writeFile(const QString &path, const QByteArray &bytes, QString *error) const;

    // Waits (user input blocked) for an async call's callback, ≤ timeoutMs.
    [[nodiscard]] static std::optional<QVariantMap>
    waitFor(const std::function<void(std::function<void(const QVariantMap &)>)> &start, int timeoutMs = 30000);
    // The body text of an invoice e-mail's "Pay online" line ("" without one).
    [[nodiscard]] QString payOnlineLine(qint64 documentId) const;

    ::QindaOffice::Books::BooksDocument *m_document;
    ::QindaOffice::Books::Connect::ConnectionsController *m_connections = nullptr;
    Launcher m_launcher;
    QDate m_today;
    int m_revision = 0;
};

} // namespace QindaOffice::Apps::Books
