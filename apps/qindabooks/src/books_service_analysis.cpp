// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: the dashboard (KPIs, trends, expense mix, ratios) and
// housekeeping (backups, accountant exports, list import).
#include "books_service.h"

#include "accounts.h"
#include "analysis.h"
#include "backups.h"
#include "company_file.h"
#include "documents.h"
#include "ledger_export.h"
#include "ledger_query.h"
#include "purchases.h"
#include "sales.h"

#include <QFile>
#include <QStandardPaths>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

struct PeriodTotals {
    qint64 income = 0;
    qint64 expenses = 0;
};

// Income (income + other income) and expenses (COGS + expenses + other
// expenses) over [from, to] on `basis` — the same split as
// FinancialAnalysis's trends, so the KPI cards and the chart agree.
PeriodTotals periodTotals(CompanyFile &file, const QHash<qint64, AccountType> &types, const QDate &from,
                          const QDate &to, Basis basis)
{
    PeriodTotals t;
    for (auto [account, net] : LedgerQuery(file).netByAccount({from, to, basis, {}}).asKeyValueRange()) {
        switch (types.value(account)) {
        case AccountType::Income:
        case AccountType::OtherIncome: t.income -= net; break;
        case AccountType::CostOfGoodsSold:
        case AccountType::Expense:
        case AccountType::OtherExpense: t.expenses += net; break;
        default: break;
        }
    }
    return t;
}

// AGENT-CONTRACT (list keys, with ListsDialog.qml): the plural keys are the
// API's ("customers", "vendors", "items", "accounts"). The singular party
// kinds the rest of the API uses ("customer", "vendor") and the obvious
// synonyms are accepted too — the window once sent "customer" here and
// every import failed without a word (M63).
std::optional<ListKind> listKindFrom(const QString &raw)
{
    const QString key = raw.trimmed().toLower();
    for (const auto &[kind, names] :
         {std::pair{ListKind::Accounts, QStringList{QStringLiteral("accounts"), QStringLiteral("account"),
                                                    QStringLiteral("chart")}},
          std::pair{ListKind::Customers, QStringList{QStringLiteral("customers"), QStringLiteral("customer")}},
          std::pair{ListKind::Vendors, QStringList{QStringLiteral("vendors"), QStringLiteral("vendor"),
                                                   QStringLiteral("suppliers"), QStringLiteral("supplier")}},
          std::pair{ListKind::Items, QStringList{QStringLiteral("items"), QStringLiteral("item"),
                                                 QStringLiteral("products"), QStringLiteral("product"),
                                                 QStringLiteral("services")}}}) {
        if (names.contains(key)) {
            return kind;
        }
    }
    return std::nullopt;
}

QString listKey(ListKind kind)
{
    switch (kind) {
    case ListKind::Accounts: return QStringLiteral("accounts");
    case ListKind::Customers: return QStringLiteral("customers");
    case ListKind::Vendors: return QStringLiteral("vendors");
    case ListKind::Items: return QStringLiteral("items");
    }
    return QStringLiteral("customers");
}

// Rule-of-thumb health of a ratio for the dashboard's colour: margins
// positive, liquidity at least 1×, debt below 2× equity, customers paying
// within 45 days, bills paid within 60.
bool ratioIsGood(const Ratio &r)
{
    if (!r.available) {
        return false;
    }
    const qint64 one = pow10i(r.scale);
    if (r.id == QLatin1String("gross_margin") || r.id == QLatin1String("net_margin")) {
        return r.value > 0;
    }
    if (r.id == QLatin1String("current_ratio") || r.id == QLatin1String("quick_ratio")) {
        return r.value >= one;
    }
    if (r.id == QLatin1String("debt_to_equity")) {
        return r.value >= 0 && r.value < 2 * one;
    }
    if (r.id == QLatin1String("dso")) {
        return r.value <= 45;
    }
    return r.id != QLatin1String("dpo") || r.value <= 60;
}

} // namespace

QVariantMap BooksService::kpis(const QString &asOfIso) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    QDate asOf = Format::date(asOfIso);
    if (!asOf.isValid()) {
        asOf = todayDate();
    }
    const CompanySettings settings = c->settings();
    ChartOfAccounts chart(*c);
    QHash<qint64, AccountType> types;
    for (const Account &a : chart.accounts(true)) {
        types.insert(a.id, a.type);
    }
    const PeriodTotals mtd = periodTotals(*c, types, QDate(asOf.year(), asOf.month(), 1), asOf, settings.basis);
    const PeriodTotals ytd = periodTotals(*c, types, settings.fiscalYearStart(asOf), asOf, settings.basis);
    const qint64 undeposited = chart.idForRole(SystemRole::UndepositedFunds);
    qint64 cash = 0;
    for (auto [account, net] : LedgerQuery(*c).netByAccount({{}, asOf, settings.basis, {}}).asKeyValueRange()) {
        if (types.value(account) == AccountType::Bank || account == undeposited) {
            cash += net;
        }
    }
    Documents documents(*c);
    qint64 open = 0;
    qint64 overdue = 0;
    int openCount = 0;
    int overdueCount = 0;
    for (const BusinessDocument &d : Sales(*c).openInvoices(0, asOf)) {
        const qint64 balance = documents.openBalance(d.id, asOf).minor();
        open += balance;
        ++openCount;
        if (d.dueDate.isValid() && d.dueDate < asOf) {
            overdue += balance;
            ++overdueCount;
        }
    }
    qint64 bills = 0;
    int billCount = 0;
    for (const BusinessDocument &d : Purchases(*c).unpaidBills(0, asOf)) {
        bills += documents.openBalance(d.id, asOf).minor();
        ++billCount;
    }
    // AGENT-CONTRACT (kpis keys, with the lead's DashboardView): amounts
    // are minor units with a `…Text` twin; the three counts are plain ints.
    const MoneyFormat money = moneyFormat();
    QVariantMap out{{QStringLiteral("ok"), true},
                    {QStringLiteral("asOf"), Format::isoDate(asOf)},
                    {QStringLiteral("openInvoiceCount"), openCount},
                    {QStringLiteral("overdueInvoices"), overdueCount},
                    {QStringLiteral("unpaidBillCount"), billCount}};
    for (const auto &[key, value] :
         {std::pair{"incomeMtd", mtd.income}, std::pair{"expensesMtd", mtd.expenses},
          std::pair{"netMtd", mtd.income - mtd.expenses}, std::pair{"incomeYtd", ytd.income},
          std::pair{"expensesYtd", ytd.expenses}, std::pair{"netYtd", ytd.income - ytd.expenses},
          std::pair{"openInvoices", open}, std::pair{"overdueAmount", overdue}, std::pair{"unpaidBills", bills},
          std::pair{"cash", cash}}) {
        out.insert(QString::fromLatin1(key), value);
        out.insert(QString::fromLatin1(key) + QStringLiteral("Text"), money.text(value));
    }
    return out;
}

QVariantList BooksService::monthlyTrends(int months) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    for (const TrendPoint &p : FinancialAnalysis(*c).monthlyTrends(todayDate(), qBound(1, months, 120))) {
        QVariantMap row{{QStringLiteral("month"), p.month.toString(QStringLiteral("yyyy-MM"))},
                        {QStringLiteral("label"), money.locale.toString(p.month, QStringLiteral("MMM yyyy"))},
                        {QStringLiteral("shortLabel"), money.locale.toString(p.month, QStringLiteral("MMM"))}};
        for (const auto &[key, value] : {std::pair{"income", p.revenue.minor()}, std::pair{"expenses", p.expenses.minor()},
                                         std::pair{"net", p.netIncome.minor()}, std::pair{"cash", p.cashBalance.minor()}}) {
            row.insert(QString::fromLatin1(key), value);
            row.insert(QString::fromLatin1(key) + QStringLiteral("Text"), money.text(value));
            // AGENT-GUARD: chart geometry only (books_format.h contract).
            row.insert(QString::fromLatin1(key) + QStringLiteral("Chart"), money.chart(value));
        }
        out << row;
    }
    return out;
}

QVariantList BooksService::expenseBreakdown(const QString &fromIso, const QString &toIso) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    QDate from = Format::date(fromIso);
    QDate to = Format::date(toIso);
    if (!to.isValid()) {
        to = todayDate();
    }
    if (!from.isValid()) {
        from = c->settings().fiscalYearStart(to);
    }
    const MoneyFormat money = moneyFormat();
    for (const ExpenseShare &s : FinancialAnalysis(*c).expenseBreakdown(from, to)) {
        out << QVariantMap{{QStringLiteral("accountId"), s.accountId},
                           {QStringLiteral("label"), s.name},
                           {QStringLiteral("amount"), s.amount.minor()},
                           {QStringLiteral("amountText"), money.text(s.amount.minor())},
                           {QStringLiteral("amountChart"), money.chart(s.amount.minor())},
                           // share: percent with one decimal as a number for
                           // the chart; shareText the exact figure.
                           {QStringLiteral("share"), double(s.share) / 10.0},
                           {QStringLiteral("shareText"), Decimal(s.share, 1).toString() + QLatin1Char('%')},
                           {QStringLiteral("definition"), s.definition}};
    }
    return out;
}

QVariantList BooksService::ratios(const QString &asOfIso) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    QDate asOf = Format::date(asOfIso);
    if (!asOf.isValid()) {
        asOf = todayDate();
    }
    for (const Ratio &r : FinancialAnalysis(*c).ratios(c->settings().fiscalYearStart(asOf), asOf)) {
        out << QVariantMap{{QStringLiteral("id"), r.id},
                           {QStringLiteral("label"), r.title},
                           {QStringLiteral("valueText"), r.display()},
                           {QStringLiteral("value"), r.value},
                           {QStringLiteral("scale"), r.scale},
                           {QStringLiteral("available"), r.available},
                           {QStringLiteral("definition"), r.definition},
                           {QStringLiteral("good"), ratioIsGood(r)}};
    }
    return out;
}

// ---- housekeeping ----------------------------------------------------------------

QVariantMap BooksService::backupNow()
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    // AGENT-NOTE: automatic copies live in the user's data dir, not next
    // to the file — a company on a USB stick is not backed up by a copy on
    // the same stick. Backup… (backupTo) writes wherever the user picks.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/qindaoffice/books/backups");
    QString path;
    const Result r = Backups::createTimestamped(*c, dir, 20, &path);
    QVariantMap out = Format::result(r);
    out.insert(QStringLiteral("path"), path);
    return out;
}

QVariantMap BooksService::backupTo(const QUrl &fileUrl)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const QString path = localPath(fileUrl);
    if (QFile::exists(path)) {
        return Format::failure(tr("%1 already exists; a backup never replaces a file.").arg(path));
    }
    QVariantMap out = Format::result(c->backupTo(path));
    out.insert(QStringLiteral("path"), path);
    return out;
}

QVariantMap BooksService::exportIif(const QUrl &fileUrl)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    QString error;
    const QString path = localPath(fileUrl);
    if (!writeFile(path, LedgerExport::iif(*c), &error)) {
        return Format::failure(error);
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("path"), path);
    return out;
}

QVariantMap BooksService::exportJournalCsv(const QUrl &fileUrl)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    QString error;
    const QString path = localPath(fileUrl);
    if (!writeFile(path, LedgerExport::journalCsv(*c), &error)) {
        return Format::failure(error);
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("path"), path);
    return out;
}

QVariantMap BooksService::exportListCsv(const QString &kind, const QUrl &fileUrl)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const std::optional<ListKind> list = listKindFrom(kind);
    if (!list) {
        return Format::failure(tr("Unknown list \"%1\" (accounts, customers, vendors, items).").arg(kind));
    }
    QString error;
    const QString path = localPath(fileUrl);
    if (!writeFile(path, LedgerExport::listCsv(*c, *list), &error)) {
        return Format::failure(error);
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("path"), path);
    out.insert(QStringLiteral("kind"), listKey(*list));
    return out;
}

// AGENT-CONTRACT (result, with ListsDialog.qml): {ok, error, kind, added,
// skipped, errors[]}. ok is false only when nothing could be imported and
// something was wrong; rows refused next to rows added come back in
// `errors` with ok true, so the dialog shows both.
QVariantMap BooksService::importListCsv(const QString &kind, const QUrl &fileUrl)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const std::optional<ListKind> list = listKindFrom(kind);
    if (!list) {
        return Format::failure(tr("Unknown list \"%1\" (accounts, customers, vendors, items).").arg(kind));
    }
    QFile in(localPath(fileUrl));
    if (!in.open(QIODevice::ReadOnly)) {
        return Format::failure(tr("%1 could not be read: %2").arg(localPath(fileUrl), in.errorString()));
    }
    const ImportReport report = ListImport::importCsv(*c, *list, in.readAll());
    QVariantMap out = report.added > 0 || report.errors.isEmpty()
        ? Format::success()
        : Format::failure(report.errors.value(0));
    out.insert(QStringLiteral("kind"), listKey(*list));
    out.insert(QStringLiteral("added"), report.added);
    out.insert(QStringLiteral("skipped"), report.skipped);
    out.insert(QStringLiteral("errors"), report.errors);
    return out;
}

} // namespace QindaOffice::Apps::Books
