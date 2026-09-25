// SPDX-License-Identifier: GPL-3.0-or-later
#include "analysis.h"

#include "accounts.h"
#include "company_file.h"
#include "ledger_query.h"

#include <QHash>

#include <algorithm>

namespace QindaOffice::Books {

namespace {

struct Totals {
    qint64 income = 0;       // credit-positive
    qint64 otherIncome = 0;
    qint64 cogs = 0;         // debit-positive
    qint64 expenses = 0;
    qint64 otherExpenses = 0;
};

Totals pnlTotals(const QHash<qint64, qint64> &net, const QHash<qint64, AccountType> &types)
{
    Totals t;
    for (auto [account, value] : net.asKeyValueRange()) {
        switch (types.value(account)) {
        case AccountType::Income: t.income -= value; break;
        case AccountType::OtherIncome: t.otherIncome -= value; break;
        case AccountType::CostOfGoodsSold: t.cogs += value; break;
        case AccountType::Expense: t.expenses += value; break;
        case AccountType::OtherExpense: t.otherExpenses += value; break;
        default: break;
        }
    }
    return t;
}

Ratio makeRatio(const QString &id, const QString &title, RatioUnit unit, qint64 numerator, qint64 denominator)
{
    Ratio r;
    r.id = id;
    r.title = title;
    r.definition = FinancialAnalysis::definition(id);
    r.unit = unit;
    r.numerator = numerator;
    r.denominator = denominator;
    r.available = denominator != 0;
    // Percent: 1 decimal (×1000); times: 2 decimals (×100); days: whole.
    const qint64 factor = unit == RatioUnit::Percent ? 1000 : unit == RatioUnit::Times ? 100 : 1;
    r.scale = unit == RatioUnit::Percent ? 1 : unit == RatioUnit::Times ? 2 : 0;
    if (r.available) {
        r.value = scaleExact(numerator, factor, denominator, RoundingMode::HalfUp).value_or(0);
    }
    return r;
}

} // namespace

QString Ratio::display() const
{
    if (!available) {
        return QStringLiteral("—");
    }
    const QString number = Decimal(value, scale).toString();
    switch (unit) {
    case RatioUnit::Percent: return number + QLatin1Char('%');
    case RatioUnit::Times: return number + QStringLiteral("×");
    case RatioUnit::Days: return number + QStringLiteral(" days");
    }
    return number;
}

QString FinancialAnalysis::definition(const QString &id)
{
    static const QHash<QString, QString> text{
        {QStringLiteral("gross_margin"),
         QStringLiteral("Gross margin: (income − cost of goods sold) ÷ income. The share of each sale left after the "
                        "direct cost of delivering it.")},
        {QStringLiteral("net_margin"),
         QStringLiteral("Net margin: net income ÷ total income (including other income). The share of every sale the "
                        "business keeps after all expenses.")},
        {QStringLiteral("current_ratio"),
         QStringLiteral("Current ratio: current assets ÷ current liabilities, on the last day of the period. Above 1 "
                        "means short-term debts are covered by short-term assets.")},
        {QStringLiteral("quick_ratio"),
         QStringLiteral("Quick ratio: (cash + accounts receivable) ÷ current liabilities — the current ratio without "
                        "assets that are slow to turn into cash.")},
        {QStringLiteral("debt_to_equity"),
         QStringLiteral("Debt to equity: total liabilities ÷ total equity (including this year's net income). How much "
                        "of the business is financed by what it owes rather than by its owners.")},
        {QStringLiteral("dso"),
         QStringLiteral("Days sales outstanding: accounts receivable at period end ÷ income for the period × days in "
                        "the period. How long customers take to pay, on average.")},
        {QStringLiteral("dpo"),
         QStringLiteral("Days payable outstanding: accounts payable at period end ÷ (cost of goods sold + expenses) × "
                        "days in the period. How long the business takes to pay its bills.")},
        {QStringLiteral("expense_share"),
         QStringLiteral("Share of total expenses (cost of goods sold, expenses and other expenses) in the period.")},
        {QStringLiteral("revenue"), QStringLiteral("Revenue: income plus other income recognised in the month.")},
        {QStringLiteral("expenses"),
         QStringLiteral("Expenses: cost of goods sold, expenses and other expenses recognised in the month.")},
        {QStringLiteral("net_income"), QStringLiteral("Net income: revenue minus expenses for the month.")},
        {QStringLiteral("cash_balance"),
         QStringLiteral("Cash balance: bank accounts plus undeposited funds at the end of the month.")},
        {QStringLiteral("forecast"),
         QStringLiteral("Forecast: the average of the last few months' revenue and expenses, projected flat. A "
                        "trend line, not a prediction — it knows nothing about seasons or new contracts.")},
    };
    return text.value(id);
}

FinancialAnalysis::FinancialAnalysis(CompanyFile &file)
    : m_file(file)
{
}

QList<Ratio> FinancialAnalysis::ratios(const QDate &from, const QDate &to, std::optional<Basis> basisOpt) const
{
    const Basis basis = basisOpt.value_or(m_file.settings().basis);
    ChartOfAccounts chart(m_file);
    const qint64 undeposited = chart.idForRole(SystemRole::UndepositedFunds);
    QHash<qint64, AccountType> types;
    for (const Account &a : chart.accounts(true)) {
        types.insert(a.id, a.type);
    }
    LedgerQuery ledger(m_file);
    const Totals pnl = pnlTotals(ledger.netByAccount({from, to, basis, {}}), types);
    const QHash<qint64, qint64> balances = ledger.netByAccount({{}, to, basis, {}});
    qint64 currentAssets = 0, quickAssets = 0, receivable = 0, currentLiabilities = 0, liabilities = 0;
    qint64 payable = 0, equity = 0;
    for (auto [account, net] : balances.asKeyValueRange()) {
        const AccountType type = types.value(account);
        const AccountTypeInfo info = accountTypeInfo(type);
        if (info.section == ReportSection::CurrentAssets) {
            currentAssets += net;
        }
        if (type == AccountType::Bank || type == AccountType::AccountsReceivable || account == undeposited) {
            quickAssets += net;
        }
        if (type == AccountType::AccountsReceivable) {
            receivable += net;
        }
        if (type == AccountType::AccountsPayable) {
            payable -= net;
        }
        if (info.section == ReportSection::CurrentLiabilities) {
            currentLiabilities -= net;
        }
        if (info.accountClass == AccountClass::Liability) {
            liabilities -= net;
        }
        // Equity including every period's P&L = −(assets + liabilities) net.
        if (info.accountClass == AccountClass::Equity || !info.isBalanceSheet()) {
            equity -= net;
        }
    }
    const qint64 revenue = pnl.income + pnl.otherIncome;
    const qint64 netIncome = revenue - pnl.cogs - pnl.expenses - pnl.otherExpenses;
    const qint64 days = from.daysTo(to) + 1;
    return {
        makeRatio(QStringLiteral("gross_margin"), QStringLiteral("Gross margin"), RatioUnit::Percent,
                  pnl.income - pnl.cogs, pnl.income),
        makeRatio(QStringLiteral("net_margin"), QStringLiteral("Net margin"), RatioUnit::Percent, netIncome, revenue),
        makeRatio(QStringLiteral("current_ratio"), QStringLiteral("Current ratio"), RatioUnit::Times, currentAssets,
                  currentLiabilities),
        makeRatio(QStringLiteral("quick_ratio"), QStringLiteral("Quick ratio"), RatioUnit::Times, quickAssets,
                  currentLiabilities),
        makeRatio(QStringLiteral("debt_to_equity"), QStringLiteral("Debt to equity"), RatioUnit::Times, liabilities,
                  equity),
        makeRatio(QStringLiteral("dso"), QStringLiteral("Days sales outstanding"), RatioUnit::Days, receivable * days,
                  pnl.income),
        makeRatio(QStringLiteral("dpo"), QStringLiteral("Days payable outstanding"), RatioUnit::Days, payable * days,
                  pnl.cogs + pnl.expenses),
    };
}

QList<ExpenseShare> FinancialAnalysis::expenseBreakdown(const QDate &from, const QDate &to, int top,
                                                        std::optional<Basis> basisOpt) const
{
    const Basis basis = basisOpt.value_or(m_file.settings().basis);
    const QHash<qint64, qint64> net = LedgerQuery(m_file).netByAccount({from, to, basis, {}});
    QList<ExpenseShare> all;
    qint64 total = 0;
    for (const Account &a : ChartOfAccounts(m_file).accounts(true)) {
        const AccountClass cls = accountTypeInfo(a.type).accountClass;
        if (cls != AccountClass::Expense || net.value(a.id) == 0) {
            continue;
        }
        all << ExpenseShare{a.id, a.fullName, m_file.money(net.value(a.id)), 0, definition(QStringLiteral("expense_share"))};
        total += net.value(a.id);
    }
    std::stable_sort(all.begin(), all.end(), [](const ExpenseShare &x, const ExpenseShare &y) { return x.amount > y.amount; });
    QList<ExpenseShare> out = all.mid(0, qMax(0, top));
    if (all.size() > out.size()) {
        Money rest = m_file.money(0);
        for (qsizetype i = out.size(); i < all.size(); ++i) {
            rest += all[i].amount;
        }
        out << ExpenseShare{0, QStringLiteral("Everything else"), rest, 0, definition(QStringLiteral("expense_share"))};
    }
    for (ExpenseShare &s : out) {
        s.share = total == 0 ? 0 : scaleExact(s.amount.minor(), 1000, total, RoundingMode::HalfUp).value_or(0);
    }
    return out;
}

QList<TrendPoint> FinancialAnalysis::monthlyTrends(const QDate &lastMonth, int months, std::optional<Basis> basisOpt) const
{
    const Basis basis = basisOpt.value_or(m_file.settings().basis);
    QList<TrendPoint> out;
    if (months <= 0 || !lastMonth.isValid()) {
        return out;
    }
    const int last = LedgerQuery::monthIndex(lastMonth);
    const int first = last - months + 1;
    const QDate from = LedgerQuery::monthStart(first);
    const QDate to = LedgerQuery::monthStart(last).addMonths(1).addDays(-1);
    ChartOfAccounts chart(m_file);
    const qint64 undeposited = chart.idForRole(SystemRole::UndepositedFunds);
    QHash<qint64, AccountType> types;
    for (const Account &a : chart.accounts(true)) {
        types.insert(a.id, a.type);
    }
    auto isCash = [&](qint64 account) { return types.value(account) == AccountType::Bank || account == undeposited; };
    LedgerQuery ledger(m_file);
    qint64 cash = 0;
    for (auto [account, net] : ledger.netByAccount({{}, from.addDays(-1), basis, {}}).asKeyValueRange()) {
        if (isCash(account)) {
            cash += net;
        }
    }
    QHash<int, QHash<qint64, qint64>> byMonth;
    for (const LedgerSum &s : ledger.aggregate({from, to, basis, {}}, GroupAccount | GroupMonth)) {
        byMonth[s.month][s.accountId] += s.net();
    }
    for (int m = first; m <= last; ++m) {
        const QHash<qint64, qint64> &net = byMonth[m];
        const Totals t = pnlTotals(net, types);
        for (auto [account, value] : net.asKeyValueRange()) {
            if (isCash(account)) {
                cash += value;
            }
        }
        TrendPoint p;
        p.month = LedgerQuery::monthStart(m);
        p.revenue = m_file.money(t.income + t.otherIncome);
        p.expenses = m_file.money(t.cogs + t.expenses + t.otherExpenses);
        p.netIncome = p.revenue - p.expenses;
        p.cashBalance = m_file.money(cash);
        out << p;
    }
    return out;
}

QList<TrendPoint> FinancialAnalysis::forecast(const QList<TrendPoint> &history, int monthsAhead, int window) const
{
    QList<TrendPoint> out;
    if (history.isEmpty() || monthsAhead <= 0 || window <= 0) {
        return out;
    }
    const qsizetype n = qMin<qsizetype>(window, history.size());
    Money revenue = m_file.money(0);
    Money expenses = m_file.money(0);
    for (qsizetype i = history.size() - n; i < history.size(); ++i) {
        revenue += history[i].revenue;
        expenses += history[i].expenses;
    }
    const Money avgRevenue = revenue.scaled(1, n);
    const Money avgExpenses = expenses.scaled(1, n);
    Money cash = history.last().cashBalance;
    for (int k = 1; k <= monthsAhead; ++k) {
        TrendPoint p;
        p.month = history.last().month.addMonths(k);
        p.revenue = avgRevenue;
        p.expenses = avgExpenses;
        p.netIncome = avgRevenue - avgExpenses;
        cash += p.netIncome;
        p.cashBalance = cash;
        p.forecast = true;
        out << p;
    }
    return out;
}

} // namespace QindaOffice::Books
