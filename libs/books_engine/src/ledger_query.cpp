// SPDX-License-Identifier: GPL-3.0-or-later
#include "ledger_query.h"

#include "accounts.h"
#include "company_file.h"
#include "decimal.h"
#include "sql_util.h"

#include <QMap>
#include <QSqlQuery>

#include <algorithm>
#include <tuple>

namespace QindaOffice::Books {

namespace {

// Kinds the cash basis defers until settled.
const QString kDeferredKinds = QStringLiteral("%1,%2,%3,%4")
                                   .arg(int(DocKind::Invoice))
                                   .arg(int(DocKind::CreditMemo))
                                   .arg(int(DocKind::Bill))
                                   .arg(int(DocKind::VendorCredit));

bool isDeferred(DocKind kind)
{
    return docKindIsReceivable(kind) || docKindIsPayable(kind);
}

using Key = std::tuple<qint64, int, qint64, qint64, qint64>;

} // namespace

LedgerQuery::LedgerQuery(CompanyFile &file)
    : m_file(file)
{
}

QList<LedgerSum> LedgerQuery::aggregate(const LedgerFilter &filter, int groups) const
{
    QStringList select;
    QStringList groupBy;
    auto add = [&](int flag, const QString &expr) {
        if (groups & flag) {
            select << expr;
            groupBy << expr;
        } else {
            select << QStringLiteral("0");
        }
    };
    add(GroupAccount, QStringLiteral("account_id"));
    add(GroupMonth, QStringLiteral("(CAST(substr(date, 1, 4) AS INTEGER) * 12 + CAST(substr(date, 6, 2) AS INTEGER) - 1)"));
    add(GroupClass, QStringLiteral("COALESCE(class_id, 0)"));
    add(GroupParty, QStringLiteral("COALESCE(party_id, 0)"));
    add(GroupItem, QStringLiteral("COALESCE(item_id, 0)"));
    QString sql = QStringLiteral("SELECT ") + select.join(QStringLiteral(", "))
        + QStringLiteral(", SUM(debit), SUM(credit) FROM journal_lines WHERE voided = 0");
    QVariantList binds;
    if (filter.from.isValid()) {
        sql += QStringLiteral(" AND date >= ?");
        binds << Sql::isoDate(filter.from);
    }
    if (filter.to.isValid()) {
        sql += QStringLiteral(" AND date <= ?");
        binds << Sql::isoDate(filter.to);
    }
    if (filter.classId) {
        sql += QStringLiteral(" AND COALESCE(class_id, 0) = ?");
        binds << *filter.classId;
    }
    if (filter.basis == Basis::Cash) {
        sql += QStringLiteral(" AND kind NOT IN (") + kDeferredKinds + QLatin1Char(')');
    }
    if (!groupBy.isEmpty()) {
        sql += QStringLiteral(" GROUP BY ") + groupBy.join(QStringLiteral(", "));
    }
    QMap<Key, LedgerSum> merged;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, sql, binds);
        while (q.next()) {
            LedgerSum s;
            s.accountId = q.value(0).toLongLong();
            s.month = (groups & GroupMonth) ? q.value(1).toInt() : -1;
            s.classId = q.value(2).toLongLong();
            s.partyId = q.value(3).toLongLong();
            s.itemId = q.value(4).toLongLong();
            s.debit = q.value(5).toLongLong();
            s.credit = q.value(6).toLongLong();
            if (s.debit == 0 && s.credit == 0 && q.value(5).isNull()) {
                continue; // no rows at all
            }
            merged.insert(Key{s.accountId, s.month, s.classId, s.partyId, s.itemId}, s);
        }
    }
    if (filter.basis == Basis::Cash) {
        for (const VirtualLine &v : cashRecognition(filter.from, filter.to)) {
            if (filter.classId && v.classId != *filter.classId) {
                continue;
            }
            const Key key{(groups & GroupAccount) ? v.accountId : 0, (groups & GroupMonth) ? monthIndex(v.date) : -1,
                          (groups & GroupClass) ? v.classId : 0, (groups & GroupParty) ? v.partyId : 0,
                          (groups & GroupItem) ? v.itemId : 0};
            LedgerSum &s = merged[key];
            std::tie(s.accountId, s.month, s.classId, s.partyId, s.itemId) = key;
            s.debit += v.debit;
            s.credit += v.credit;
        }
    }
    return merged.values();
}

QHash<qint64, qint64> LedgerQuery::netByAccount(const LedgerFilter &filter) const
{
    QHash<qint64, qint64> out;
    for (const LedgerSum &s : aggregate(filter, GroupAccount)) {
        out[s.accountId] += s.net();
    }
    return out;
}

QList<VirtualLine> LedgerQuery::cashRecognition(const QDate &from, const QDate &to) const
{
    const QSqlDatabase db = m_file.database();
    struct App {
        qint64 id;
        QDate date;
        qint64 amount;
    };
    struct Doc {
        DocKind kind = DocKind::Invoice;
        qint64 total = 0;
        qint64 txnId = 0;
        QList<App> apps;
    };
    QHash<qint64, Doc> docs;
    {
        // Every live application up to `to`, joined to both documents.
        QSqlQuery q(db);
        Sql::run(q, QStringLiteral("SELECT a.id, a.date, a.amount, s.id, s.kind, s.total, s.txn_id, t.id, t.kind,"
                                   " t.total, t.txn_id FROM applications a"
                                   " JOIN documents s ON s.id = a.source_doc_id JOIN documents t ON t.id = a.target_doc_id"
                                   " WHERE a.voided = 0 AND (? = '' OR a.date <= ?)"),
                 {Sql::isoDate(to), Sql::isoDate(to)});
        while (q.next()) {
            const App app{q.value(0).toLongLong(), Sql::fromIso(q.value(1)), q.value(2).toLongLong()};
            for (int side = 0; side < 2; ++side) {
                const int base = 3 + side * 4;
                const DocKind kind = DocKind(q.value(base + 1).toInt());
                const qint64 txn = q.value(base + 3).toLongLong();
                if (!isDeferred(kind) || txn == 0) {
                    continue;
                }
                Doc &doc = docs[q.value(base).toLongLong()];
                doc.kind = kind;
                doc.total = q.value(base + 2).toLongLong();
                doc.txnId = txn;
                doc.apps << app;
            }
        }
    }
    QHash<qint64, AccountType> types;
    {
        QSqlQuery q(db);
        Sql::run(q, QStringLiteral("SELECT id, type FROM accounts"));
        while (q.next()) {
            types.insert(q.value(0).toLongLong(), AccountType(q.value(1).toInt()));
        }
    }
    QList<VirtualLine> out;
    for (auto it = docs.begin(); it != docs.end(); ++it) {
        Doc &doc = it.value();
        if (doc.total == 0) {
            continue;
        }
        struct Line {
            qint64 account, party, klass, item, net;
        };
        QList<Line> lines;
        qint64 settleAccount = 0;
        qint64 settleParty = 0;
        {
            QSqlQuery q(db);
            Sql::run(q, QStringLiteral("SELECT account_id, COALESCE(party_id, 0), COALESCE(class_id, 0),"
                                       " COALESCE(item_id, 0), debit - credit FROM journal_lines WHERE txn_id = ?"
                                       " ORDER BY line_no"),
                     {doc.txnId});
            while (q.next()) {
                const qint64 account = q.value(0).toLongLong();
                const AccountType type = types.value(account);
                if (type == AccountType::AccountsReceivable || type == AccountType::AccountsPayable) {
                    settleAccount = account;
                    settleParty = q.value(1).toLongLong();
                    continue;
                }
                lines << Line{account, q.value(1).toLongLong(), q.value(2).toLongLong(), q.value(3).toLongLong(),
                              q.value(4).toLongLong()};
            }
        }
        std::sort(doc.apps.begin(), doc.apps.end(), [](const App &a, const App &b) {
            return a.date != b.date ? a.date < b.date : a.id < b.id;
        });
        qint64 cumulative = 0;
        for (const App &app : std::as_const(doc.apps)) {
            const qint64 before = cumulative;
            cumulative = qMin(cumulative + app.amount, doc.total);
            if (from.isValid() && app.date < from) {
                continue;
            }
            qint64 sum = 0;
            for (const Line &line : std::as_const(lines)) {
                const qint64 after = scaleExact(line.net, cumulative, doc.total, RoundingMode::HalfUp).value_or(0);
                const qint64 prior = scaleExact(line.net, before, doc.total, RoundingMode::HalfUp).value_or(0);
                const qint64 delta = after - prior;
                if (delta == 0) {
                    continue;
                }
                sum += delta;
                out << VirtualLine{line.account, line.party, line.klass, line.item, it.key(), app.date,
                                   delta > 0 ? delta : 0, delta < 0 ? -delta : 0};
            }
            if (sum != 0 && settleAccount != 0) {
                out << VirtualLine{settleAccount, settleParty, 0, 0, it.key(), app.date, sum < 0 ? -sum : 0,
                                   sum > 0 ? sum : 0};
            }
        }
    }
    return out;
}

} // namespace QindaOffice::Books
