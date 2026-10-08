// SPDX-License-Identifier: GPL-3.0-or-later
// Statement of Cash Flows (indirect method), budgets and Budget vs Actual,
// and the report catalog.
#include "reports.h"

#include "company_file.h"
#include "ledger_query.h"
#include "reports_util.h"
#include "sql_util.h"

namespace QindaOffice::Books {

using namespace ReportUtil;

// AGENT-NOTE: the indirect method needs no classification of individual
// transactions: by double entry, Σ(debit − credit) over ALL accounts in a
// period is zero, so
//   Δcash = −Σ_P&L(net) − Σ_non-cash balance sheet(net)
//         = net income + Σ −(change of every other balance-sheet account).
// Each non-cash balance-sheet account contributes −(its net change), in
// the activity its type belongs to. "Cash" = Bank accounts + Undeposited
// Funds. tst_books_reports checks the statement ties to Δcash exactly.
ReportTable Reports::cashFlow(const QDate &from, const QDate &to, std::optional<Basis> basisOpt) const
{
    const Basis basis = basisOr(basisOpt);
    ReportTable t = makeTable(m_file, QStringLiteral("cash_flow"), QStringLiteral("Statement of Cash Flows"),
                              periodText(from, to) + QStringLiteral(" · ") + basisText(basis));
    t.columns = {{QStringLiteral("label"), QString(), ColumnType::Text},
                 {QStringLiteral("total"), QStringLiteral("Total"), ColumnType::Money}};
    ChartOfAccounts coa(m_file);
    const qint64 undeposited = coa.idForRole(SystemRole::UndepositedFunds);
    const QList<Account> chart = coa.accounts(true);
    LedgerQuery ledger(m_file);
    const QHash<qint64, qint64> period = ledger.netByAccount({from, to, basis, {}});
    auto isCash = [undeposited](const Account &a) { return a.type == AccountType::Bank || a.id == undeposited; };
    qint64 netIncome = 0;
    for (const Account &a : chart) {
        if (!accountTypeInfo(a.type).isBalanceSheet()) {
            netIncome -= period.value(a.id);
        }
    }
    enum Activity { Operating, Investing, Financing };
    auto activityOf = [](AccountType type) {
        switch (type) {
        case AccountType::FixedAsset:
        case AccountType::OtherAsset:
            return Investing;
        case AccountType::LongTermLiability:
        case AccountType::Equity:
            return Financing;
        default:
            return Operating;
        }
    };
    const QStringList keys{QStringLiteral("operating"), QStringLiteral("investing"), QStringLiteral("financing")};
    const QStringList titles{QStringLiteral("Operating Activities"), QStringLiteral("Investing Activities"),
                             QStringLiteral("Financing Activities")};
    qint64 netChange = 0;
    for (int activity = Operating; activity <= Financing; ++activity) {
        t.rows << sectionRow(QStringLiteral("section:") + keys[activity], titles[activity], 2);
        qint64 subtotal = 0;
        if (activity == Operating) {
            t.rows << moneyRow(RowKind::Account, QStringLiteral("net_income"), QStringLiteral("Net Income"), {netIncome}, 1);
            subtotal += netIncome;
        }
        for (const Account &a : chart) {
            if (!accountTypeInfo(a.type).isBalanceSheet() || isCash(a) || activityOf(a.type) != activity) {
                continue;
            }
            const qint64 impact = -period.value(a.id);
            if (impact == 0) {
                continue;
            }
            ReportRow r = moneyRow(RowKind::Account, QStringLiteral("account:%1").arg(a.id), a.fullName, {impact}, 1);
            r.drill = DrillDown{a.id, 0, 0, from, to, {}};
            t.rows << r;
            subtotal += impact;
        }
        t.rows << moneyRow(RowKind::Subtotal, QStringLiteral("total:") + keys[activity],
                           QStringLiteral("Net Cash Provided by ") + titles[activity], {subtotal});
        netChange += subtotal;
    }
    const QHash<qint64, qint64> opening = ledger.netByAccount({{}, from.addDays(-1), basis, {}});
    qint64 cashStart = 0;
    for (const Account &a : chart) {
        if (isCash(a)) {
            cashStart += opening.value(a.id);
        }
    }
    t.rows << moneyRow(RowKind::Total, QStringLiteral("total:net_change"), QStringLiteral("Net Change in Cash"), {netChange});
    t.rows << moneyRow(RowKind::Account, QStringLiteral("cash_beginning"), QStringLiteral("Cash at Beginning of Period"),
                       {cashStart});
    t.rows << moneyRow(RowKind::Total, QStringLiteral("cash_end"), QStringLiteral("Cash at End of Period"),
                       {cashStart + netChange});
    return t;
}

// ---- budgets -------------------------------------------------------------------

Budgets::Budgets(CompanyFile &file)
    : m_file(file)
{
}

Result Budgets::set(qint64 accountId, int year, int month, qint64 amountMinor, qint64 classId)
{
    if (month < 1 || month > 12 || !ChartOfAccounts(m_file).account(accountId)) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a budget needs an account and a month 1-12"));
    }
    QString error;
    if (!Sql::exec(m_file.database(),
                   QStringLiteral("INSERT INTO budgets(account_id, class_id, year, month, amount) VALUES(?,?,?,?,?)"
                                  " ON CONFLICT(account_id, class_id, year, month) DO UPDATE SET amount = excluded.amount"),
                   {accountId, classId, year, month, amountMinor}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    emit m_file.changed();
    return Result::success(accountId);
}

Result Budgets::setAnnual(qint64 accountId, int fiscalYear, qint64 amountMinor, qint64 classId)
{
    const int startMonth = m_file.settings().fiscalYearStartMonth;
    const QList<Money> months = m_file.money(amountMinor).split(12);
    SqlTransaction txn(m_file);
    for (int i = 0; i < 12; ++i) {
        const QDate d = QDate(fiscalYear, startMonth, 1).addMonths(i);
        if (const Result r = set(accountId, d.year(), d.month(), months[i].minor(), classId); !r.ok) {
            return r;
        }
    }
    txn.commit();
    return Result::success(accountId);
}

qint64 Budgets::amount(qint64 accountId, int year, int month, qint64 classId) const
{
    return Sql::scalarInt(m_file.database(),
                          QStringLiteral("SELECT amount FROM budgets WHERE account_id = ? AND class_id = ? AND year = ?"
                                         " AND month = ?"),
                          {accountId, classId, year, month});
}

QList<qint64> Budgets::fiscalYearMonths(qint64 accountId, int fiscalYear, qint64 classId) const
{
    const int startMonth = m_file.settings().fiscalYearStartMonth;
    QList<qint64> out;
    for (int i = 0; i < 12; ++i) {
        const QDate d = QDate(fiscalYear, startMonth, 1).addMonths(i);
        out << amount(accountId, d.year(), d.month(), classId);
    }
    return out;
}

Result Budgets::setFiscalYearMonths(qint64 accountId, int fiscalYear, const QList<qint64> &months, qint64 classId)
{
    if (months.size() != 12 || !ChartOfAccounts(m_file).account(accountId)) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a budget year needs an account and 12 months"));
    }
    const int startMonth = m_file.settings().fiscalYearStartMonth;
    SqlTransaction txn(m_file);
    for (int i = 0; i < 12; ++i) {
        const QDate d = QDate(fiscalYear, startMonth, 1).addMonths(i);
        QString error;
        // AGENT-NOTE: SQL directly rather than set(), which signals
        // changed() per month; the transaction signals once.
        if (!Sql::exec(m_file.database(),
                       QStringLiteral("INSERT INTO budgets(account_id, class_id, year, month, amount) VALUES(?,?,?,?,?)"
                                      " ON CONFLICT(account_id, class_id, year, month) DO UPDATE SET amount = excluded.amount"),
                       {accountId, classId, d.year(), d.month(), months.at(i)}, &error)) {
            return Result::failure(ErrorCode::Storage, error);
        }
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(accountId);
}

qint64 Budgets::total(qint64 accountId, const QDate &from, const QDate &to, qint64 classId) const
{
    const int a = LedgerQuery::monthIndex(from);
    const int b = LedgerQuery::monthIndex(to);
    return Sql::scalarInt(m_file.database(),
                          QStringLiteral("SELECT COALESCE(SUM(amount), 0) FROM budgets WHERE account_id = ?"
                                         " AND class_id = ? AND year * 12 + month - 1 BETWEEN ? AND ?"),
                          {accountId, classId, a, b});
}

ReportTable Reports::budgetVsActual(const QDate &from, const QDate &to, std::optional<Basis> basisOpt) const
{
    const Basis basis = basisOr(basisOpt);
    ReportTable t = makeTable(m_file, QStringLiteral("budget_vs_actual"), QStringLiteral("Budget vs. Actual"),
                              periodText(from, to) + QStringLiteral(" · ") + basisText(basis));
    const QList<Account> chart = ChartOfAccounts(m_file).accounts(true);
    Budgets budgets(m_file);
    ValueColumn actual{QStringLiteral("actual"), QStringLiteral("Actual"),
                       LedgerQuery(m_file).netByAccount({from, to, basis, {}}), from, to};
    ValueColumn budget{QStringLiteral("budget"), QStringLiteral("Budget"), {}, from, to};
    for (const Account &a : chart) {
        if (accountTypeInfo(a.type).isBalanceSheet()) {
            continue;
        }
        const qint64 amount = budgets.total(a.id, from, to);
        // Budgets are entered as positive amounts in the account's normal
        // sign; stored here as debit−credit like the actuals.
        const bool credit = accountTypeInfo(a.type).normalBalance == NormalBalance::Credit;
        budget.net.insert(a.id, credit ? -amount : amount);
    }
    ValueColumn over{QStringLiteral("over"), QStringLiteral("Over Budget"), {}, from, to};
    for (const Account &a : chart) {
        over.net.insert(a.id, actual.net.value(a.id) - budget.net.value(a.id));
    }
    const QList<ValueColumn> columns{actual, budget, over};
    setColumns(t, QString(), columns);
    const QList<qint64> income = appendSection(t, chart, {AccountType::Income, AccountType::OtherIncome},
                                               QStringLiteral("income"), QStringLiteral("Income"), columns, -1);
    const QList<qint64> expenses =
        appendSection(t, chart, {AccountType::CostOfGoodsSold, AccountType::Expense, AccountType::OtherExpense},
                      QStringLiteral("expenses"), QStringLiteral("Expenses"), columns, 1);
    t.rows << moneyRow(RowKind::Total, QStringLiteral("total:net_income"), QStringLiteral("Net Income"),
                       add(income, expenses, -1));
    // % of budget.
    t.columns << ReportColumn{QStringLiteral("pct_of_budget"), QStringLiteral("% of Budget"), ColumnType::Percent};
    for (ReportRow &r : t.rows) {
        const bool has = r.kind != RowKind::Section && r.cells.size() > 2 && !r.cells[2].empty && r.cells[2].value != 0;
        r.cells << (has ? ReportCell::ofPercent(percentOf(r.cells[1].value, r.cells[2].value)) : ReportCell::blank());
    }
    return t;
}

QList<Reports::Catalog> Reports::catalog()
{
    const QString overview = QStringLiteral("Business overview");
    const QString owed = QStringLiteral("Who owes you");
    const QString owe = QStringLiteral("What you owe");
    const QString sales = QStringLiteral("Sales and customers");
    const QString expenses = QStringLiteral("Expenses and vendors");
    const QString accountant = QStringLiteral("For my accountant");
    return {
        {QStringLiteral("profit_and_loss"), QStringLiteral("Profit and Loss"), overview,
         QStringLiteral("Income minus cost of goods sold and expenses for a period: whether the business made money.")},
        {QStringLiteral("balance_sheet"), QStringLiteral("Balance Sheet"), overview,
         QStringLiteral("What the business owns (assets), owes (liabilities) and the owners' stake (equity) on one date. "
                        "Assets always equal liabilities plus equity.")},
        {QStringLiteral("cash_flow"), QStringLiteral("Statement of Cash Flows"), overview,
         QStringLiteral("How net income became a change in cash: operating, investing and financing activity "
                        "(indirect method).")},
        {QStringLiteral("budget_vs_actual"), QStringLiteral("Budget vs. Actual"), overview,
         QStringLiteral("Income and expenses for a period against the monthly budget.")},
        {QStringLiteral("ar_aging_summary"), QStringLiteral("A/R Aging Summary"), owed,
         QStringLiteral("Unpaid customer balances by days past due: current, 1–30, 31–60, 61–90, over 90.")},
        {QStringLiteral("ar_aging_detail"), QStringLiteral("A/R Aging Detail"), owed,
         QStringLiteral("Every open invoice and credit, grouped by days past due.")},
        {QStringLiteral("customer_balances"), QStringLiteral("Customer Balances"), owed,
         QStringLiteral("What each customer owes as of a date.")},
        {QStringLiteral("open_invoices"), QStringLiteral("Open Invoices"), owed,
         QStringLiteral("Invoices with an unpaid balance, with due dates.")},
        {QStringLiteral("ap_aging_summary"), QStringLiteral("A/P Aging Summary"), owe,
         QStringLiteral("Unpaid bills by days past due.")},
        {QStringLiteral("ap_aging_detail"), QStringLiteral("A/P Aging Detail"), owe,
         QStringLiteral("Every open bill and vendor credit, grouped by days past due.")},
        {QStringLiteral("vendor_balances"), QStringLiteral("Vendor Balances"), owe,
         QStringLiteral("What the business owes each vendor as of a date.")},
        {QStringLiteral("unpaid_bills"), QStringLiteral("Unpaid Bills"), owe,
         QStringLiteral("Bills with an unpaid balance, with due dates.")},
        {QStringLiteral("sales_by_customer"), QStringLiteral("Sales by Customer"), sales,
         QStringLiteral("Income from each customer in a period, net of discounts and credits.")},
        {QStringLiteral("sales_by_item"), QStringLiteral("Sales by Product/Service"), sales,
         QStringLiteral("Income by product or service in a period.")},
        {QStringLiteral("sales_tax_liability"), QStringLiteral("Sales Tax Liability"), sales,
         QStringLiteral("Taxable sales, tax collected and paid per agency and rate, and what is still owed.")},
        {QStringLiteral("expenses_by_vendor"), QStringLiteral("Expenses by Vendor"), expenses,
         QStringLiteral("Spending with each vendor in a period.")},
        {QStringLiteral("expenses_by_category"), QStringLiteral("Expenses by Category"), expenses,
         QStringLiteral("Spending per expense account in a period, with its share of the total.")},
        {QStringLiteral("trial_balance"), QStringLiteral("Trial Balance"), accountant,
         QStringLiteral("Every account's debit or credit balance on a date; total debits equal total credits.")},
        {QStringLiteral("general_ledger"), QStringLiteral("General Ledger"), accountant,
         QStringLiteral("Every posting to every account in a period with running balances.")},
        {QStringLiteral("journal"), QStringLiteral("Journal"), accountant,
         QStringLiteral("Every transaction in a period with its debit and credit lines.")},
        {QStringLiteral("transaction_list"), QStringLiteral("Transaction List by Date"), accountant,
         QStringLiteral("One line per transaction in a period.")},
        {QStringLiteral("reconciliation"), QStringLiteral("Reconciliation Report"), accountant,
         QStringLiteral("A finished or in-progress bank reconciliation: cleared and uncleared items.")},
    };
}

} // namespace QindaOffice::Books
