// SPDX-License-Identifier: GPL-3.0-or-later
#include "ledger_export.h"

#include "accounts.h"
#include "company_file.h"
#include "csv_util.h"
#include "lists.h"

#include <QHash>
#include <QSqlQuery>

namespace QindaOffice::Books {

namespace {

struct Names {
    QHash<qint64, QString> accounts; // full names
    QHash<qint64, QString> parties;
    QHash<qint64, QString> classes;
    QHash<qint64, QString> terms;
};

Names namesOf(CompanyFile &file)
{
    Names n;
    for (const Account &a : ChartOfAccounts(file).accounts(true)) {
        n.accounts.insert(a.id, a.fullName);
    }
    Lists lists(file);
    for (PartyKind kind : {PartyKind::Customer, PartyKind::Vendor}) {
        for (const Party &p : lists.parties(kind, true)) {
            n.parties.insert(p.id, p.displayName);
        }
    }
    for (const ClassTag &c : lists.classes(true)) {
        n.classes.insert(c.id, c.name);
    }
    for (const Terms &t : lists.terms(true)) {
        n.terms.insert(t.id, t.name);
    }
    return n;
}

struct PostingRow {
    qint64 txn = 0;
    QDate date;
    DocKind kind = DocKind::JournalEntry;
    QString number;
    QString txnMemo;
    qint64 account = 0;
    qint64 debit = 0;
    qint64 credit = 0;
    qint64 party = 0;
    qint64 klass = 0;
    QString memo;
};

// AGENT-NOTE: reads the posted ledger directly (the schema contract in
// libs/books_engine/src/schema_migrations.cpp): live transactions only.
QList<PostingRow> postings(CompanyFile &file, const QDate &from, const QDate &to)
{
    QList<PostingRow> rows;
    QSqlQuery q(file.database());
    q.prepare(QStringLiteral("SELECT t.id, t.date, t.kind, COALESCE(t.number, ''), COALESCE(t.memo, ''), l.account_id,"
                             " l.debit, l.credit, COALESCE(l.party_id, 0), COALESCE(l.class_id, 0), COALESCE(l.memo, '')"
                             " FROM transactions t JOIN journal_lines l ON l.txn_id = t.id WHERE t.voided = 0"
                             " AND (COALESCE(?, '') = '' OR t.date >= ?) AND (COALESCE(?, '') = '' OR t.date <= ?)"
                             " ORDER BY t.date, t.id, l.line_no"));
    const QString f = from.isValid() ? from.toString(Qt::ISODate) : QString();
    const QString t = to.isValid() ? to.toString(Qt::ISODate) : QString();
    q.addBindValue(f);
    q.addBindValue(f);
    q.addBindValue(t);
    q.addBindValue(t);
    q.exec();
    while (q.next()) {
        rows << PostingRow{q.value(0).toLongLong(), QDate::fromString(q.value(1).toString(), Qt::ISODate),
                           DocKind(q.value(2).toInt()), q.value(3).toString(), q.value(4).toString(),
                           q.value(5).toLongLong(), q.value(6).toLongLong(), q.value(7).toLongLong(),
                           q.value(8).toLongLong(), q.value(9).toLongLong(), q.value(10).toString()};
    }
    return rows;
}

QString yesNo(bool value)
{
    return value ? QStringLiteral("Y") : QStringLiteral("N");
}

QString itemKindText(ItemKind kind)
{
    switch (kind) {
    case ItemKind::Service: return QStringLiteral("service");
    case ItemKind::NonInventory: return QStringLiteral("non-inventory");
    case ItemKind::OtherCharge: return QStringLiteral("other charge");
    }
    return QStringLiteral("service");
}

// IIF values are tab-separated lines: no tabs, no line breaks, and double
// quotes would start a quoted field in QuickBooks' reader.
QString iifValue(const QString &value)
{
    QString v = value;
    v.replace(QLatin1Char('\t'), QLatin1Char(' '));
    v.replace(QLatin1Char('\r'), QLatin1Char(' '));
    v.replace(QLatin1Char('\n'), QLatin1Char(' '));
    v.replace(QLatin1Char('"'), QLatin1Char('\''));
    return v.simplified();
}

void iifLine(QByteArray &out, const QStringList &fields)
{
    QStringList clean;
    for (const QString &f : fields) {
        clean << iifValue(f);
    }
    out += clean.join(QLatin1Char('\t')).toUtf8();
    out += "\r\n";
}

QStringList addressLines(const QString &name, const QString &address, int count)
{
    QStringList lines{name};
    for (const QString &l : address.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        lines << l.trimmed();
    }
    while (lines.size() < count) {
        lines << QString();
    }
    return lines.mid(0, count);
}

} // namespace

QString LedgerExport::iifAccountType(int accountType)
{
    switch (AccountType(accountType)) {
    case AccountType::Bank: return QStringLiteral("BANK");
    case AccountType::AccountsReceivable: return QStringLiteral("AR");
    case AccountType::OtherCurrentAsset: return QStringLiteral("OCASSET");
    case AccountType::FixedAsset: return QStringLiteral("FIXASSET");
    case AccountType::OtherAsset: return QStringLiteral("OASSET");
    case AccountType::AccountsPayable: return QStringLiteral("AP");
    case AccountType::CreditCard: return QStringLiteral("CCARD");
    case AccountType::OtherCurrentLiability: return QStringLiteral("OCLIAB");
    case AccountType::LongTermLiability: return QStringLiteral("LTLIAB");
    case AccountType::Equity: return QStringLiteral("EQUITY");
    case AccountType::Income: return QStringLiteral("INC");
    case AccountType::OtherIncome: return QStringLiteral("EXINC");
    case AccountType::CostOfGoodsSold: return QStringLiteral("COGS");
    case AccountType::Expense: return QStringLiteral("EXP");
    case AccountType::OtherExpense: return QStringLiteral("EXEXP");
    }
    return QStringLiteral("EXP");
}

QByteArray LedgerExport::journalCsv(CompanyFile &file, const QDate &from, const QDate &to)
{
    const Names names = namesOf(file);
    QList<QStringList> rows{{QStringLiteral("Date"), QStringLiteral("Type"), QStringLiteral("Num"), QStringLiteral("Name"),
                             QStringLiteral("Account"), QStringLiteral("Debit"), QStringLiteral("Credit"),
                             QStringLiteral("Memo")}};
    for (const PostingRow &p : postings(file, from, to)) {
        rows << QStringList{p.date.toString(Qt::ISODate), docKindName(p.kind), p.number, names.parties.value(p.party),
                            names.accounts.value(p.account),
                            p.debit != 0 ? file.money(p.debit).toPlainString() : QString(),
                            p.credit != 0 ? file.money(p.credit).toPlainString() : QString(),
                            p.memo.isEmpty() ? p.txnMemo : p.memo};
    }
    return Csv::document(rows);
}

QByteArray LedgerExport::listCsv(CompanyFile &file, ListKind kind)
{
    const Names names = namesOf(file);
    QList<QStringList> rows;
    auto active = [](bool a) { return a ? QStringLiteral("yes") : QStringLiteral("no"); };
    switch (kind) {
    case ListKind::Accounts:
        rows << QStringList{QStringLiteral("Number"), QStringLiteral("Name"), QStringLiteral("Type"),
                            QStringLiteral("Description"), QStringLiteral("Active")};
        for (const Account &a : ChartOfAccounts(file).accounts(true)) {
            rows << QStringList{a.number, a.fullName, accountTypeInfo(a.type).key, a.description, active(a.active)};
        }
        break;
    case ListKind::Customers:
    case ListKind::Vendors: {
        const bool vendors = kind == ListKind::Vendors;
        QStringList header{QStringLiteral("Name"), QStringLiteral("Company"), QStringLiteral("Contact"),
                           QStringLiteral("Email"), QStringLiteral("Phone"), QStringLiteral("Billing Address"),
                           QStringLiteral("Shipping Address"), QStringLiteral("Tax ID"), QStringLiteral("Terms")};
        if (vendors) {
            header << QStringLiteral("1099") << QStringLiteral("Default Account");
        }
        header << QStringLiteral("Notes") << QStringLiteral("Active");
        rows << header;
        for (const Party &p : Lists(file).parties(vendors ? PartyKind::Vendor : PartyKind::Customer, true)) {
            QStringList row{p.displayName, p.companyName, p.contactName, p.email, p.phone, p.billingAddress,
                            p.shippingAddress, p.taxId, names.terms.value(p.termsId)};
            if (vendors) {
                row << (p.is1099 ? QStringLiteral("yes") : QStringLiteral("no")) << names.accounts.value(p.defaultAccountId);
            }
            row << p.notes << active(p.active);
            rows << row;
        }
        break;
    }
    case ListKind::Items:
        rows << QStringList{QStringLiteral("Name"), QStringLiteral("Type"), QStringLiteral("Description"),
                            QStringLiteral("SKU"), QStringLiteral("Price"), QStringLiteral("Cost"),
                            QStringLiteral("Income Account"), QStringLiteral("Expense Account"),
                            QStringLiteral("Taxable"), QStringLiteral("Active")};
        for (const Item &i : Lists(file).items(true)) {
            rows << QStringList{i.name, itemKindText(i.kind), i.description, i.sku, i.price.toPlainString(),
                                i.cost.toPlainString(), names.accounts.value(i.incomeAccountId),
                                names.accounts.value(i.expenseAccountId), i.taxable ? QStringLiteral("yes") : QStringLiteral("no"),
                                active(i.active)};
        }
        break;
    }
    return Csv::document(rows);
}

QByteArray LedgerExport::iif(CompanyFile &file, const IifOptions &options)
{
    const Names names = namesOf(file);
    QByteArray out;
    if (options.lists) {
        iifLine(out, {QStringLiteral("!ACCNT"), QStringLiteral("NAME"), QStringLiteral("ACCNTTYPE"), QStringLiteral("DESC"),
                      QStringLiteral("ACCNUM")});
        for (const Account &a : ChartOfAccounts(file).accounts(true)) {
            iifLine(out, {QStringLiteral("ACCNT"), a.fullName, iifAccountType(int(a.type)), a.description, a.number});
        }
        Lists lists(file);
        iifLine(out, {QStringLiteral("!CUST"), QStringLiteral("NAME"), QStringLiteral("COMPANYNAME"), QStringLiteral("BADDR1"),
                      QStringLiteral("BADDR2"), QStringLiteral("BADDR3"), QStringLiteral("BADDR4"), QStringLiteral("BADDR5"),
                      QStringLiteral("PHONE1"), QStringLiteral("EMAIL"), QStringLiteral("TERMS"), QStringLiteral("NOTE")});
        for (const Party &p : lists.parties(PartyKind::Customer, true)) {
            iifLine(out, QStringList{QStringLiteral("CUST"), p.displayName, p.companyName}
                             + addressLines(p.companyName.isEmpty() ? p.displayName : p.companyName, p.billingAddress, 5)
                             + QStringList{p.phone, p.email, names.terms.value(p.termsId), p.notes});
        }
        iifLine(out, {QStringLiteral("!VEND"), QStringLiteral("NAME"), QStringLiteral("COMPANYNAME"), QStringLiteral("ADDR1"),
                      QStringLiteral("ADDR2"), QStringLiteral("ADDR3"), QStringLiteral("ADDR4"), QStringLiteral("ADDR5"),
                      QStringLiteral("PHONE1"), QStringLiteral("EMAIL"), QStringLiteral("TERMS"), QStringLiteral("TAXID"),
                      QStringLiteral("1099"), QStringLiteral("NOTE")});
        for (const Party &p : lists.parties(PartyKind::Vendor, true)) {
            iifLine(out, QStringList{QStringLiteral("VEND"), p.displayName, p.companyName}
                             + addressLines(p.companyName.isEmpty() ? p.displayName : p.companyName, p.billingAddress, 5)
                             + QStringList{p.phone, p.email, names.terms.value(p.termsId), p.taxId, yesNo(p.is1099), p.notes});
        }
        iifLine(out, {QStringLiteral("!INVITEM"), QStringLiteral("NAME"), QStringLiteral("INVITEMTYPE"), QStringLiteral("DESC"),
                      QStringLiteral("ACCNT"), QStringLiteral("PRICE"), QStringLiteral("COST"), QStringLiteral("TAXABLE")});
        for (const Item &i : lists.items(true)) {
            // SERV = service, OTHC = other charge; non-inventory parts use
            // NONINV (QuickBooks' "Non-inventory Part").
            const QString type = i.kind == ItemKind::Service     ? QStringLiteral("SERV")
                                 : i.kind == ItemKind::OtherCharge ? QStringLiteral("OTHC")
                                                                   : QStringLiteral("NONINV");
            iifLine(out, {QStringLiteral("INVITEM"), i.name, type, i.description,
                          names.accounts.value(i.incomeAccountId != 0 ? i.incomeAccountId : i.expenseAccountId),
                          i.price.toPlainString(), i.cost.toPlainString(), yesNo(i.taxable)});
        }
    }
    if (options.journal) {
        const QStringList columns{QStringLiteral("TRNSTYPE"), QStringLiteral("DATE"),   QStringLiteral("ACCNT"),
                                  QStringLiteral("NAME"),     QStringLiteral("CLASS"),  QStringLiteral("AMOUNT"),
                                  QStringLiteral("DOCNUM"),   QStringLiteral("MEMO")};
        iifLine(out, QStringList{QStringLiteral("!TRNS"), QStringLiteral("TRNSID")} + columns);
        iifLine(out, QStringList{QStringLiteral("!SPL"), QStringLiteral("SPLID")} + columns);
        iifLine(out, {QStringLiteral("!ENDTRNS")});
        qint64 current = 0;
        for (const PostingRow &p : postings(file, options.from, options.to)) {
            const bool first = p.txn != current;
            if (first && current != 0) {
                iifLine(out, {QStringLiteral("ENDTRNS")});
            }
            current = p.txn;
            iifLine(out, {first ? QStringLiteral("TRNS") : QStringLiteral("SPL"), QString(),
                          QStringLiteral("GENERAL JOURNAL"), p.date.toString(QStringLiteral("MM/dd/yyyy")),
                          names.accounts.value(p.account), names.parties.value(p.party), names.classes.value(p.klass),
                          file.money(p.debit - p.credit).toPlainString(), p.number,
                          p.memo.isEmpty() ? p.txnMemo : p.memo});
        }
        if (current != 0) {
            iifLine(out, {QStringLiteral("ENDTRNS")});
        }
    }
    return out;
}

} // namespace QindaOffice::Books
