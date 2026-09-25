// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: the report catalog, running a report with its options,
// drill-down to the transactions behind a number, export to PDF/CSV and
// the fiscal-year-aware date-range presets.
#include "books_service.h"

#include "accounts.h"
#include "company_file.h"
#include "documents.h"
#include "reconciliation.h"
#include "report_output.h"
#include "reports.h"

#include <algorithm>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

// AGENT-CONTRACT (catalog `options`, with the lead's ReportsView): the
// words are from, to, asOf, basis, comparison, columns — the controls the
// report's option bar shows. The reconciliation report takes sessionId /
// accountId instead (it is opened from the Banking workspace).
QStringList optionsFor(const QString &id)
{
    static const QHash<QString, QStringList> table{
        {QStringLiteral("profit_and_loss"),
         {QStringLiteral("from"), QStringLiteral("to"), QStringLiteral("basis"), QStringLiteral("comparison"),
          QStringLiteral("columns")}},
        {QStringLiteral("balance_sheet"), {QStringLiteral("asOf"), QStringLiteral("basis"), QStringLiteral("comparison")}},
        {QStringLiteral("trial_balance"), {QStringLiteral("asOf"), QStringLiteral("basis")}},
        {QStringLiteral("sales_tax_liability"), {QStringLiteral("from"), QStringLiteral("to")}},
        {QStringLiteral("general_ledger"), {QStringLiteral("from"), QStringLiteral("to")}},
        {QStringLiteral("journal"), {QStringLiteral("from"), QStringLiteral("to")}},
        {QStringLiteral("transaction_list"), {QStringLiteral("from"), QStringLiteral("to")}},
        {QStringLiteral("reconciliation"), {}},
    };
    if (table.contains(id)) {
        return table.value(id);
    }
    if (id.startsWith(QLatin1String("ar_")) || id.startsWith(QLatin1String("ap_")) || id.endsWith(QLatin1String("_balances"))
        || id == QLatin1String("open_invoices") || id == QLatin1String("unpaid_bills")) {
        return {QStringLiteral("asOf")};
    }
    // Cash flow, budget vs actual, the sales and expense breakdowns.
    return {QStringLiteral("from"), QStringLiteral("to"), QStringLiteral("basis")};
}

Comparison comparisonFrom(const QString &key)
{
    if (key == QLatin1String("priorPeriod")) {
        return Comparison::PriorPeriod;
    }
    return key == QLatin1String("priorYear") ? Comparison::PriorYear : Comparison::None;
}

} // namespace

QVariantList BooksService::reportCatalog() const
{
    QVariantList out;
    for (const Reports::Catalog &r : Reports::catalog()) {
        out << QVariantMap{{QStringLiteral("id"), r.id},
                           {QStringLiteral("title"), r.title},
                           {QStringLiteral("group"), r.group},
                           {QStringLiteral("definition"), r.definition},
                           {QStringLiteral("options"), optionsFor(r.id)}};
    }
    return out;
}

std::optional<ReportTable> BooksService::buildReport(const QString &id, const QVariantMap &options,
                                                     QString *error) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        if (error != nullptr) {
            *error = tr("No company is open in this window.");
        }
        return std::nullopt;
    }
    const QDate today = todayDate();
    QDate from = Format::date(options.value(QStringLiteral("from")));
    QDate to = Format::date(options.value(QStringLiteral("to")));
    QDate asOf = Format::date(options.value(QStringLiteral("asOf")));
    if (!from.isValid()) {
        from = c->settings().fiscalYearStart(today);
    }
    if (!to.isValid()) {
        to = today;
    }
    if (!asOf.isValid()) {
        asOf = options.contains(QStringLiteral("to")) ? to : today;
    }
    if (from > to) {
        if (error != nullptr) {
            *error = tr("The period starts after it ends.");
        }
        return std::nullopt;
    }
    std::optional<Basis> basis;
    const QString basisKey = options.value(QStringLiteral("basis")).toString();
    if (basisKey == QLatin1String("cash")) {
        basis = Basis::Cash;
    } else if (basisKey == QLatin1String("accrual")) {
        basis = Basis::Accrual;
    }
    const Comparison comparison = comparisonFrom(options.value(QStringLiteral("comparison")).toString());
    Reports reports(*c);
    if (id == QLatin1String("profit_and_loss")) {
        const QString columns = options.value(QStringLiteral("columns")).toString();
        return reports.profitAndLoss({from, to, basis, comparison,
                                      columns == QLatin1String("months")    ? PnlColumns::Months
                                      : columns == QLatin1String("classes") ? PnlColumns::Classes
                                                                            : PnlColumns::Total});
    }
    if (id == QLatin1String("balance_sheet")) {
        return reports.balanceSheet({asOf, basis, comparison});
    }
    if (id == QLatin1String("cash_flow")) {
        return reports.cashFlow(from, to, basis);
    }
    if (id == QLatin1String("trial_balance")) {
        return reports.trialBalance(asOf, basis);
    }
    if (id == QLatin1String("general_ledger")) {
        return reports.generalLedger(from, to, Format::id(options.value(QStringLiteral("accountId"))));
    }
    if (id == QLatin1String("journal")) {
        return reports.journal(from, to);
    }
    if (id == QLatin1String("transaction_list")) {
        return reports.transactionList(from, to);
    }
    if (id == QLatin1String("reconciliation")) {
        qint64 session = Format::id(options.value(QStringLiteral("sessionId")));
        if (session == 0) {
            // The latest reconciliation of the named account (or of any).
            const qint64 account = Format::id(options.value(QStringLiteral("accountId")));
            ChartOfAccounts chart(*c);
            for (const Account &a : chart.accounts(true)) {
                if ((account != 0 && a.id != account) || (a.type != AccountType::Bank && a.type != AccountType::CreditCard)) {
                    continue;
                }
                for (const ReconciliationSession &s : Reconciliation(*c).history(a.id)) {
                    session = std::max(session, s.id);
                }
            }
        }
        if (session == 0) {
            if (error != nullptr) {
                *error = tr("No account has been reconciled yet.");
            }
            return std::nullopt;
        }
        return reports.reconciliation(session);
    }
    using Ranged = ReportTable (Reports::*)(const QDate &, const QDate &, std::optional<Basis>) const;
    static const QHash<QString, Ranged> ranged{
        {QStringLiteral("sales_by_customer"), &Reports::salesByCustomer},
        {QStringLiteral("sales_by_item"), &Reports::salesByItem},
        {QStringLiteral("expenses_by_vendor"), &Reports::expensesByVendor},
        {QStringLiteral("expenses_by_category"), &Reports::expensesByCategory},
        {QStringLiteral("budget_vs_actual"), &Reports::budgetVsActual},
    };
    if (ranged.contains(id)) {
        return (reports.*ranged.value(id))(from, to, basis);
    }
    if (id == QLatin1String("sales_tax_liability")) {
        return reports.salesTaxLiability(from, to);
    }
    using AsOf = ReportTable (Reports::*)(const QDate &) const;
    static const QHash<QString, AsOf> asOfReports{
        {QStringLiteral("ar_aging_summary"), &Reports::arAgingSummary},
        {QStringLiteral("ar_aging_detail"), &Reports::arAgingDetail},
        {QStringLiteral("ap_aging_summary"), &Reports::apAgingSummary},
        {QStringLiteral("ap_aging_detail"), &Reports::apAgingDetail},
        {QStringLiteral("customer_balances"), &Reports::customerBalances},
        {QStringLiteral("vendor_balances"), &Reports::vendorBalances},
        {QStringLiteral("open_invoices"), &Reports::openInvoices},
        {QStringLiteral("unpaid_bills"), &Reports::unpaidBills},
    };
    if (asOfReports.contains(id)) {
        return (reports.*asOfReports.value(id))(asOf);
    }
    if (error != nullptr) {
        *error = tr("Unknown report \"%1\".").arg(id);
    }
    return std::nullopt;
}

QVariantMap BooksService::runReport(const QString &id, const QVariantMap &options) const
{
    QString error;
    const std::optional<ReportTable> table = buildReport(id, options, &error);
    if (!table) {
        return Format::failure(error);
    }
    QVariantMap out = Format::report(*table, moneyFormat());
    out.insert(QStringLiteral("ok"), true);
    out.insert(QStringLiteral("options"), optionsFor(id));
    return out;
}

QVariantList BooksService::drillDown(const QVariantMap &drillMap) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const DrillDown drill = Format::drillFrom(drillMap);
    const MoneyFormat money = moneyFormat();
    Documents documents(*c);
    const QDate from = drill.from.isValid() ? drill.from : QDate(1900, 1, 1);
    const QDate to = drill.to.isValid() ? drill.to : QDate(9999, 12, 31);
    const auto row = [&](const BusinessDocument &d, qint64 amount, const QString &memo) {
        return QVariantMap{{QStringLiteral("documentId"), d.id},
                           {QStringLiteral("docId"), d.id},
                           {QStringLiteral("date"), Format::isoDate(d.date)},
                           {QStringLiteral("kind"), Format::kindKey(d.kind)},
                           {QStringLiteral("kindLabel"), Format::kindLabel(d.kind)},
                           {QStringLiteral("number"), d.number.isEmpty() ? d.reference : d.number},
                           {QStringLiteral("name"), nameOfParty(d.partyId)},
                           {QStringLiteral("memo"), memo},
                           {QStringLiteral("amount"), amount},
                           {QStringLiteral("amountText"), money.text(amount)}};
    };
    if (drill.accountId != 0) {
        // What posted to the account in the period: the General Ledger's
        // detail rows, in the account's normal sign.
        const std::optional<Account> account = ChartOfAccounts(*c).account(drill.accountId);
        const bool debitNormal = account && accountTypeInfo(account->type).normalBalance == NormalBalance::Debit;
        const ReportTable gl = Reports(*c).generalLedger(from, to, drill.accountId);
        const int debitCol = gl.columnIndex(QStringLiteral("debit"));
        const int creditCol = gl.columnIndex(QStringLiteral("credit"));
        const int memoCol = gl.columnIndex(QStringLiteral("memo"));
        for (const ReportRow &r : gl.rows) {
            if (r.kind != RowKind::Detail || r.drill.documentIds.isEmpty()) {
                continue;
            }
            const std::optional<BusinessDocument> d = documents.load(r.drill.documentIds.first());
            if (!d) {
                continue;
            }
            const qint64 net = r.cells.value(debitCol).value - r.cells.value(creditCol).value;
            out << row(*d, debitNormal ? net : -net, r.cells.value(memoCol).text);
        }
        return out;
    }
    QList<BusinessDocument> docs;
    if (!drill.documentIds.isEmpty()) {
        for (qint64 id : drill.documentIds) {
            if (const std::optional<BusinessDocument> d = documents.load(id)) {
                docs << *d;
            }
        }
    } else if (drill.partyId != 0 || drill.itemId != 0) {
        DocumentFilter f;
        f.partyId = drill.partyId;
        f.from = drill.from;
        f.to = drill.to;
        f.includeDrafts = false;
        docs = documents.list(f);
    }
    for (const BusinessDocument &d : std::as_const(docs)) {
        if (d.kind == DocKind::Estimate || d.kind == DocKind::InvoiceTemplate) {
            continue;
        }
        if (drill.itemId == 0) {
            out << row(d, d.total.minor(), d.memo);
            continue;
        }
        const DocumentTotals totals = documents.computeTotals(d);
        qint64 sum = 0;
        bool found = false;
        for (qsizetype i = 0; i < d.lines.size(); ++i) {
            if (d.lines[i].itemId == drill.itemId) {
                found = true;
                sum += i < totals.lineAmounts.size() ? totals.lineAmounts[i].minor() : d.lines[i].amount.minor();
            }
        }
        if (found) {
            out << row(d, sum, d.memo);
        }
    }
    return out;
}

QVariantMap BooksService::exportReport(const QString &id, const QVariantMap &options, const QUrl &fileUrl,
                                       const QString &format)
{
    QString error;
    const std::optional<ReportTable> table = buildReport(id, options, &error);
    if (!table) {
        return Format::failure(error);
    }
    const QString path = localPath(fileUrl);
    const bool ok = format == QLatin1String("csv") ? writeFile(path, reportToCsv(*table), &error)
                                                   : ReportPdf::writeReportPdf(*file(), *table, path, &error);
    if (!ok) {
        return Format::failure(error);
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("path"), path);
    return out;
}

QVariantMap BooksService::rangePreset(const QString &name) const
{
    const QDate today = todayDate();
    const int fiscalMonth = file() != nullptr ? file()->settings().fiscalYearStartMonth : 1;
    CompanySettings s;
    s.fiscalYearStartMonth = fiscalMonth;
    const QDate fyStart = s.fiscalYearStart(today);
    const QDate monthStart(today.year(), today.month(), 1);
    // Fiscal quarters count from the fiscal year's first month.
    const int monthsIn = (today.year() - fyStart.year()) * 12 + today.month() - fyStart.month();
    const QDate quarterStart = fyStart.addMonths((monthsIn / 3) * 3);
    QDate from;
    QDate to;
    QString label;
    if (name == QLatin1String("thisMonth")) {
        from = monthStart;
        to = monthStart.addMonths(1).addDays(-1);
        label = tr("This month");
    } else if (name == QLatin1String("lastMonth")) {
        from = monthStart.addMonths(-1);
        to = monthStart.addDays(-1);
        label = tr("Last month");
    } else if (name == QLatin1String("thisQuarter")) {
        from = quarterStart;
        to = quarterStart.addMonths(3).addDays(-1);
        label = tr("This quarter");
    } else if (name == QLatin1String("lastQuarter")) {
        from = quarterStart.addMonths(-3);
        to = quarterStart.addDays(-1);
        label = tr("Last quarter");
    } else if (name == QLatin1String("thisYear")) {
        from = fyStart;
        to = fyStart.addYears(1).addDays(-1);
        label = fiscalMonth == 1 ? tr("This year") : tr("This fiscal year");
    } else if (name == QLatin1String("lastYear")) {
        from = fyStart.addYears(-1);
        to = fyStart.addDays(-1);
        label = fiscalMonth == 1 ? tr("Last year") : tr("Last fiscal year");
    } else if (name == QLatin1String("yearToDate")) {
        from = fyStart;
        to = today;
        label = tr("Year to date");
    } else if (name == QLatin1String("last12Months")) {
        from = monthStart.addMonths(-11);
        to = monthStart.addMonths(1).addDays(-1);
        label = tr("Last 12 months");
    } else if (name == QLatin1String("today")) {
        from = today;
        to = today;
        label = tr("Today");
    } else {
        return {{QStringLiteral("from"), QString()}, {QStringLiteral("to"), QString()}, {QStringLiteral("label"), QString()}};
    }
    return {{QStringLiteral("from"), Format::isoDate(from)},
            {QStringLiteral("to"), Format::isoDate(to)},
            {QStringLiteral("label"), label}};
}

} // namespace QindaOffice::Apps::Books
