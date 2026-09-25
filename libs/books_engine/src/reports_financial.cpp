// SPDX-License-Identifier: GPL-3.0-or-later
// Profit and Loss, Balance Sheet and Trial Balance.
#include "reports.h"

#include "company_file.h"
#include "ledger_query.h"
#include "lists.h"
#include "reconciliation.h"
#include "reports_util.h"

namespace QindaOffice::Books {

using namespace ReportUtil;

namespace {

bool isPnl(AccountType type)
{
    return !accountTypeInfo(type).isBalanceSheet();
}

// Removes an empty section (header + zero total, no account rows).
void pruneEmptySection(ReportTable &t, const QString &key)
{
    const int totalRow = int(t.rows.size()) - 1;
    if (totalRow < 1 || t.rows[totalRow].key != QStringLiteral("total:") + key
        || t.rows[totalRow - 1].key != QStringLiteral("section:") + key) {
        return;
    }
    for (int c = 1; c < t.rows[totalRow].cells.size(); ++c) {
        if (t.rows[totalRow].cells[c].value != 0) {
            return;
        }
    }
    t.rows.remove(totalRow - 1, 2);
}

// A percent column after the money columns: change / |prior|.
void appendChangePercent(ReportTable &t, int changeCol, int priorCol)
{
    t.columns << ReportColumn{QStringLiteral("change_pct"), QStringLiteral("% Change"), ColumnType::Percent};
    for (ReportRow &r : t.rows) {
        if (r.kind == RowKind::Section || r.cells.size() <= changeCol || r.cells[changeCol].empty) {
            r.cells << ReportCell::blank();
            continue;
        }
        const qint64 prior = r.cells[priorCol].value;
        r.cells << (prior == 0 ? ReportCell::blank()
                               : ReportCell::ofPercent(percentOf(r.cells[changeCol].value, prior < 0 ? -prior : prior)));
    }
}

QList<ValueColumn> comparisonColumns(const ValueColumn &current, const ValueColumn &prior)
{
    ValueColumn change{QStringLiteral("change"), QStringLiteral("$ Change"), {}, current.from, current.to};
    for (auto it = current.net.cbegin(); it != current.net.cend(); ++it) {
        change.net[it.key()] += it.value();
    }
    for (auto it = prior.net.cbegin(); it != prior.net.cend(); ++it) {
        change.net[it.key()] -= it.value();
    }
    return {current, prior, change};
}

} // namespace

Reports::Reports(CompanyFile &file)
    : m_file(file)
{
}

Basis Reports::basisOr(std::optional<Basis> basis) const
{
    return basis.value_or(m_file.settings().basis);
}

ReportTable Reports::reconciliation(qint64 sessionId) const
{
    return Reconciliation(m_file).report(sessionId);
}

ReportTable Reports::profitAndLoss(const PnlOptions &options) const
{
    const Basis basis = basisOr(options.basis);
    ReportTable t = makeTable(m_file, QStringLiteral("profit_and_loss"), QStringLiteral("Profit and Loss"),
                              periodText(options.from, options.to) + QStringLiteral(" · ") + basisText(basis));
    LedgerQuery ledger(m_file);
    QList<ValueColumn> columns;
    ValueColumn total{QStringLiteral("total"), QStringLiteral("Total"),
                      ledger.netByAccount({options.from, options.to, basis, {}}), options.from, options.to};
    if (options.columns == PnlColumns::Months) {
        QHash<int, QHash<qint64, qint64>> byMonth;
        for (const LedgerSum &s : ledger.aggregate({options.from, options.to, basis, {}}, GroupAccount | GroupMonth)) {
            byMonth[s.month][s.accountId] += s.net();
        }
        for (int m = LedgerQuery::monthIndex(options.from); m <= LedgerQuery::monthIndex(options.to); ++m) {
            const QDate start = LedgerQuery::monthStart(m);
            columns << ValueColumn{start.toString(QStringLiteral("yyyy-MM")), start.toString(QStringLiteral("MMM yyyy")),
                                   byMonth.value(m), qMax(start, options.from),
                                   qMin(start.addMonths(1).addDays(-1), options.to)};
        }
        columns << total;
    } else if (options.columns == PnlColumns::Classes) {
        QHash<qint64, QHash<qint64, qint64>> byClass;
        for (const LedgerSum &s : ledger.aggregate({options.from, options.to, basis, {}}, GroupAccount | GroupClass)) {
            byClass[s.classId][s.accountId] += s.net();
        }
        for (const ClassTag &c : Lists(m_file).classes(true)) {
            columns << ValueColumn{QStringLiteral("class:%1").arg(c.id), c.name, byClass.value(c.id), options.from,
                                   options.to};
        }
        columns << ValueColumn{QStringLiteral("class:0"), QStringLiteral("Unclassified"), byClass.value(0),
                               options.from, options.to};
        columns << total;
    } else if (options.comparison != Comparison::None) {
        QDate pFrom = options.from.addYears(-1);
        QDate pTo = options.to.addYears(-1);
        if (options.comparison == Comparison::PriorPeriod) {
            pTo = options.from.addDays(-1);
            const bool wholeMonths = options.from.day() == 1 && options.to.addDays(1).day() == 1;
            if (wholeMonths) {
                // March → February, Q2 → Q1: the same number of months.
                const int months = LedgerQuery::monthIndex(options.to) - LedgerQuery::monthIndex(options.from) + 1;
                pFrom = options.from.addMonths(-months);
            } else {
                pFrom = pTo.addDays(-options.from.daysTo(options.to));
            }
        }
        const ValueColumn prior{QStringLiteral("prior"), periodText(pFrom, pTo), ledger.netByAccount({pFrom, pTo, basis, {}}),
                                pFrom, pTo};
        total.title = periodText(options.from, options.to);
        columns = comparisonColumns(total, prior);
    } else {
        columns << total;
    }
    setColumns(t, QString(), columns);
    const QList<Account> chart = ChartOfAccounts(m_file).accounts(true);
    const QList<qint64> income = appendSection(t, chart, {AccountType::Income}, QStringLiteral("income"),
                                               QStringLiteral("Income"), columns, -1);
    const QList<qint64> cogs = appendSection(t, chart, {AccountType::CostOfGoodsSold}, QStringLiteral("cogs"),
                                             QStringLiteral("Cost of Goods Sold"), columns, 1);
    pruneEmptySection(t, QStringLiteral("cogs"));
    const QList<qint64> gross = add(income, cogs, -1);
    t.rows << moneyRow(RowKind::Total, QStringLiteral("total:gross_profit"), QStringLiteral("Gross Profit"), gross);
    const QList<qint64> expenses = appendSection(t, chart, {AccountType::Expense}, QStringLiteral("expenses"),
                                                 QStringLiteral("Expenses"), columns, 1);
    const QList<qint64> operating = add(gross, expenses, -1);
    t.rows << moneyRow(RowKind::Total, QStringLiteral("total:net_operating_income"),
                       QStringLiteral("Net Operating Income"), operating);
    const QList<qint64> otherIncome = appendSection(t, chart, {AccountType::OtherIncome}, QStringLiteral("other_income"),
                                                    QStringLiteral("Other Income"), columns, -1);
    pruneEmptySection(t, QStringLiteral("other_income"));
    const QList<qint64> otherExpenses = appendSection(t, chart, {AccountType::OtherExpense},
                                                      QStringLiteral("other_expenses"), QStringLiteral("Other Expenses"),
                                                      columns, 1);
    pruneEmptySection(t, QStringLiteral("other_expenses"));
    const QList<qint64> netOther = add(otherIncome, otherExpenses, -1);
    t.rows << moneyRow(RowKind::Total, QStringLiteral("total:net_other_income"), QStringLiteral("Net Other Income"),
                       netOther);
    t.rows << moneyRow(RowKind::Total, QStringLiteral("total:net_income"), QStringLiteral("Net Income"),
                       add(operating, netOther));
    if (options.comparison != Comparison::None && options.columns == PnlColumns::Total) {
        appendChangePercent(t, 3, 2);
    }
    return t;
}

namespace {

// Balance-sheet values as of `asOf`: permanent accounts cumulatively, the
// P&L of earlier fiscal years folded into Retained Earnings, and this
// fiscal year's P&L returned separately as net income (credit-positive).
ValueColumn balanceColumn(CompanyFile &file, const QString &id, const QDate &asOf, Basis basis, qint64 *netIncome)
{
    const QList<Account> chart = ChartOfAccounts(file).accounts(true);
    QHash<qint64, AccountType> types;
    for (const Account &a : chart) {
        types.insert(a.id, a.type);
    }
    const QDate fyStart = file.settings().fiscalYearStart(asOf);
    LedgerQuery ledger(file);
    ValueColumn column{id, QStringLiteral("As of %1").arg(asOf.toString(QStringLiteral("MMM d, yyyy"))), {}, {}, asOf};
    qint64 priorPnl = 0;
    for (auto [account, net] : ledger.netByAccount({{}, asOf, basis, {}}).asKeyValueRange()) {
        if (isPnl(types.value(account))) {
            continue;
        }
        column.net[account] += net;
    }
    for (auto [account, net] : ledger.netByAccount({{}, fyStart.addDays(-1), basis, {}}).asKeyValueRange()) {
        if (isPnl(types.value(account))) {
            priorPnl += net;
        }
    }
    qint64 current = 0;
    for (auto [account, net] : ledger.netByAccount({fyStart, asOf, basis, {}}).asKeyValueRange()) {
        if (isPnl(types.value(account))) {
            current += net;
        }
    }
    column.net[ChartOfAccounts(file).idForRole(SystemRole::RetainedEarnings)] += priorPnl;
    *netIncome = -current;
    return column;
}

} // namespace

ReportTable Reports::balanceSheet(const BalanceSheetOptions &options) const
{
    const Basis basis = basisOr(options.basis);
    ReportTable t = makeTable(m_file, QStringLiteral("balance_sheet"), QStringLiteral("Balance Sheet"),
                              periodText({}, options.asOf) + QStringLiteral(" · ") + basisText(basis));
    QList<ValueColumn> columns;
    QList<qint64> netIncome;
    qint64 ni = 0;
    columns << balanceColumn(m_file, QStringLiteral("total"), options.asOf, basis, &ni);
    netIncome << ni;
    if (options.comparison != Comparison::None) {
        const QDate prior = options.comparison == Comparison::PriorYear
            ? options.asOf.addYears(-1)
            : QDate(options.asOf.year(), options.asOf.month(), 1).addDays(-1);
        qint64 priorNi = 0;
        ValueColumn p = balanceColumn(m_file, QStringLiteral("prior"), prior, basis, &priorNi);
        columns = comparisonColumns(columns.first(), p);
        netIncome = {ni, priorNi, ni - priorNi};
    }
    setColumns(t, QString(), columns);
    const QList<Account> chart = ChartOfAccounts(m_file).accounts(true);
    const int n = int(columns.size());
    t.rows << sectionRow(QStringLiteral("section:assets"), QStringLiteral("Assets"), n + 1);
    QList<qint64> assets = appendSection(t, chart,
                                         {AccountType::Bank, AccountType::AccountsReceivable, AccountType::OtherCurrentAsset},
                                         QStringLiteral("current_assets"), QStringLiteral("Current Assets"), columns, 1, 1);
    assets = add(assets, appendSection(t, chart, {AccountType::FixedAsset}, QStringLiteral("fixed_assets"),
                                       QStringLiteral("Fixed Assets"), columns, 1, 1));
    pruneEmptySection(t, QStringLiteral("fixed_assets"));
    assets = add(assets, appendSection(t, chart, {AccountType::OtherAsset}, QStringLiteral("other_assets"),
                                       QStringLiteral("Other Assets"), columns, 1, 1));
    pruneEmptySection(t, QStringLiteral("other_assets"));
    t.rows << moneyRow(RowKind::Total, QStringLiteral("total:assets"), QStringLiteral("Total Assets"), assets);

    t.rows << sectionRow(QStringLiteral("section:liabilities_and_equity"), QStringLiteral("Liabilities and Equity"), n + 1);
    t.rows << sectionRow(QStringLiteral("section:liabilities"), QStringLiteral("Liabilities"), n + 1, 1);
    QList<qint64> liabilities =
        appendSection(t, chart, {AccountType::AccountsPayable, AccountType::CreditCard, AccountType::OtherCurrentLiability},
                      QStringLiteral("current_liabilities"), QStringLiteral("Current Liabilities"), columns, -1, 2);
    liabilities = add(liabilities, appendSection(t, chart, {AccountType::LongTermLiability},
                                                 QStringLiteral("long_term_liabilities"),
                                                 QStringLiteral("Long-Term Liabilities"), columns, -1, 2));
    pruneEmptySection(t, QStringLiteral("long_term_liabilities"));
    t.rows << moneyRow(RowKind::Subtotal, QStringLiteral("total:liabilities"), QStringLiteral("Total Liabilities"),
                       liabilities, 1);
    QList<qint64> equity = appendSection(t, chart, {AccountType::Equity}, QStringLiteral("equity"),
                                         QStringLiteral("Equity"), columns, -1, 1);
    // Net income sits inside Equity, before its total.
    ReportRow totalEquity = t.rows.takeLast();
    t.rows << moneyRow(RowKind::Account, QStringLiteral("net_income"), QStringLiteral("Net Income"), netIncome, 2);
    equity = add(equity, netIncome);
    totalEquity = moneyRow(RowKind::Subtotal, totalEquity.key, totalEquity.cells.first().text, equity, 1);
    t.rows << totalEquity;
    t.rows << moneyRow(RowKind::Total, QStringLiteral("total:liabilities_and_equity"),
                       QStringLiteral("Total Liabilities and Equity"), add(liabilities, equity));
    return t;
}

ReportTable Reports::trialBalance(const QDate &asOf, std::optional<Basis> basisOpt) const
{
    const Basis basis = basisOr(basisOpt);
    ReportTable t = makeTable(m_file, QStringLiteral("trial_balance"), QStringLiteral("Trial Balance"),
                              periodText({}, asOf) + QStringLiteral(" · ") + basisText(basis));
    t.columns = {{QStringLiteral("label"), QString(), ColumnType::Text},
                 {QStringLiteral("debit"), QStringLiteral("Debit"), ColumnType::Money},
                 {QStringLiteral("credit"), QStringLiteral("Credit"), ColumnType::Money}};
    qint64 unusedNi = 0;
    ValueColumn permanent = balanceColumn(m_file, QStringLiteral("bs"), asOf, basis, &unusedNi);
    const QDate fyStart = m_file.settings().fiscalYearStart(asOf);
    const QHash<qint64, qint64> pnl = LedgerQuery(m_file).netByAccount({fyStart, asOf, basis, {}});
    qint64 debits = 0;
    qint64 credits = 0;
    for (const Account &a : ChartOfAccounts(m_file).accounts(true)) {
        const qint64 net = isPnl(a.type) ? pnl.value(a.id) : permanent.net.value(a.id);
        if (net == 0) {
            continue;
        }
        ReportRow r;
        r.kind = RowKind::Account;
        r.key = QStringLiteral("account:%1").arg(a.id);
        r.cells = {ReportCell::ofText(a.number.isEmpty() ? a.fullName : a.number + QLatin1Char(' ') + a.fullName),
                   net > 0 ? ReportCell::ofMoney(net) : ReportCell::blank(),
                   net < 0 ? ReportCell::ofMoney(-net) : ReportCell::blank()};
        r.drill.accountId = a.id;
        r.drill.to = asOf;
        t.rows << r;
        (net > 0 ? debits : credits) += net > 0 ? net : -net;
    }
    t.rows << moneyRow(RowKind::Total, QStringLiteral("total"), QStringLiteral("Total"), {debits, credits});
    return t;
}

} // namespace QindaOffice::Books
