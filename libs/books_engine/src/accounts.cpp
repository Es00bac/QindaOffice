// SPDX-License-Identifier: GPL-3.0-or-later
#include "accounts.h"

#include "company_file.h"
#include "documents.h"
#include "journal.h"
#include "sql_util.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

#include <algorithm>
#include <functional>

namespace QindaOffice::Books {

namespace {

Account readAccount(const QSqlQuery &q)
{
    Account a;
    a.id = q.value(0).toLongLong();
    a.number = q.value(1).toString();
    a.name = q.value(2).toString();
    a.type = AccountType(q.value(3).toInt());
    a.parentId = q.value(4).toLongLong();
    a.systemRole = q.value(5).toString();
    a.functionalGroup = q.value(6).toString();
    a.description = q.value(7).toString();
    a.active = q.value(8).toBool();
    return a;
}

const QString kSelect = QStringLiteral(
    "SELECT id, number, name, type, parent_id, system_role, functional_group, description, active"
    " FROM accounts");

QString accountJson(const Account &a)
{
    QJsonObject o{{QStringLiteral("id"), a.id},
                  {QStringLiteral("number"), a.number},
                  {QStringLiteral("name"), a.name},
                  {QStringLiteral("type"), accountTypeInfo(a.type).key},
                  {QStringLiteral("parent"), a.parentId},
                  {QStringLiteral("role"), a.systemRole},
                  {QStringLiteral("group"), a.functionalGroup},
                  {QStringLiteral("active"), a.active}};
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

} // namespace

ChartOfAccounts::ChartOfAccounts(CompanyFile &file)
    : m_file(file)
{
}

QList<Account> ChartOfAccounts::accounts(bool includeInactive) const
{
    QList<Account> all;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, kSelect);
        while (q.next()) {
            all << readAccount(q);
        }
    }
    QHash<qint64, QList<qsizetype>> children;
    QHash<qint64, qsizetype> index;
    for (qsizetype i = 0; i < all.size(); ++i) {
        index.insert(all[i].id, i);
    }
    QList<qsizetype> roots;
    for (qsizetype i = 0; i < all.size(); ++i) {
        if (all[i].parentId != 0 && index.contains(all[i].parentId)) {
            children[all[i].parentId] << i;
        } else {
            roots << i;
        }
    }
    auto order = [&all](qsizetype a, qsizetype b) {
        const Account &x = all[a];
        const Account &y = all[b];
        if (x.type != y.type) {
            return int(x.type) < int(y.type);
        }
        const QString kx = x.number.isEmpty() ? QStringLiteral("~") + x.name : x.number;
        const QString ky = y.number.isEmpty() ? QStringLiteral("~") + y.name : y.number;
        return kx.localeAwareCompare(ky) < 0;
    };
    QList<Account> out;
    std::function<void(qsizetype, const QString &, int)> visit = [&](qsizetype i, const QString &prefix,
                                                                     int depth) {
        Account a = all[i];
        a.fullName = prefix.isEmpty() ? a.name : prefix + QLatin1Char(':') + a.name;
        a.depth = depth;
        if (includeInactive || a.active) {
            out << a;
        }
        QList<qsizetype> kids = children.value(a.id);
        std::sort(kids.begin(), kids.end(), order);
        for (qsizetype k : kids) {
            visit(k, a.fullName, depth + 1);
        }
    };
    std::sort(roots.begin(), roots.end(), order);
    for (qsizetype r : roots) {
        visit(r, QString(), 0);
    }
    return out;
}

QList<Account> ChartOfAccounts::accountsOfType(AccountType type, bool includeInactive) const
{
    QList<Account> out;
    for (const Account &a : accounts(includeInactive)) {
        if (a.type == type) {
            out << a;
        }
    }
    return out;
}

std::optional<Account> ChartOfAccounts::account(qint64 id) const
{
    for (const Account &a : accounts(true)) {
        if (a.id == id) {
            return a;
        }
    }
    return std::nullopt;
}

qint64 ChartOfAccounts::idForRole(const QString &role) const
{
    return Sql::scalarInt(m_file.database(), QStringLiteral("SELECT id FROM accounts WHERE system_role = ?"),
                          {role});
}

qint64 ChartOfAccounts::find(const QString &nameOrNumber) const
{
    const QString key = nameOrNumber.trimmed();
    if (key.isEmpty()) {
        return 0;
    }
    qint64 leafMatch = 0;
    for (const Account &a : accounts(true)) {
        if (a.fullName.compare(key, Qt::CaseInsensitive) == 0 || (!a.number.isEmpty() && a.number == key)) {
            return a.id;
        }
        if (leafMatch == 0 && a.name.compare(key, Qt::CaseInsensitive) == 0) {
            leafMatch = a.id;
        }
    }
    return leafMatch;
}

bool ChartOfAccounts::hasPostings(qint64 id) const
{
    return Sql::scalarInt(m_file.database(),
                          QStringLiteral("SELECT EXISTS(SELECT 1 FROM journal_lines WHERE account_id = ?)"), {id})
        != 0;
}

Result ChartOfAccounts::create(const Account &account)
{
    if (account.name.trimmed().isEmpty() || account.name.contains(QLatin1Char(':'))) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("an account needs a name without ':'"));
    }
    const QList<Account> existing = accounts(true);
    const AccountClass cls = accountTypeInfo(account.type).accountClass;
    QString parentName;
    for (const Account &a : existing) {
        if (a.id == account.parentId) {
            if (accountTypeInfo(a.type).accountClass != cls) {
                return Result::failure(ErrorCode::Invalid,
                                       QStringLiteral("a sub-account must be in the same class as its parent"));
            }
            parentName = a.fullName;
        }
        if (!account.number.isEmpty() && a.number == account.number) {
            return Result::failure(ErrorCode::Duplicate,
                                   QStringLiteral("account number %1 is already used").arg(account.number));
        }
        if (!account.systemRole.isEmpty() && a.systemRole == account.systemRole) {
            return Result::failure(ErrorCode::Duplicate,
                                   QStringLiteral("the %1 role already has an account").arg(account.systemRole));
        }
    }
    if (account.parentId != 0 && parentName.isEmpty()) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("unknown parent account"));
    }
    const QString full = parentName.isEmpty() ? account.name.trimmed()
                                              : parentName + QLatin1Char(':') + account.name.trimmed();
    for (const Account &a : existing) {
        if (a.fullName.compare(full, Qt::CaseInsensitive) == 0) {
            return Result::failure(ErrorCode::Duplicate, QStringLiteral("an account named %1 exists").arg(full));
        }
    }
    SqlTransaction txn(m_file);
    QString error;
    const qint64 id = Sql::insert(
        m_file.database(),
        QStringLiteral("INSERT INTO accounts(number, name, type, parent_id, system_role, functional_group,"
                       " description, active, created_at) VALUES(?,?,?,?,?,?,?,?,?)"),
        {account.number.isEmpty() ? QVariant() : QVariant(account.number), account.name.trimmed(),
         int(account.type), Sql::idOrNull(account.parentId),
         account.systemRole.isEmpty() ? QVariant() : QVariant(account.systemRole),
         account.functionalGroup.isEmpty() ? QVariant() : QVariant(account.functionalGroup),
         account.description, account.active ? 1 : 0, CompanyFile::nowStamp()},
        &error);
    if (id == 0) {
        return Result::failure(ErrorCode::Storage, error);
    }
    Account created = account;
    created.id = id;
    m_file.audit(QStringLiteral("create"), QStringLiteral("account"), id, {}, accountJson(created));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(id);
}

Result ChartOfAccounts::update(const Account &account)
{
    const std::optional<Account> before = this->account(account.id);
    if (!before) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("unknown account"));
    }
    if (account.name.trimmed().isEmpty() || account.name.contains(QLatin1Char(':'))) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("an account needs a name without ':'"));
    }
    if (account.type != before->type && hasPostings(account.id)
        && accountTypeInfo(account.type).accountClass != accountTypeInfo(before->type).accountClass) {
        return Result::failure(ErrorCode::HasPostings,
                               QStringLiteral("an account with postings cannot move to another class"));
    }
    if (!before->systemRole.isEmpty() && account.type != before->type) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a system account keeps its type"));
    }
    if (account.parentId == account.id) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("an account cannot be its own parent"));
    }
    SqlTransaction txn(m_file);
    QString error;
    if (!Sql::exec(m_file.database(),
                   QStringLiteral("UPDATE accounts SET number = ?, name = ?, type = ?, parent_id = ?,"
                                  " functional_group = ?, description = ? WHERE id = ?"),
                   {account.number.isEmpty() ? QVariant() : QVariant(account.number), account.name.trimmed(),
                    int(account.type), Sql::idOrNull(account.parentId),
                    account.functionalGroup.isEmpty() ? QVariant() : QVariant(account.functionalGroup),
                    account.description, account.id},
                   &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    Account after = account;
    after.systemRole = before->systemRole;
    m_file.audit(QStringLiteral("update"), QStringLiteral("account"), account.id, accountJson(*before),
                 accountJson(after));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(account.id);
}

Result ChartOfAccounts::setActive(qint64 id, bool active)
{
    const std::optional<Account> before = account(id);
    if (!before) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("unknown account"));
    }
    if (!active && !before->systemRole.isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("system accounts cannot be deactivated"));
    }
    if (!active && !balance(id).isZero()) {
        return Result::failure(ErrorCode::Invalid,
                               QStringLiteral("move the balance out of %1 before deactivating it").arg(before->name));
    }
    SqlTransaction txn(m_file);
    Sql::exec(m_file.database(), QStringLiteral("UPDATE accounts SET active = ? WHERE id = ?"), {active ? 1 : 0, id});
    m_file.audit(active ? QStringLiteral("activate") : QStringLiteral("deactivate"), QStringLiteral("account"), id,
                 accountJson(*before), {});
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(id);
}

Result ChartOfAccounts::remove(qint64 id)
{
    const std::optional<Account> before = account(id);
    if (!before) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("unknown account"));
    }
    if (!before->systemRole.isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("system accounts cannot be deleted"));
    }
    if (hasPostings(id)) {
        return Result::failure(ErrorCode::HasPostings,
                               QStringLiteral("%1 has transactions; deactivate it instead").arg(before->name));
    }
    const qint64 refs = Sql::scalarInt(
        m_file.database(),
        // AGENT-GUARD: Qt counts '?' itself; numbered ?1 placeholders
        // silently fail to bind. Repeat the value instead.
        QStringLiteral("SELECT (SELECT COUNT(*) FROM accounts WHERE parent_id = ?)"
                       " + (SELECT COUNT(*) FROM document_lines WHERE account_id = ?)"
                       " + (SELECT COUNT(*) FROM items WHERE income_account_id = ? OR expense_account_id = ?)"),
        {id, id, id, id});
    if (refs > 0) {
        return Result::failure(ErrorCode::HasPostings,
                               QStringLiteral("%1 is still used; deactivate it instead").arg(before->name));
    }
    SqlTransaction txn(m_file);
    QString error;
    if (!Sql::exec(m_file.database(), QStringLiteral("DELETE FROM accounts WHERE id = ?"), {id}, &error)) {
        return Result::failure(ErrorCode::HasPostings, error);
    }
    m_file.audit(QStringLiteral("delete"), QStringLiteral("account"), id, accountJson(*before), {});
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(id);
}

Result ChartOfAccounts::ensureSystemAccounts()
{
    for (const SystemRole::RoleSpec &spec : SystemRole::all()) {
        if (idForRole(spec.role) != 0) {
            continue;
        }
        Account a;
        a.name = spec.defaultName;
        a.type = spec.type;
        a.systemRole = spec.role;
        // A template may already use the default name for a plain account.
        if (find(a.name) != 0) {
            a.name += QStringLiteral(" (System)");
        }
        const Result created = create(a);
        if (!created.ok) {
            return created;
        }
    }
    return Result::success();
}

Money ChartOfAccounts::balance(qint64 accountId, const QDate &asOf) const
{
    const qint64 type = Sql::scalarInt(m_file.database(), QStringLiteral("SELECT type FROM accounts WHERE id = ?"),
                                       {accountId});
    const qint64 net = Sql::scalarInt(
        m_file.database(),
        QStringLiteral("SELECT COALESCE(SUM(debit) - SUM(credit), 0) FROM journal_lines"
                       " WHERE account_id = ? AND voided = 0 AND (? = '' OR date <= ?)"),
        {accountId, Sql::isoDate(asOf), Sql::isoDate(asOf)});
    const bool debitNormal = accountTypeInfo(AccountType(type)).normalBalance == NormalBalance::Debit;
    return m_file.money(debitNormal ? net : -net);
}

Result ChartOfAccounts::setOpeningBalance(qint64 accountId, const Money &amount, const QDate &date)
{
    const std::optional<Account> target = account(accountId);
    if (!target) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("unknown account"));
    }
    if (!accountTypeInfo(target->type).isBalanceSheet()) {
        return Result::failure(ErrorCode::Invalid,
                               QStringLiteral("opening balances belong to balance-sheet accounts"));
    }
    if (!amount.sameCurrency(m_file.money(0))) {
        return Result::failure(ErrorCode::CurrencyMismatch, QStringLiteral("amount is not in the company currency"));
    }
    const qint64 equity = idForRole(SystemRole::OpeningBalanceEquity);
    const bool debitNormal = accountTypeInfo(target->type).normalBalance == NormalBalance::Debit;
    // A positive amount increases the account on its normal side.
    const bool debitTarget = debitNormal == !amount.isNegative();
    const qint64 value = amount.abs().minor();
    QList<PostingLine> lines{
        PostingLine{accountId, debitTarget ? value : 0, debitTarget ? 0 : value, 0, 0, 0, 0, 0,
                    QStringLiteral("Opening balance")},
        PostingLine{equity, debitTarget ? 0 : value, debitTarget ? value : 0, 0, 0, 0, 0, 0,
                    QStringLiteral("Opening balance")},
    };
    SqlTransaction txn(m_file);
    BusinessDocument doc;
    doc.kind = DocKind::OpeningBalance;
    doc.date = date;
    doc.memo = QStringLiteral("Opening balance: %1").arg(target->fullName);
    doc.total = amount.abs();
    doc.subtotal = amount.abs();
    Documents documents(m_file);
    if (const Result written = documents.write(doc); !written.ok) {
        return written;
    }
    const Result posted = Ledger(m_file).postLines(doc.id, doc.kind, date, {}, doc.memo, lines, {});
    if (!posted.ok) {
        return posted;
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(doc.id);
}

} // namespace QindaOffice::Books
