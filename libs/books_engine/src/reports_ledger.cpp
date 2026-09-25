// SPDX-License-Identifier: GPL-3.0-or-later
// General Ledger, Journal and Transaction List by Date — listings of what
// was posted (accrual by nature; voided transactions are left out).
#include "reports.h"

#include "company_file.h"
#include "reports_util.h"
#include "sql_util.h"

#include <QSqlQuery>

namespace QindaOffice::Books {

using namespace ReportUtil;

namespace {

ReportCell textOrBlank(const QString &text)
{
    return text.isEmpty() ? ReportCell::blank() : ReportCell::ofText(text);
}

} // namespace

ReportTable Reports::generalLedger(const QDate &from, const QDate &to, qint64 accountId) const
{
    ReportTable t = makeTable(m_file, QStringLiteral("general_ledger"), QStringLiteral("General Ledger"),
                              periodText(from, to) + QStringLiteral(" · Accrual basis"));
    t.columns = {{QStringLiteral("date"), QStringLiteral("Date"), ColumnType::Date},
                 {QStringLiteral("type"), QStringLiteral("Type"), ColumnType::Text},
                 {QStringLiteral("number"), QStringLiteral("Num"), ColumnType::Text},
                 {QStringLiteral("name"), QStringLiteral("Name"), ColumnType::Text},
                 {QStringLiteral("memo"), QStringLiteral("Memo"), ColumnType::Text},
                 {QStringLiteral("split"), QStringLiteral("Split"), ColumnType::Text},
                 {QStringLiteral("debit"), QStringLiteral("Debit"), ColumnType::Money},
                 {QStringLiteral("credit"), QStringLiteral("Credit"), ColumnType::Money},
                 {QStringLiteral("balance"), QStringLiteral("Balance"), ColumnType::Money}};
    const QSqlDatabase db = m_file.database();
    for (const Account &a : ChartOfAccounts(m_file).accounts(true)) {
        if (accountId != 0 && a.id != accountId) {
            continue;
        }
        const bool debitNormal = accountTypeInfo(a.type).normalBalance == NormalBalance::Debit;
        const bool permanent = accountTypeInfo(a.type).isBalanceSheet();
        // P&L accounts start each report at zero; balance-sheet accounts
        // carry their balance forward.
        qint64 opening = permanent ? Sql::scalarInt(db,
                                                    QStringLiteral("SELECT COALESCE(SUM(debit) - SUM(credit), 0) FROM"
                                                                   " journal_lines WHERE account_id = ? AND voided = 0"
                                                                   " AND date < ?"),
                                                    {a.id, Sql::isoDate(from)})
                                   : 0;
        QList<ReportRow> details;
        qint64 running = opening;
        qint64 debits = 0;
        qint64 credits = 0;
        {
            QSqlQuery q(db);
            Sql::run(q, QStringLiteral(
                            "SELECT l.date, t.kind, t.number, COALESCE(p.display_name, ''), COALESCE(l.memo, ''),"
                            " (SELECT CASE WHEN COUNT(*) = 1 THEN MAX(a2.name) ELSE '-Split-' END FROM journal_lines o"
                            "   JOIN accounts a2 ON a2.id = o.account_id WHERE o.txn_id = l.txn_id AND o.id <> l.id),"
                            " l.debit, l.credit, t.doc_id, l.txn_id FROM journal_lines l"
                            " JOIN transactions t ON t.id = l.txn_id LEFT JOIN parties p ON p.id = l.party_id"
                            " WHERE l.account_id = ? AND l.voided = 0 AND l.date >= ? AND l.date <= ?"
                            " ORDER BY l.date, l.txn_id, l.id"),
                     {a.id, Sql::isoDate(from), Sql::isoDate(to)});
            while (q.next()) {
                const qint64 debit = q.value(6).toLongLong();
                const qint64 credit = q.value(7).toLongLong();
                running += debit - credit;
                debits += debit;
                credits += credit;
                ReportRow r;
                r.kind = RowKind::Detail;
                r.depth = 1;
                r.key = QStringLiteral("txn:%1:%2").arg(q.value(9).toLongLong()).arg(a.id);
                r.cells = {ReportCell::ofDate(Sql::fromIso(q.value(0))),
                           ReportCell::ofText(docKindName(DocKind(q.value(1).toInt()))),
                           textOrBlank(q.value(2).toString()),
                           textOrBlank(q.value(3).toString()),
                           textOrBlank(q.value(4).toString()),
                           textOrBlank(q.value(5).toString()),
                           debit != 0 ? ReportCell::ofMoney(debit) : ReportCell::blank(),
                           credit != 0 ? ReportCell::ofMoney(credit) : ReportCell::blank(),
                           ReportCell::ofMoney(debitNormal ? running : -running)};
                r.drill.documentIds = {q.value(8).toLongLong()};
                details << r;
            }
        }
        if (details.isEmpty() && opening == 0) {
            continue;
        }
        ReportRow header;
        header.kind = RowKind::Section;
        header.key = QStringLiteral("account:%1").arg(a.id);
        header.cells = {ReportCell::blank(), ReportCell::ofText(a.fullName)};
        for (int i = 2; i < t.columns.size(); ++i) {
            header.cells << ReportCell::blank();
        }
        header.drill = DrillDown{a.id, 0, 0, from, to, {}};
        t.rows << header;
        ReportRow start = header;
        start.kind = RowKind::Detail;
        start.depth = 1;
        start.key = QStringLiteral("opening:%1").arg(a.id);
        start.cells[1] = ReportCell::ofText(QStringLiteral("Beginning Balance"));
        start.cells[8] = ReportCell::ofMoney(debitNormal ? opening : -opening);
        t.rows << start;
        t.rows << details;
        ReportRow total = header;
        total.kind = RowKind::Subtotal;
        total.key = QStringLiteral("subtotal:account:%1").arg(a.id);
        total.cells[1] = ReportCell::ofText(QStringLiteral("Total for ") + a.fullName);
        total.cells[6] = ReportCell::ofMoney(debits);
        total.cells[7] = ReportCell::ofMoney(credits);
        total.cells[8] = ReportCell::ofMoney(debitNormal ? running : -running);
        t.rows << total;
    }
    return t;
}

ReportTable Reports::journal(const QDate &from, const QDate &to) const
{
    ReportTable t = makeTable(m_file, QStringLiteral("journal"), QStringLiteral("Journal"), periodText(from, to));
    t.columns = {{QStringLiteral("date"), QStringLiteral("Date"), ColumnType::Date},
                 {QStringLiteral("type"), QStringLiteral("Type"), ColumnType::Text},
                 {QStringLiteral("number"), QStringLiteral("Num"), ColumnType::Text},
                 {QStringLiteral("name"), QStringLiteral("Name"), ColumnType::Text},
                 {QStringLiteral("memo"), QStringLiteral("Memo"), ColumnType::Text},
                 {QStringLiteral("account"), QStringLiteral("Account"), ColumnType::Text},
                 {QStringLiteral("debit"), QStringLiteral("Debit"), ColumnType::Money},
                 {QStringLiteral("credit"), QStringLiteral("Credit"), ColumnType::Money}};
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT l.txn_id, l.date, t.kind, t.number, COALESCE(p.display_name, ''),"
                               " COALESCE(l.memo, ''), a.name, l.debit, l.credit, t.doc_id FROM journal_lines l"
                               " JOIN transactions t ON t.id = l.txn_id JOIN accounts a ON a.id = l.account_id"
                               " LEFT JOIN parties p ON p.id = l.party_id WHERE l.voided = 0 AND l.date >= ?"
                               " AND l.date <= ? ORDER BY l.date, l.txn_id, l.line_no"),
             {Sql::isoDate(from), Sql::isoDate(to)});
    qint64 currentTxn = 0;
    qint64 txnDebit = 0;
    qint64 txnCredit = 0;
    qint64 allDebit = 0;
    qint64 allCredit = 0;
    auto closeTxn = [&] {
        if (currentTxn != 0) {
            ReportRow r;
            r.kind = RowKind::Subtotal;
            r.key = QStringLiteral("subtotal:txn:%1").arg(currentTxn);
            r.cells = {ReportCell::blank(), ReportCell::blank(), ReportCell::blank(), ReportCell::blank(),
                       ReportCell::blank(), ReportCell::blank(), ReportCell::ofMoney(txnDebit), ReportCell::ofMoney(txnCredit)};
            t.rows << r;
        }
    };
    while (q.next()) {
        const qint64 txn = q.value(0).toLongLong();
        if (txn != currentTxn) {
            closeTxn();
            currentTxn = txn;
            txnDebit = txnCredit = 0;
        }
        const qint64 debit = q.value(7).toLongLong();
        const qint64 credit = q.value(8).toLongLong();
        txnDebit += debit;
        txnCredit += credit;
        allDebit += debit;
        allCredit += credit;
        ReportRow r;
        r.kind = RowKind::Detail;
        r.key = QStringLiteral("txn:%1").arg(txn);
        r.cells = {ReportCell::ofDate(Sql::fromIso(q.value(1))), ReportCell::ofText(docKindName(DocKind(q.value(2).toInt()))),
                   textOrBlank(q.value(3).toString()), textOrBlank(q.value(4).toString()),
                   textOrBlank(q.value(5).toString()), ReportCell::ofText(q.value(6).toString()),
                   debit != 0 ? ReportCell::ofMoney(debit) : ReportCell::blank(),
                   credit != 0 ? ReportCell::ofMoney(credit) : ReportCell::blank()};
        r.drill.documentIds = {q.value(9).toLongLong()};
        t.rows << r;
    }
    closeTxn();
    ReportRow total;
    total.kind = RowKind::Total;
    total.key = QStringLiteral("total");
    total.cells = {ReportCell::blank(), ReportCell::ofText(QStringLiteral("Total")), ReportCell::blank(),
                   ReportCell::blank(), ReportCell::blank(), ReportCell::blank(), ReportCell::ofMoney(allDebit),
                   ReportCell::ofMoney(allCredit)};
    t.rows << total;
    return t;
}

ReportTable Reports::transactionList(const QDate &from, const QDate &to) const
{
    ReportTable t = makeTable(m_file, QStringLiteral("transaction_list"), QStringLiteral("Transaction List by Date"),
                              periodText(from, to));
    t.columns = {{QStringLiteral("date"), QStringLiteral("Date"), ColumnType::Date},
                 {QStringLiteral("type"), QStringLiteral("Type"), ColumnType::Text},
                 {QStringLiteral("number"), QStringLiteral("Num"), ColumnType::Text},
                 {QStringLiteral("name"), QStringLiteral("Name"), ColumnType::Text},
                 {QStringLiteral("memo"), QStringLiteral("Memo"), ColumnType::Text},
                 {QStringLiteral("amount"), QStringLiteral("Amount"), ColumnType::Money}};
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT t.id, t.date, t.kind, t.number, COALESCE(p.display_name, ''),"
                               " COALESCE(t.memo, ''), (SELECT SUM(debit) FROM journal_lines WHERE txn_id = t.id),"
                               " t.doc_id FROM transactions t JOIN documents d ON d.id = t.doc_id"
                               " LEFT JOIN parties p ON p.id = d.party_id WHERE t.voided = 0 AND t.date >= ?"
                               " AND t.date <= ? ORDER BY t.date, t.id"),
             {Sql::isoDate(from), Sql::isoDate(to)});
    while (q.next()) {
        ReportRow r;
        r.kind = RowKind::Detail;
        r.key = QStringLiteral("txn:%1").arg(q.value(0).toLongLong());
        r.cells = {ReportCell::ofDate(Sql::fromIso(q.value(1))), ReportCell::ofText(docKindName(DocKind(q.value(2).toInt()))),
                   textOrBlank(q.value(3).toString()), textOrBlank(q.value(4).toString()),
                   textOrBlank(q.value(5).toString()), ReportCell::ofMoney(q.value(6).toLongLong())};
        r.drill.documentIds = {q.value(7).toLongLong()};
        t.rows << r;
    }
    return t;
}

} // namespace QindaOffice::Books
