// SPDX-License-Identifier: GPL-3.0-or-later
// IIF import, transaction half (iif_import.h): check every TRNS block, skip
// what the books already hold, post the rest — as the matching document
// when its posting reproduces the rows, else as a journal entry — and run
// the whole import as one batch that a dry run rolls back.
#include "iif_import_tx_p.h"

#include "accounts.h"
#include "bank_import.h"
#include "company_file.h"
#include "journal.h"
#include "sql_util.h"

#include <QSet>
#include <QSqlQuery>

#include <algorithm>

namespace QindaOffice::Books::IifInternal {

namespace {

enum class Side { Customer, Vendor, Payee, Depositor, Journal };

Side sideOf(const QString &t)
{
    if (t == QLatin1String("INVOICE") || t == QLatin1String("CREDITMEMO") || t == QLatin1String("CASHSALE")
        || t == QLatin1String("PAYMENT") || t == QLatin1String("CASHREFUND") || t == QLatin1String("STMTCHG")) {
        return Side::Customer;
    }
    if (t == QLatin1String("BILL") || t.startsWith(QLatin1String("BILLPMT")) || t == QLatin1String("BILLPAYMENT")
        || t == QLatin1String("BILLREFUND") || t == QLatin1String("ITEMRECEIPT")) {
        return Side::Vendor;
    }
    if (t == QLatin1String("CHECK") || t.startsWith(QLatin1String("CREDITCARD")) || t.startsWith(QLatin1String("CCARD"))) {
        return Side::Payee;
    }
    return t == QLatin1String("DEPOSIT") ? Side::Depositor : Side::Journal;
}

// Within a day: what creates balances, then what settles them, then
// deposits of what was received — so applications find their targets.
int priorityOf(const QString &t)
{
    if (t == QLatin1String("PAYMENT") || t.startsWith(QLatin1String("BILLPMT")) || t == QLatin1String("BILLPAYMENT")) {
        return 1;
    }
    return t == QLatin1String("DEPOSIT") ? 2 : 0;
}

TxPlan plan(Context &ctx, const IifTransaction &t, int order, QHash<QString, QList<int>> *unknown)
{
    TxPlan p;
    p.order = order;
    p.typeText = t.trns.value(QStringLiteral("TRNSTYPE"));
    p.type = IifReader::normalizedType(p.typeText);
    p.firstLine = t.firstLine;
    p.lastLine = t.lastLine;
    const QString where = QStringLiteral("lines %1–%2: ").arg(t.firstLine).arg(t.lastLine);
    if (!t.error.isEmpty()) {
        p.refusal = where + t.error;
        return p;
    }
    p.date = IifReader::parseDate(t.trns.value(QStringLiteral("DATE")));
    p.due = IifReader::parseDate(t.trns.value(QStringLiteral("DUEDATE")));
    p.number = t.trns.value(QStringLiteral("DOCNUM"));
    p.memo = t.trns.value(QStringLiteral("MEMO"));
    if (!p.date.isValid()) {
        p.refusal = QStringLiteral("line %1: \"%2\" is not a date (MM/DD/YYYY)")
                        .arg(t.trns.line)
                        .arg(t.trns.value(QStringLiteral("DATE")));
        return p;
    }
    if (t.splits.isEmpty()) {
        p.refusal = where + QStringLiteral("a transaction needs at least one SPL row");
        return p;
    }
    QList<IifRecord> records{t.trns};
    records += t.splits;
    qint64 sum = 0;
    qint64 debits = 0;
    QStringList missing;
    for (const IifRecord &r : std::as_const(records)) {
        TxRow row;
        row.line = r.line;
        row.accountName = r.value(QStringLiteral("ACCNT"));
        row.accountId = ctx.account(row.accountName);
        const QString amount = r.value(QStringLiteral("AMOUNT"));
        const std::optional<qint64> minor =
            amount.isEmpty() ? std::optional<qint64>(0) : BankImport::parseAmount(amount, ctx.exponent);
        if (!minor) {
            p.refusal = QStringLiteral("line %1: \"%2\" is not an amount in %3").arg(r.line).arg(amount, ctx.file.currency());
            return p;
        }
        row.amount = *minor;
        row.name = r.value(QStringLiteral("NAME"));
        row.memo = r.value(QStringLiteral("MEMO"));
        row.classId = ctx.klass(r.value(QStringLiteral("CLASS")));
        row.itemId = ctx.item(r.value(QStringLiteral("INVITEM")));
        if (row.accountId == 0) {
            const QString key = row.accountName.isEmpty() ? QStringLiteral("(no account)") : row.accountName;
            (*unknown)[key] << r.line;
            if (!missing.contains(key)) {
                missing << key;
            }
        }
        sum += row.amount;
        debits += std::max<qint64>(row.amount, 0);
        p.rows << row;
    }
    if (sum != 0) {
        p.refusal = where + QStringLiteral("the rows do not balance: debits %1, credits %2")
                                .arg(ctx.file.money(debits).toPlainString(),
                                     ctx.file.money(debits - sum).toPlainString());
    } else if (!missing.isEmpty()) {
        p.refusal = where + QStringLiteral("no account in this company for %1").arg(missing.join(QStringLiteral(", ")));
    } else if (debits == 0) {
        p.refusal = where + QStringLiteral("every amount is zero; nothing to post");
    }
    return p;
}

// Names on the rows → parties. The TRNS row's name is the transaction's
// customer, vendor or payee (created when missing); a split names its own
// party only where it means one — deposits list who paid, journal lines on
// A/R and A/P name a customer or vendor — and otherwise only an existing
// name is used: QuickBooks writes the tax agency on an invoice's sales-tax
// split, which must not become a customer. A name that is no party is kept
// in the row's memo.
void resolveParties(Context &ctx, TxPlan &p, const QHash<qint64, AccountType> &types)
{
    const Side side = sideOf(p.type);
    const QString head = p.rows.first().name.toLower();
    for (qsizetype i = 0; i < p.rows.size(); ++i) {
        TxRow &row = p.rows[i];
        if (row.name.isEmpty()) {
            continue;
        }
        const QString n = row.name.toLower();
        const AccountType type = types.value(row.accountId, AccountType::Expense);
        const bool trns = i == 0 || n == head;
        if (side == Side::Customer && trns) {
            row.partyId = ctx.customer(row.name, true);
        } else if (side == Side::Vendor && trns) {
            row.partyId = ctx.vendor(row.name, true);
        } else if (side == Side::Payee && trns) {
            row.partyId = ctx.vendors.contains(n) || !ctx.customers.contains(n) ? ctx.vendor(row.name, true)
                                                                                : ctx.customers.value(n);
        } else if (side == Side::Depositor && i > 0) {
            row.partyId = ctx.customers.contains(n) || !ctx.vendors.contains(n) ? ctx.customer(row.name, true)
                                                                                : ctx.vendors.value(n);
        } else if (type == AccountType::AccountsReceivable) {
            row.partyId = ctx.customer(row.name, true);
        } else if (type == AccountType::AccountsPayable) {
            row.partyId = ctx.vendor(row.name, true);
        } else {
            row.partyId = ctx.anyParty(row.name);
        }
        if (row.partyId == 0) {
            row.memo = row.memo.isEmpty() ? row.name : row.memo + QStringLiteral(" · ") + row.name;
        }
    }
}

// "Already in the books" (iif_import.h): the same date and the same
// account/amount rows, posted for the same name. A document puts its party
// on every line while a file names it on some rows only, so a posted
// transaction matches when the plan's main name (the TRNS row's, else the
// first named row's) is AMONG its parties.
QString rowsKey(const QDate &date, QList<QPair<qint64, qint64>> rows)
{
    std::sort(rows.begin(), rows.end());
    QStringList parts;
    for (const auto &[account, amount] : std::as_const(rows)) {
        if (amount != 0) {
            parts << QStringLiteral("%1:%2").arg(account).arg(amount);
        }
    }
    return date.toString(Qt::ISODate) + QLatin1Char('|') + parts.join(QLatin1Char(','));
}

QString rowsKey(const TxPlan &p)
{
    QList<QPair<qint64, qint64>> rows;
    for (const TxRow &r : p.rows) {
        rows << qMakePair(r.accountId, r.amount);
    }
    return rowsKey(p.date, rows);
}

qint64 mainParty(const TxPlan &p)
{
    for (const TxRow &r : p.rows) {
        if (r.partyId != 0) {
            return r.partyId;
        }
    }
    return 0;
}

// The live transactions already posted on `date`: rows key → the parties
// of each.
using Existing = QHash<QString, QList<QSet<qint64>>>;

Existing existingOn(CompanyFile &file, const QDate &date)
{
    QHash<qint64, QList<QPair<qint64, qint64>>> rows;
    QHash<qint64, QSet<qint64>> parties;
    QSqlQuery q(file.database());
    Sql::run(q, QStringLiteral("SELECT txn_id, account_id, debit - credit, COALESCE(party_id, 0) FROM journal_lines"
                               " WHERE date = ? AND voided = 0 ORDER BY txn_id, id"),
             {Sql::isoDate(date)});
    while (q.next()) {
        rows[q.value(0).toLongLong()] << qMakePair(q.value(1).toLongLong(), q.value(2).toLongLong());
        parties[q.value(0).toLongLong()].insert(q.value(3).toLongLong());
    }
    Existing out;
    for (auto it = rows.cbegin(); it != rows.cend(); ++it) {
        out[rowsKey(date, it.value())] << parties.value(it.key());
    }
    return out;
}

int countMatching(const Existing &existing, const QString &key, qint64 party)
{
    int n = 0;
    for (const QSet<qint64> &parties : existing.value(key)) {
        n += party == 0 || parties.contains(party) ? 1 : 0;
    }
    return n;
}

// True when the live postings of `docs` are exactly the plan's rows.
bool postedAsPlanned(CompanyFile &file, const QList<qint64> &docs, const TxPlan &p)
{
    QList<QPair<qint64, qint64>> posted;
    for (qint64 doc : docs) {
        QSqlQuery q(file.database());
        Sql::run(q, QStringLiteral("SELECT l.account_id, l.debit - l.credit FROM journal_lines l JOIN documents d"
                                   " ON d.txn_id = l.txn_id WHERE d.id = ?"),
                 {doc});
        while (q.next()) {
            posted << qMakePair(q.value(0).toLongLong(), q.value(1).toLongLong());
        }
    }
    QList<QPair<qint64, qint64>> planned;
    for (const TxRow &r : p.rows) {
        if (r.amount != 0) {
            planned << qMakePair(r.accountId, r.amount);
        }
    }
    std::sort(posted.begin(), posted.end());
    std::sort(planned.begin(), planned.end());
    return !docs.isEmpty() && posted == planned;
}

Result postJournal(Context &ctx, const TxPlan &p)
{
    JournalEntryInput e;
    e.date = p.date;
    e.number = p.number;
    e.memo = p.type == QLatin1String("GENERALJOURNAL") || p.typeText.isEmpty()
                 ? p.memo
                 : (p.memo.isEmpty() ? QString() : p.memo + QLatin1Char(' ')) + QStringLiteral("(QuickBooks %1)").arg(p.typeText);
    for (const TxRow &r : p.rows) {
        if (r.amount == 0) {
            continue;
        }
        JournalLine l;
        l.accountId = r.accountId;
        l.debit = ctx.file.money(r.amount > 0 ? r.amount : 0);
        l.credit = ctx.file.money(r.amount < 0 ? -r.amount : 0);
        l.partyId = r.partyId;
        l.classId = r.classId;
        l.memo = r.memo;
        e.lines << l;
    }
    return Ledger(ctx.file).postJournalEntry(e);
}

IifTypeCount &countFor(IifImportReport &report, const TxPlan &p)
{
    for (IifTypeCount &c : report.transactions) {
        if (IifReader::normalizedType(c.type) == p.type) {
            return c;
        }
    }
    report.transactions << IifTypeCount{p.typeText.isEmpty() ? QStringLiteral("(no type)") : p.typeText, 0, 0, 0, 0, 0};
    return report.transactions.last();
}

} // namespace

void importTransactions(Context &ctx, const QList<IifTransaction> &transactions)
{
    QHash<qint64, AccountType> types;
    for (const Account &a : ChartOfAccounts(ctx.file).accounts(true)) {
        types.insert(a.id, a.type);
    }
    QHash<QString, QList<int>> unknown;
    QList<TxPlan> plans;
    QList<IifTransaction> orders; // M68: non-posting, never planned as postings
    for (int i = 0; i < transactions.size(); ++i) {
        if (isPurchaseOrder(transactions.at(i))) {
            orders << transactions.at(i);
            continue;
        }
        plans << plan(ctx, transactions.at(i), i, &unknown);
    }
    for (auto it = unknown.cbegin(); it != unknown.cend(); ++it) {
        ctx.report.unmappedAccounts << IifUnmappedAccount{it.key(), it.value()};
    }
    std::sort(ctx.report.unmappedAccounts.begin(), ctx.report.unmappedAccounts.end(),
              [](const IifUnmappedAccount &a, const IifUnmappedAccount &b) { return a.name < b.name; });
    std::stable_sort(plans.begin(), plans.end(), [](const TxPlan &a, const TxPlan &b) {
        if (a.date != b.date) {
            return a.date < b.date;
        }
        const int pa = priorityOf(a.type);
        const int pb = priorityOf(b.type);
        return pa != pb ? pa < pb : a.order < b.order;
    });
    QHash<QDate, Existing> existing; // snapshot per date, taken before anything of that date posts
    QHash<QString, int> seen;
    for (TxPlan &p : plans) {
        IifTypeCount &count = countFor(ctx.report, p);
        ++count.count;
        if (!p.refusal.isEmpty()) {
            ++count.refused;
            ctx.report.refused << IifRefusal{p.firstLine, p.lastLine, p.typeText, p.refusal};
            continue;
        }
        resolveParties(ctx, p, types);
        if (!existing.contains(p.date)) {
            existing.insert(p.date, existingOn(ctx.file, p.date));
        }
        const QString key = rowsKey(p);
        const qint64 party = mainParty(p);
        if (++seen[key + QLatin1Char('#') + QString::number(party)] <= countMatching(existing.value(p.date), key, party)) {
            ++count.duplicates;
            continue;
        }
        bool asDocument = false;
        {
            // AGENT-GUARD: the document is kept only when what it posted is
            // the rows; otherwise this savepoint rolls it back.
            SqlTransaction attempt(ctx.file);
            QList<qint64> docs;
            if (postDocument(ctx, p, &docs) && postedAsPlanned(ctx.file, docs, p)) {
                asDocument = attempt.commit();
            }
        }
        Result posted = Result::success();
        if (!asDocument) {
            posted = postJournal(ctx, p);
        }
        if (!posted.ok) {
            ++count.refused;
            ctx.report.refused << IifRefusal{p.firstLine, p.lastLine, p.typeText,
                                             QStringLiteral("lines %1–%2: %3").arg(p.firstLine).arg(p.lastLine).arg(posted.error)};
            continue;
        }
        ++(asDocument ? count.documents : count.journal);
        ++ctx.report.posted;
        for (const TxRow &r : std::as_const(p.rows)) {
            ctx.report.postedDebits += std::max<qint64>(r.amount, 0);
        }
    }
    importPurchaseOrders(ctx, orders);
}

} // namespace QindaOffice::Books::IifInternal

namespace QindaOffice::Books {

IifImportReport IifImport::run(CompanyFile &file, const QByteArray &iif, const IifImportOptions &options)
{
    IifImportReport report;
    const IifDocument doc = IifReader::parse(iif);
    if (doc.lists.isEmpty() && doc.transactions.isEmpty()) {
        report.error = doc.problems.isEmpty()
                           ? QStringLiteral("There are no QuickBooks IIF lists or transactions in this file.")
                           : QStringLiteral("This is not a QuickBooks IIF file (%1).").arg(doc.problems.first());
        return report;
    }
    for (const QString &problem : doc.problems) {
        report.note(problem);
    }
    // AGENT-CONTRACT (iif_import.h): one batch. A dry run is the same work,
    // rolled back when `batch` goes out of scope uncommitted.
    SqlTransaction batch(file);
    if (!batch.begun()) {
        report.error = batch.error();
        return report;
    }
    IifInternal::Context ctx(file, options, report);
    IifInternal::importLists(ctx, doc.lists);
    IifInternal::importEmployeesAndTime(ctx, doc.lists);
    IifInternal::importTransactions(ctx, doc.transactions);
    IifInternal::deactivateHidden(ctx);
    report.ok = true;
    if (!options.commit) {
        return report;
    }
    if (!report.refused.isEmpty() && !options.skipRefused) {
        report.ok = false;
        report.error = QStringLiteral("%1 IIF record(s) were refused, so nothing was imported.").arg(report.refused.size());
        return report;
    }
    if (!batch.commit()) {
        report.ok = false;
        report.error = batch.error();
        return report;
    }
    report.committed = true;
    return report;
}

} // namespace QindaOffice::Books
