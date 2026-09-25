// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The chart of accounts: typed accounts with optional numbers and
// sub-accounts, system roles the engine posts to by itself, and the
// account-type metadata every report is laid out from.
//
// AGENT-CONTRACT (storage): AccountType values are persisted in
// accounts.type; the role strings in SystemRole are persisted in
// accounts.system_role and named by the chart templates (resources/
// templates/*.json). Never renumber or rename either.

#include "books_types.h"
#include "money.h"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

enum class AccountType {
    Bank = 1,
    AccountsReceivable = 2,
    OtherCurrentAsset = 3,
    FixedAsset = 4,
    OtherAsset = 5,
    AccountsPayable = 6,
    CreditCard = 7,
    OtherCurrentLiability = 8,
    LongTermLiability = 9,
    Equity = 10,
    Income = 11,
    OtherIncome = 12,
    CostOfGoodsSold = 13,
    Expense = 14,
    OtherExpense = 15,
};

enum class AccountClass { Asset, Liability, Equity, Income, Expense };
enum class NormalBalance { Debit, Credit };
// Where the account lands on the Balance Sheet / Profit and Loss.
enum class ReportSection {
    CurrentAssets,
    FixedAssets,
    OtherAssets,
    CurrentLiabilities,
    LongTermLiabilities,
    Equity,
    Income,
    CostOfGoodsSold,
    Expenses,
    OtherIncome,
    OtherExpenses,
};

struct AccountTypeInfo {
    AccountType type;
    QString key;   // "bank", "accounts_receivable", … (template/CSV/IIF key)
    QString label; // "Bank", "Accounts receivable (A/R)", …
    AccountClass accountClass;
    NormalBalance normalBalance;
    ReportSection section;
    [[nodiscard]] bool isBalanceSheet() const
    {
        return accountClass == AccountClass::Asset || accountClass == AccountClass::Liability
            || accountClass == AccountClass::Equity;
    }
};

[[nodiscard]] AccountTypeInfo accountTypeInfo(AccountType type);
[[nodiscard]] std::optional<AccountType> accountTypeFromKey(const QString &key);
[[nodiscard]] QList<AccountType> allAccountTypes();
[[nodiscard]] QString reportSectionLabel(ReportSection section);

namespace SystemRole {
inline const QString AccountsReceivable = QStringLiteral("accounts_receivable");
inline const QString AccountsPayable = QStringLiteral("accounts_payable");
inline const QString UndepositedFunds = QStringLiteral("undeposited_funds");
inline const QString SalesTaxPayable = QStringLiteral("sales_tax_payable");
inline const QString RetainedEarnings = QStringLiteral("retained_earnings");
inline const QString OpeningBalanceEquity = QStringLiteral("opening_balance_equity");
inline const QString SalesDiscounts = QStringLiteral("sales_discounts");
// Every role and the type/name an account created for it gets.
struct RoleSpec {
    QString role;
    AccountType type;
    QString defaultName;
};
[[nodiscard]] QList<RoleSpec> all();
} // namespace SystemRole

struct Account {
    qint64 id = 0;
    QString number;          // optional ("1000")
    QString name;            // leaf name ("Checking")
    AccountType type = AccountType::Expense;
    qint64 parentId = 0;     // sub-account of (same class)
    QString systemRole;      // SystemRole::* or empty
    QString functionalGroup; // nonprofit: "program" | "management" | "fundraising"
    QString description;
    bool active = true;
    // Filled on read:
    QString fullName;        // "Parent:Child" (QuickBooks convention)
    int depth = 0;
};

class ChartOfAccounts {
public:
    explicit ChartOfAccounts(CompanyFile &file);

    // Validates: non-empty name, unique full name, unique number, parent
    // exists and has the same AccountClass, role not already taken.
    Result create(const Account &account);
    // Type may change only within the same class once the account has
    // postings; a system account keeps its role.
    Result update(const Account &account);
    // System accounts cannot be deactivated; an account with a non-zero
    // balance cannot either (move the balance first).
    Result setActive(qint64 id, bool active);
    // Refused with HasPostings when anything ever posted to it (void
    // included — the audit trail references it); deactivate instead.
    Result remove(qint64 id);

    [[nodiscard]] std::optional<Account> account(qint64 id) const;
    // Parents before children; within a level by type order, number, name.
    [[nodiscard]] QList<Account> accounts(bool includeInactive = false) const;
    [[nodiscard]] QList<Account> accountsOfType(AccountType type, bool includeInactive = false) const;
    [[nodiscard]] qint64 idForRole(const QString &role) const;
    // By full name ("Expenses:Rent"), leaf name, or number.
    [[nodiscard]] qint64 find(const QString &nameOrNumber) const;
    [[nodiscard]] bool hasPostings(qint64 id) const;
    // Creates any SystemRole account the chart lacks.
    Result ensureSystemAccounts();

    // Balance in the account's NORMAL sign (a bank's debit balance and a
    // loan's credit balance are both positive), accrual, as of `asOf`
    // inclusive (invalid = everything).
    [[nodiscard]] Money balance(qint64 accountId, const QDate &asOf = {}) const;
    // Posts an OpeningBalance document against Opening Balance Equity.
    Result setOpeningBalance(qint64 accountId, const Money &amount, const QDate &date);

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
