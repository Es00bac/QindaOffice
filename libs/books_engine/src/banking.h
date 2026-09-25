// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Banking: account registers, transfers, deposits from Undeposited Funds,
// and the imported bank feed with rules and match suggestions
// (docs/books.md "Banking").
//
// AGENT-CONTRACT (bank feed sign): BankLine::amount is in minor units and
// signed from the ACCOUNT's point of view as a debit: positive = money
// into a bank account / a payment or refund onto a credit card, negative
// = money out / a charge. books_io's CSV/OFX/QIF parsers produce exactly
// this; the importer does not reinterpret it.
//
// AGENT-CONTRACT (dedup): a line with a FITID dedups on (account, FITID).
// Without one it dedups on (account, date, amount, normalised payee, n)
// where n counts identical tuples earlier in the same import — so two
// real €4.50 coffees on one day survive, and re-importing the same file
// adds nothing.

#include "books_types.h"
#include "journal.h"
#include "money.h"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

struct BankLine {
    QDate date;
    qint64 amount = 0; // minor units, + in / − out (see contract above)
    QString payee;
    QString memo;
    QString fitid;       // OFX FITID or a bank transaction id; may be empty
    QString checkNumber;
};

enum class FeedStatus { New = 0, Matched = 1, Categorized = 2, Excluded = 3 };

struct BankFeedEntry {
    qint64 id = 0;
    qint64 accountId = 0;
    QDate date;
    Money amount;
    QString payee;
    QString memo;
    QString fitid;
    QString checkNumber;
    FeedStatus status = FeedStatus::New;
    qint64 suggestedAccountId = 0; // from a bank rule
    qint64 suggestedPartyId = 0;
    qint64 suggestedClassId = 0;
    qint64 ruleId = 0;
    qint64 docId = 0;              // the document it was matched to / created
};

struct ImportSummary {
    int added = 0;
    int duplicates = 0;
    QList<qint64> ids;
};

enum class RuleDirection { Any = 0, MoneyIn = 1, MoneyOut = 2 };

struct BankRule {
    qint64 id = 0;
    QString name;
    int priority = 0;                 // higher wins
    QString payeeContains;            // case-insensitive, payee or memo
    std::optional<qint64> amountMin;  // on |amount|, minor units
    std::optional<qint64> amountMax;
    RuleDirection direction = RuleDirection::Any;
    qint64 bankAccountId = 0;         // 0 = every account
    qint64 accountId = 0;             // category to assign
    qint64 partyId = 0;
    qint64 classId = 0;
    QString memo;
    bool active = true;
};

struct MatchSuggestion {
    qint64 journalLineId = 0;
    qint64 docId = 0;
    QDate date;
    Money amount;
    QString description;
    int score = 0; // 0..100: amount equal (required), date and payee closeness
};

struct RegisterEntry {
    qint64 lineId = 0;
    qint64 txnId = 0;
    qint64 docId = 0;
    QDate date;
    DocKind kind = DocKind::JournalEntry;
    QString number;
    QString payee;
    QString memo;
    QString otherAccount; // the other side, or "-Split-"
    Money increase;       // debit for assets, credit for liabilities
    Money decrease;
    Money balance;        // running, in the account's normal sign
    int cleared = 0;      // 0 uncleared, 1 cleared, 2 reconciled
};

class Banking {
public:
    explicit Banking(CompanyFile &file);

    // ---- registers and money movement ----
    [[nodiscard]] QList<RegisterEntry> registerFor(qint64 accountId, const QDate &from = {},
                                                   const QDate &to = {}) const;
    Result transfer(qint64 fromAccountId, qint64 toAccountId, const Money &amount, const QDate &date,
                    const QString &memo = {}, const PostOptions &options = {});
    // Deposits payments sitting in Undeposited Funds (by payment/sales
    // receipt document id) into a bank account in one deposit.
    Result deposit(qint64 bankAccountId, const QDate &date, const QList<qint64> &paymentDocIds,
                   const QString &memo = {}, const PostOptions &options = {});
    // Payments and sales receipts still in Undeposited Funds.
    [[nodiscard]] QList<qint64> undepositedPayments() const;

    // ---- bank feed ----
    ImportSummary importLines(qint64 accountId, const QList<BankLine> &lines, const QString &batch = {});
    [[nodiscard]] QList<BankFeedEntry> feed(qint64 accountId, std::optional<FeedStatus> status = {}) const;
    [[nodiscard]] std::optional<BankFeedEntry> feedEntry(qint64 id) const;
    Result exclude(qint64 feedId);
    Result restore(qint64 feedId);
    // Creates an Expense (money out) or Deposit (money in) document for the
    // line against `categoryAccountId`, marks the new bank-side journal
    // line cleared, and the feed line categorized.
    Result categorize(qint64 feedId, qint64 categoryAccountId, qint64 partyId = 0, qint64 classId = 0,
                      const QString &memo = {}, const PostOptions &options = {});
    // Uncleared journal lines on the feed's account with the same signed
    // amount, dated within ±dayWindow, best first.
    [[nodiscard]] QList<MatchSuggestion> suggestMatches(qint64 feedId, int dayWindow = 10) const;
    // Marks the feed line matched to an existing journal line (clears it).
    Result match(qint64 feedId, qint64 journalLineId);

    // ---- rules ----
    Result createRule(const BankRule &rule);
    Result deleteRule(qint64 ruleId);
    [[nodiscard]] QList<BankRule> rules() const;
    // Fills suggestions on the account's New lines from the best matching
    // rule; returns how many lines a rule matched.
    int applyRules(qint64 accountId);
    // categorize() with the line's rule suggestion.
    Result acceptSuggestion(qint64 feedId, const PostOptions &options = {});

    [[nodiscard]] static QString dedupKey(const BankLine &line, int occurrence);
    // Lower-cased, punctuation and digits stripped, whitespace collapsed.
    [[nodiscard]] static QString normalizePayee(const QString &payee);
    // 0..100 token-overlap similarity of two payee strings.
    [[nodiscard]] static int payeeSimilarity(const QString &a, const QString &b);

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
