// SPDX-License-Identifier: GPL-3.0-or-later
// Sales by customer / product, expenses by vendor / category, sales tax
// liability.
#include "reports.h"

#include "company_file.h"
#include "documents.h"
#include "ledger_query.h"
#include "lists.h"
#include "reports_util.h"
#include "sql_util.h"
#include "tax.h"

#include <QMap>

namespace QindaOffice::Books {

using namespace ReportUtil;

namespace {

bool isSalesAccount(AccountType t)
{
    return t == AccountType::Income;
}

bool isExpenseAccount(AccountType t)
{
    return t == AccountType::CostOfGoodsSold || t == AccountType::Expense || t == AccountType::OtherExpense;
}

// label → (key id, amount), then rows sorted by label with a share column.
void appendRanked(ReportTable &t, const QMap<QString, QPair<qint64, qint64>> &values, const QString &keyPrefix,
                  const QDate &from, const QDate &to, bool party)
{
    qint64 total = 0;
    for (const auto &v : values) {
        total += v.second;
    }
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (it.value().second == 0) {
            continue;
        }
        ReportRow r = moneyRow(RowKind::Account, keyPrefix + QString::number(it.value().first), it.key(),
                               {it.value().second});
        r.cells << ReportCell::ofPercent(percentOf(it.value().second, total));
        r.drill.from = from;
        r.drill.to = to;
        (party ? r.drill.partyId : r.drill.itemId) = it.value().first;
        t.rows << r;
    }
    ReportRow sum = moneyRow(RowKind::Total, QStringLiteral("total"), QStringLiteral("Total"), {total});
    sum.cells << ReportCell::ofPercent(total == 0 ? 0 : 1000);
    t.rows << sum;
}

} // namespace

ReportTable Reports::byParty(const QDate &from, const QDate &to, Basis basis, bool sales) const
{
    ReportTable t = makeTable(m_file, sales ? QStringLiteral("sales_by_customer") : QStringLiteral("expenses_by_vendor"),
                              sales ? QStringLiteral("Sales by Customer") : QStringLiteral("Expenses by Vendor"),
                              periodText(from, to) + QStringLiteral(" · ") + basisText(basis));
    t.columns = {{QStringLiteral("label"), sales ? QStringLiteral("Customer") : QStringLiteral("Vendor"), ColumnType::Text},
                 {QStringLiteral("total"), QStringLiteral("Total"), ColumnType::Money},
                 {QStringLiteral("share"), QStringLiteral("% of Total"), ColumnType::Percent}};
    QHash<qint64, AccountType> types;
    for (const Account &a : ChartOfAccounts(m_file).accounts(true)) {
        types.insert(a.id, a.type);
    }
    Lists lists(m_file);
    QMap<QString, QPair<qint64, qint64>> values;
    for (const LedgerSum &s : LedgerQuery(m_file).aggregate({from, to, basis, {}}, GroupAccount | GroupParty)) {
        const AccountType type = types.value(s.accountId);
        if (sales ? !isSalesAccount(type) : !isExpenseAccount(type)) {
            continue;
        }
        const std::optional<Party> p = lists.party(s.partyId);
        const QString name = p ? p->displayName : (sales ? QStringLiteral("(No customer)") : QStringLiteral("(No vendor)"));
        auto &entry = values[name];
        entry.first = s.partyId;
        entry.second += sales ? -s.net() : s.net();
    }
    appendRanked(t, values, QStringLiteral("party:"), from, to, true);
    return t;
}

ReportTable Reports::salesByCustomer(const QDate &from, const QDate &to, std::optional<Basis> basis) const
{
    return byParty(from, to, basisOr(basis), true);
}

ReportTable Reports::expensesByVendor(const QDate &from, const QDate &to, std::optional<Basis> basis) const
{
    return byParty(from, to, basisOr(basis), false);
}

ReportTable Reports::salesByItem(const QDate &from, const QDate &to, std::optional<Basis> basisOpt) const
{
    const Basis basis = basisOr(basisOpt);
    ReportTable t = makeTable(m_file, QStringLiteral("sales_by_item"), QStringLiteral("Sales by Product/Service"),
                              periodText(from, to) + QStringLiteral(" · ") + basisText(basis));
    t.columns = {{QStringLiteral("label"), QStringLiteral("Product/Service"), ColumnType::Text},
                 {QStringLiteral("total"), QStringLiteral("Total"), ColumnType::Money},
                 {QStringLiteral("share"), QStringLiteral("% of Total"), ColumnType::Percent}};
    QHash<qint64, AccountType> types;
    for (const Account &a : ChartOfAccounts(m_file).accounts(true)) {
        types.insert(a.id, a.type);
    }
    Lists lists(m_file);
    QMap<QString, QPair<qint64, qint64>> values;
    for (const LedgerSum &s : LedgerQuery(m_file).aggregate({from, to, basis, {}}, GroupAccount | GroupItem)) {
        if (!isSalesAccount(types.value(s.accountId))) {
            continue;
        }
        const std::optional<Item> item = lists.item(s.itemId);
        const QString name = item ? item->name : QStringLiteral("(Discounts and other income)");
        auto &entry = values[name];
        entry.first = s.itemId;
        entry.second -= s.net();
    }
    appendRanked(t, values, QStringLiteral("item:"), from, to, false);
    return t;
}

ReportTable Reports::expensesByCategory(const QDate &from, const QDate &to, std::optional<Basis> basisOpt) const
{
    const Basis basis = basisOr(basisOpt);
    ReportTable t = makeTable(m_file, QStringLiteral("expenses_by_category"), QStringLiteral("Expenses by Category"),
                              periodText(from, to) + QStringLiteral(" · ") + basisText(basis));
    const QList<ValueColumn> columns{
        {QStringLiteral("total"), QStringLiteral("Total"), LedgerQuery(m_file).netByAccount({from, to, basis, {}}), from, to}};
    setColumns(t, QStringLiteral("Category"), columns);
    const QList<qint64> total =
        appendSection(t, ChartOfAccounts(m_file).accounts(true),
                      {AccountType::CostOfGoodsSold, AccountType::Expense, AccountType::OtherExpense},
                      QStringLiteral("expenses"), QStringLiteral("Expenses"), columns, 1);
    t.columns << ReportColumn{QStringLiteral("share"), QStringLiteral("% of Expenses"), ColumnType::Percent};
    for (ReportRow &r : t.rows) {
        const bool money = r.cells.size() > 1 && !r.cells[1].empty;
        r.cells << (money ? ReportCell::ofPercent(percentOf(r.cells[1].value, total.value(0))) : ReportCell::blank());
    }
    return t;
}

ReportTable Reports::salesTaxLiability(const QDate &from, const QDate &to) const
{
    ReportTable t = makeTable(m_file, QStringLiteral("sales_tax_liability"), QStringLiteral("Sales Tax Liability"),
                              periodText(from, to) + QStringLiteral(" · Accrual basis"));
    t.columns = {{QStringLiteral("label"), QStringLiteral("Agency / Rate"), ColumnType::Text},
                 {QStringLiteral("taxable"), QStringLiteral("Taxable Sales"), ColumnType::Money},
                 {QStringLiteral("rate"), QStringLiteral("Rate"), ColumnType::Percent},
                 {QStringLiteral("collected"), QStringLiteral("Tax Collected"), ColumnType::Money},
                 {QStringLiteral("paid"), QStringLiteral("Tax Paid"), ColumnType::Money},
                 {QStringLiteral("balance"), QStringLiteral("Balance Due"), ColumnType::Money}};
    // Taxable base per component, from the live sales documents of the period.
    QHash<qint64, qint64> taxable;
    Documents documents(m_file);
    DocumentFilter filter;
    filter.kinds = {DocKind::Invoice, DocKind::SalesReceipt, DocKind::CreditMemo};
    filter.from = from;
    filter.to = to;
    filter.includeDrafts = false;
    for (const BusinessDocument &doc : documents.list(filter)) {
        if (doc.taxRateId == 0) {
            continue;
        }
        const int sign = doc.kind == DocKind::CreditMemo ? -1 : 1;
        const DocumentTotals totals = documents.computeTotals(doc);
        for (const TaxShare &share : totals.taxes) {
            taxable[share.componentId] += sign * totals.taxableBase.minor();
        }
    }
    const QSqlDatabase db = m_file.database();
    const QString payable = QStringLiteral(" AND account_id = (SELECT id FROM accounts WHERE system_role = 'sales_tax_payable')");
    SalesTax tax(m_file);
    QList<qint64> grand(5, 0);
    for (const TaxAgency &agency : tax.agencies()) {
        t.rows << sectionRow(QStringLiteral("section:agency:%1").arg(agency.id), agency.name, int(t.columns.size()));
        qint64 agencyTaxable = 0;
        qint64 agencyCollected = 0;
        for (const TaxRate &rate : tax.rates()) {
            for (const TaxComponent &c : rate.components) {
                if (c.agencyId != agency.id) {
                    continue;
                }
                const qint64 collected = Sql::scalarInt(
                    db,
                    QStringLiteral("SELECT COALESCE(SUM(credit) - SUM(debit), 0) FROM journal_lines WHERE voided = 0"
                                   " AND tax_component_id = ? AND date >= ? AND date <= ?")
                        + payable,
                    {c.id, Sql::isoDate(from), Sql::isoDate(to)});
                const QString key = QStringLiteral("component:%1").arg(c.id);
                if (t.row(key) != nullptr) {
                    continue; // a component shared by several combined rates
                }
                ReportRow r;
                r.kind = RowKind::Account;
                r.depth = 1;
                r.key = key;
                const Decimal pct(c.rate.mantissa(), c.rate.scale());
                // Percent cell: rate × 100 with the rate's own decimals.
                r.cells = {ReportCell::ofText(c.name), ReportCell::ofMoney(taxable.value(c.id)),
                           ReportCell::ofPercent(pct.mantissa(), qMax(0, pct.scale() - 2)),
                           ReportCell::ofMoney(collected), ReportCell::blank(), ReportCell::blank()};
                if (pct.scale() < 2) {
                    r.cells[2] = ReportCell::ofPercent(pct.mantissa() * pow10i(2 - pct.scale()), 0);
                }
                t.rows << r;
                agencyTaxable += taxable.value(c.id);
                agencyCollected += collected;
            }
        }
        const qint64 paid = Sql::scalarInt(
            db,
            QStringLiteral("SELECT COALESCE(SUM(debit), 0) FROM journal_lines WHERE voided = 0 AND kind = ?"
                           " AND tax_agency_id = ? AND date >= ? AND date <= ?")
                + payable,
            {int(DocKind::SalesTaxPayment), agency.id, Sql::isoDate(from), Sql::isoDate(to)});
        const qint64 balance = tax.liability(agency.id, to).minor();
        ReportRow total;
        total.kind = RowKind::Subtotal;
        total.key = QStringLiteral("total:agency:%1").arg(agency.id);
        total.cells = {ReportCell::ofText(QStringLiteral("Total ") + agency.name), ReportCell::ofMoney(agencyTaxable),
                       ReportCell::blank(), ReportCell::ofMoney(agencyCollected), ReportCell::ofMoney(paid),
                       ReportCell::ofMoney(balance)};
        t.rows << total;
        grand = add(grand, {agencyTaxable, 0, agencyCollected, paid, balance});
    }
    ReportRow total;
    total.kind = RowKind::Total;
    total.key = QStringLiteral("total");
    total.cells = {ReportCell::ofText(QStringLiteral("Total")), ReportCell::ofMoney(grand[0]), ReportCell::blank(),
                   ReportCell::ofMoney(grand[2]), ReportCell::ofMoney(grand[3]), ReportCell::ofMoney(grand[4])};
    t.rows << total;
    return t;
}

} // namespace QindaOffice::Books
