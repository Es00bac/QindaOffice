// SPDX-License-Identifier: GPL-3.0-or-later
#include "payout_matching.h"

#include "banking.h"
#include "company_file.h"
#include "sql_util.h"

#include <QSet>
#include <QSqlQuery>

namespace QindaOffice::Books::Connect {

namespace {

struct OpenPayoutLine {
    qint64 lineId = 0;
    qint64 accountId = 0;
    QDate date;
    qint64 amount = 0; // debit − credit (a deposit: positive)
};

QList<OpenPayoutLine> openPayoutLines(CompanyFile &file, qint64 bankAccountId)
{
    QList<OpenPayoutLine> out;
    QSqlQuery q(file.database());
    Sql::run(q, QStringLiteral("SELECT l.id, l.account_id, l.date, l.debit - l.credit FROM documents d"
                               " JOIN journal_lines l ON l.txn_id = d.txn_id AND l.account_id = d.to_account_id"
                               " WHERE d.kind = ? AND d.state = ? AND d.reference LIKE 'po!_%' ESCAPE '!'"
                               " AND l.voided = 0 AND l.cleared = 0 AND (? = 0 OR l.account_id = ?)"
                               " ORDER BY l.date, l.id"),
             {int(DocKind::Transfer), int(DocState::Open), bankAccountId, bankAccountId});
    while (q.next()) {
        out << OpenPayoutLine{q.value(0).toLongLong(), q.value(1).toLongLong(), Sql::fromIso(q.value(2)),
                              q.value(3).toLongLong()};
    }
    return out;
}

} // namespace

int matchStripePayouts(CompanyFile &file, qint64 bankAccountId)
{
    int matched = 0;
    QSet<qint64> used;
    Banking banking(file);
    for (const OpenPayoutLine &open : openPayoutLines(file, bankAccountId)) {
        qint64 best = 0;
        qint64 bestDays = kPayoutMatchDays + 1;
        {
            QSqlQuery q(file.database());
            Sql::run(q, QStringLiteral("SELECT id, date FROM bank_feed WHERE account_id = ? AND status = ?"
                                       " AND amount = ? AND date >= ? AND date <= ?"
                                       " AND (LOWER(COALESCE(payee, '')) LIKE '%stripe%'"
                                       "  OR LOWER(COALESCE(memo, '')) LIKE '%stripe%') ORDER BY date, id"),
                     {open.accountId, int(FeedStatus::New), open.amount,
                      Sql::isoDate(open.date.addDays(-kPayoutMatchDays)),
                      Sql::isoDate(open.date.addDays(kPayoutMatchDays))});
            while (q.next()) {
                const qint64 id = q.value(0).toLongLong();
                const qint64 days = qAbs(Sql::fromIso(q.value(1)).daysTo(open.date));
                if (!used.contains(id) && days < bestDays) {
                    best = id;
                    bestDays = days;
                }
            }
        }
        if (best != 0 && banking.match(best, open.lineId).ok) {
            used.insert(best);
            ++matched;
        }
    }
    return matched;
}

} // namespace QindaOffice::Books::Connect
