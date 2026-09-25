// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// LedgerQuery: the one aggregation primitive every report is built from —
// Σ debit and Σ credit of live journal lines, grouped by account / month /
// class / party / item, on either basis.
//
// Accrual: one SQL GROUP BY over journal_lines (indexed on date and
// account; the 50k-line gate in tst_books_perf).
//
// Cash (docs/books.md "Cash vs accrual"): invoices, credit memos, bills
// and vendor credits are taken OUT of the ledger and put back when money
// settles them. Every application (payment → invoice, credit → invoice,
// bill payment → bill …) recognises the settled fraction of the document's
// own lines on the application date:
//   recognised(line, to date) = round(line × Σ applied ÷ document total)
// computed cumulatively (so a fully paid document recognises exactly its
// lines, never a cent more or less), with the A/R or A/P line set to the
// negative sum of the others (so every recognition balances).
// AGENT-GUARD: reports must go through this class for their numbers; a
// report that queries journal_lines directly silently ignores the basis.

#include "books_types.h"

#include <QDate>
#include <QHash>
#include <QList>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

enum LedgerGroup : int {
    GroupAccount = 1,
    GroupMonth = 2,
    GroupClass = 4,
    GroupParty = 8,
    GroupItem = 16,
};

struct LedgerFilter {
    QDate from; // invalid = from the beginning
    QDate to;   // invalid = to the end
    Basis basis = Basis::Accrual;
    std::optional<qint64> classId; // 0 = unclassified only
};

struct LedgerSum {
    qint64 accountId = 0;
    int month = -1; // monthIndex() when grouped by month
    qint64 classId = 0;
    qint64 partyId = 0;
    qint64 itemId = 0;
    qint64 debit = 0;
    qint64 credit = 0;
    [[nodiscard]] qint64 net() const { return debit - credit; }
};

struct VirtualLine {
    qint64 accountId = 0;
    qint64 partyId = 0;
    qint64 classId = 0;
    qint64 itemId = 0;
    qint64 docId = 0;
    QDate date;
    qint64 debit = 0;
    qint64 credit = 0;
};

class LedgerQuery {
public:
    explicit LedgerQuery(CompanyFile &file);

    [[nodiscard]] QList<LedgerSum> aggregate(const LedgerFilter &filter, int groups) const;
    // debit − credit per account.
    [[nodiscard]] QHash<qint64, qint64> netByAccount(const LedgerFilter &filter) const;
    // The cash-basis recognition lines dated within [from, to].
    [[nodiscard]] QList<VirtualLine> cashRecognition(const QDate &from, const QDate &to) const;

    [[nodiscard]] static int monthIndex(const QDate &date) { return date.year() * 12 + date.month() - 1; }
    [[nodiscard]] static QDate monthStart(int index) { return QDate(index / 12, index % 12 + 1, 1); }

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
