// SPDX-License-Identifier: GPL-3.0-or-later
// Account-type metadata: normal balance side and report section per type
// (docs/books.md "Chart of accounts").
#include "accounts.h"

namespace QindaOffice::Books {

AccountTypeInfo accountTypeInfo(AccountType type)
{
    using C = AccountClass;
    using N = NormalBalance;
    using S = ReportSection;
    switch (type) {
    case AccountType::Bank:
        return {type, QStringLiteral("bank"), QStringLiteral("Bank"), C::Asset, N::Debit, S::CurrentAssets};
    case AccountType::AccountsReceivable:
        return {type, QStringLiteral("accounts_receivable"), QStringLiteral("Accounts receivable (A/R)"),
                C::Asset, N::Debit, S::CurrentAssets};
    case AccountType::OtherCurrentAsset:
        return {type, QStringLiteral("other_current_asset"), QStringLiteral("Other current asset"),
                C::Asset, N::Debit, S::CurrentAssets};
    case AccountType::FixedAsset:
        return {type, QStringLiteral("fixed_asset"), QStringLiteral("Fixed asset"), C::Asset, N::Debit,
                S::FixedAssets};
    case AccountType::OtherAsset:
        return {type, QStringLiteral("other_asset"), QStringLiteral("Other asset"), C::Asset, N::Debit,
                S::OtherAssets};
    case AccountType::AccountsPayable:
        return {type, QStringLiteral("accounts_payable"), QStringLiteral("Accounts payable (A/P)"),
                C::Liability, N::Credit, S::CurrentLiabilities};
    case AccountType::CreditCard:
        return {type, QStringLiteral("credit_card"), QStringLiteral("Credit card"), C::Liability,
                N::Credit, S::CurrentLiabilities};
    case AccountType::OtherCurrentLiability:
        return {type, QStringLiteral("other_current_liability"),
                QStringLiteral("Other current liability"), C::Liability, N::Credit,
                S::CurrentLiabilities};
    case AccountType::LongTermLiability:
        return {type, QStringLiteral("long_term_liability"), QStringLiteral("Long-term liability"),
                C::Liability, N::Credit, S::LongTermLiabilities};
    case AccountType::Equity:
        return {type, QStringLiteral("equity"), QStringLiteral("Equity"), C::Equity, N::Credit, S::Equity};
    case AccountType::Income:
        return {type, QStringLiteral("income"), QStringLiteral("Income"), C::Income, N::Credit, S::Income};
    case AccountType::OtherIncome:
        return {type, QStringLiteral("other_income"), QStringLiteral("Other income"), C::Income,
                N::Credit, S::OtherIncome};
    case AccountType::CostOfGoodsSold:
        return {type, QStringLiteral("cost_of_goods_sold"), QStringLiteral("Cost of goods sold"),
                C::Expense, N::Debit, S::CostOfGoodsSold};
    case AccountType::Expense:
        return {type, QStringLiteral("expense"), QStringLiteral("Expense"), C::Expense, N::Debit,
                S::Expenses};
    case AccountType::OtherExpense:
        return {type, QStringLiteral("other_expense"), QStringLiteral("Other expense"), C::Expense,
                N::Debit, S::OtherExpenses};
    }
    return {AccountType::Expense, QStringLiteral("expense"), QStringLiteral("Expense"), C::Expense,
            N::Debit, S::Expenses};
}

QList<AccountType> allAccountTypes()
{
    QList<AccountType> types;
    for (int v = int(AccountType::Bank); v <= int(AccountType::OtherExpense); ++v) {
        types << AccountType(v);
    }
    return types;
}

std::optional<AccountType> accountTypeFromKey(const QString &key)
{
    const QString k = key.trimmed().toLower();
    for (AccountType type : allAccountTypes()) {
        const AccountTypeInfo info = accountTypeInfo(type);
        if (info.key == k || info.label.toLower() == k) {
            return type;
        }
    }
    return std::nullopt;
}

QString reportSectionLabel(ReportSection section)
{
    switch (section) {
    case ReportSection::CurrentAssets: return QStringLiteral("Current Assets");
    case ReportSection::FixedAssets: return QStringLiteral("Fixed Assets");
    case ReportSection::OtherAssets: return QStringLiteral("Other Assets");
    case ReportSection::CurrentLiabilities: return QStringLiteral("Current Liabilities");
    case ReportSection::LongTermLiabilities: return QStringLiteral("Long-Term Liabilities");
    case ReportSection::Equity: return QStringLiteral("Equity");
    case ReportSection::Income: return QStringLiteral("Income");
    case ReportSection::CostOfGoodsSold: return QStringLiteral("Cost of Goods Sold");
    case ReportSection::Expenses: return QStringLiteral("Expenses");
    case ReportSection::OtherIncome: return QStringLiteral("Other Income");
    case ReportSection::OtherExpenses: return QStringLiteral("Other Expenses");
    }
    return QString();
}

namespace SystemRole {
QList<RoleSpec> all()
{
    return {
        {AccountsReceivable, AccountType::AccountsReceivable, QStringLiteral("Accounts Receivable")},
        {AccountsPayable, AccountType::AccountsPayable, QStringLiteral("Accounts Payable")},
        {UndepositedFunds, AccountType::OtherCurrentAsset, QStringLiteral("Undeposited Funds")},
        {SalesTaxPayable, AccountType::OtherCurrentLiability, QStringLiteral("Sales Tax Payable")},
        {RetainedEarnings, AccountType::Equity, QStringLiteral("Retained Earnings")},
        {OpeningBalanceEquity, AccountType::Equity, QStringLiteral("Opening Balance Equity")},
        {SalesDiscounts, AccountType::Income, QStringLiteral("Discounts Given")},
    };
}
} // namespace SystemRole

} // namespace QindaOffice::Books
