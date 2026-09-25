// SPDX-License-Identifier: GPL-3.0-or-later
// The imported bank feed: dedup on import, bank rules, match suggestions,
// categorize / match / exclude.
#include "banking.h"

#include "accounts.h"
#include "company_file.h"
#include "documents.h"
#include "purchases.h"
#include "sql_util.h"

#include <QCryptographicHash>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QSqlQuery>

#include <algorithm>

namespace QindaOffice::Books {

namespace {

const QString kFeedColumns = QStringLiteral(
    "id, account_id, date, amount, payee, memo, fitid, check_number, status, suggested_account_id,"
    " suggested_party_id, suggested_class_id, rule_id, doc_id");

BankFeedEntry readFeed(const QSqlQuery &q, const QString &currency)
{
    BankFeedEntry e;
    e.id = q.value(0).toLongLong();
    e.accountId = q.value(1).toLongLong();
    e.date = Sql::fromIso(q.value(2));
    e.amount = Money(q.value(3).toLongLong(), currency);
    e.payee = q.value(4).toString();
    e.memo = q.value(5).toString();
    e.fitid = q.value(6).toString();
    e.checkNumber = q.value(7).toString();
    e.status = FeedStatus(q.value(8).toInt());
    e.suggestedAccountId = q.value(9).toLongLong();
    e.suggestedPartyId = q.value(10).toLongLong();
    e.suggestedClassId = q.value(11).toLongLong();
    e.ruleId = q.value(12).toLongLong();
    e.docId = q.value(13).toLongLong();
    return e;
}

bool ruleMatches(const BankRule &rule, const BankFeedEntry &entry)
{
    if (!rule.active || (rule.bankAccountId != 0 && rule.bankAccountId != entry.accountId)) {
        return false;
    }
    const qint64 amount = entry.amount.minor();
    if ((rule.direction == RuleDirection::MoneyIn && amount <= 0)
        || (rule.direction == RuleDirection::MoneyOut && amount >= 0)) {
        return false;
    }
    const qint64 magnitude = amount < 0 ? -amount : amount;
    if ((rule.amountMin && magnitude < *rule.amountMin) || (rule.amountMax && magnitude > *rule.amountMax)) {
        return false;
    }
    if (!rule.payeeContains.isEmpty() && !entry.payee.contains(rule.payeeContains, Qt::CaseInsensitive)
        && !entry.memo.contains(rule.payeeContains, Qt::CaseInsensitive)) {
        return false;
    }
    return true;
}

} // namespace

QString Banking::normalizePayee(const QString &payee)
{
    static const QRegularExpression nonLetters(QStringLiteral("[^\\p{L}\\s]+"));
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    QString s = payee.toLower();
    s.replace(nonLetters, QStringLiteral(" "));
    s.replace(spaces, QStringLiteral(" "));
    return s.trimmed();
}

int Banking::payeeSimilarity(const QString &a, const QString &b)
{
    const QString x = normalizePayee(a);
    const QString y = normalizePayee(b);
    if (x.isEmpty() || y.isEmpty()) {
        return 0;
    }
    if (x == y || x.contains(y) || y.contains(x)) {
        return 100;
    }
    const QStringList xs = x.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QStringList ys = y.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QSet<QString> sx(xs.cbegin(), xs.cend());
    const QSet<QString> sy(ys.cbegin(), ys.cend());
    const qsizetype common = QSet<QString>(sx).intersect(sy).size();
    const qsizetype all = QSet<QString>(sx).unite(sy).size();
    return all == 0 ? 0 : int(common * 100 / all);
}

QString Banking::dedupKey(const BankLine &line, int occurrence)
{
    if (!line.fitid.trimmed().isEmpty()) {
        return QStringLiteral("F:") + line.fitid.trimmed();
    }
    const QByteArray basis = (Sql::isoDate(line.date) + QLatin1Char('|') + QString::number(line.amount)
                              + QLatin1Char('|') + normalizePayee(line.payee))
                                 .toUtf8();
    return QStringLiteral("H:") + QString::fromLatin1(QCryptographicHash::hash(basis, QCryptographicHash::Sha1).toHex())
        + QLatin1Char('#') + QString::number(occurrence);
}

ImportSummary Banking::importLines(qint64 accountId, const QList<BankLine> &lines, const QString &batch)
{
    ImportSummary summary;
    SqlTransaction txn(m_file);
    QHash<QString, int> seen; // occurrence counter per hash within this import
    for (const BankLine &line : lines) {
        if (!line.date.isValid()) {
            continue;
        }
        int occurrence = 0;
        if (line.fitid.trimmed().isEmpty()) {
            const QString base = dedupKey(line, 0);
            occurrence = seen.value(base, 0);
            seen.insert(base, occurrence + 1);
        }
        const QString key = dedupKey(line, occurrence);
        if (Sql::scalarInt(m_file.database(),
                           QStringLiteral("SELECT COUNT(*) FROM bank_feed WHERE account_id = ? AND dedup_key = ?"),
                           {accountId, key})
            > 0) {
            ++summary.duplicates;
            continue;
        }
        const qint64 id = Sql::insert(
            m_file.database(),
            QStringLiteral("INSERT INTO bank_feed(account_id, date, amount, payee, memo, fitid, check_number, dedup_key,"
                           " status, batch, imported_at) VALUES(?,?,?,?,?,?,?,?,0,?,?)"),
            {accountId, Sql::isoDate(line.date), line.amount, line.payee.trimmed(), line.memo.trimmed(),
             line.fitid.trimmed(), line.checkNumber, key, batch, CompanyFile::nowStamp()});
        if (id != 0) {
            ++summary.added;
            summary.ids << id;
        }
    }
    m_file.audit(QStringLiteral("import"), QStringLiteral("bank_feed"), accountId, {},
                 QStringLiteral("{\"added\":%1,\"duplicates\":%2,\"batch\":\"%3\"}")
                     .arg(summary.added)
                     .arg(summary.duplicates)
                     .arg(batch));
    txn.commit();
    return summary;
}

QList<BankFeedEntry> Banking::feed(qint64 accountId, std::optional<FeedStatus> status) const
{
    QList<BankFeedEntry> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT ") + kFeedColumns
                    + QStringLiteral(" FROM bank_feed WHERE account_id = ? AND (? < 0 OR status = ?) ORDER BY date, id"),
             {accountId, status ? int(*status) : -1, status ? int(*status) : -1});
    while (q.next()) {
        out << readFeed(q, m_file.currency());
    }
    return out;
}

std::optional<BankFeedEntry> Banking::feedEntry(qint64 id) const
{
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT ") + kFeedColumns + QStringLiteral(" FROM bank_feed WHERE id = ?"), {id});
    if (!q.next()) {
        return std::nullopt;
    }
    return readFeed(q, m_file.currency());
}

Result Banking::exclude(qint64 feedId)
{
    const std::optional<BankFeedEntry> e = feedEntry(feedId);
    if (!e || e->status != FeedStatus::New) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("only new lines can be excluded"));
    }
    Sql::exec(m_file.database(), QStringLiteral("UPDATE bank_feed SET status = ? WHERE id = ?"),
              {int(FeedStatus::Excluded), feedId});
    emit m_file.changed();
    return Result::success(feedId);
}

Result Banking::restore(qint64 feedId)
{
    const std::optional<BankFeedEntry> e = feedEntry(feedId);
    if (!e || e->status != FeedStatus::Excluded) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("only excluded lines can be restored"));
    }
    Sql::exec(m_file.database(), QStringLiteral("UPDATE bank_feed SET status = ? WHERE id = ?"),
              {int(FeedStatus::New), feedId});
    emit m_file.changed();
    return Result::success(feedId);
}

Result Banking::categorize(qint64 feedId, qint64 categoryAccountId, qint64 partyId, qint64 classId,
                           const QString &memo, const PostOptions &options)
{
    const std::optional<BankFeedEntry> e = feedEntry(feedId);
    if (!e || e->status != FeedStatus::New) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("only new lines can be categorized"));
    }
    ChartOfAccounts chart(m_file);
    const std::optional<Account> bank = chart.account(e->accountId);
    if (!bank || !chart.account(categoryAccountId)) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("choose a category account"));
    }
    const QString text = memo.isEmpty() ? (e->memo.isEmpty() ? e->payee : e->payee + QStringLiteral(" — ") + e->memo)
                                        : memo;
    SqlTransaction txn(m_file);
    BusinessDocument doc;
    doc.date = e->date;
    doc.partyId = partyId;
    doc.memo = text;
    doc.reference = e->checkNumber;
    doc.accountId = e->accountId;
    DocumentLine line;
    line.accountId = categoryAccountId;
    line.classId = classId;
    line.description = text;
    line.amount = e->amount.abs();
    doc.lines << line;
    Result created;
    if (e->amount.isNegative()) {
        doc.kind = bank->type == AccountType::CreditCard ? DocKind::CreditCardCharge
                   : e->checkNumber.isEmpty()            ? DocKind::Expense
                                                         : DocKind::Check;
        created = Purchases(m_file).createExpense(doc, options);
    } else {
        doc.kind = DocKind::Deposit;
        doc.total = doc.subtotal = e->amount;
        Documents documents(m_file);
        created = documents.write(doc);
        if (created.ok) {
            const QList<PostingLine> lines{
                PostingLine{e->accountId, e->amount.minor(), 0, partyId, 0, 0, 0, 0, text},
                PostingLine{categoryAccountId, 0, e->amount.minor(), partyId, classId, 0, 0, 0, text},
            };
            const Result posted = Ledger(m_file).postLines(doc.id, doc.kind, doc.date, {}, text, lines, options);
            created = posted.ok ? Result::success(doc.id) : posted;
        }
    }
    if (!created.ok) {
        return created;
    }
    Sql::exec(m_file.database(),
              QStringLiteral("UPDATE journal_lines SET cleared = 1 WHERE account_id = ? AND txn_id ="
                             " (SELECT txn_id FROM documents WHERE id = ?)"),
              {e->accountId, created.id});
    Sql::exec(m_file.database(), QStringLiteral("UPDATE bank_feed SET status = ?, doc_id = ? WHERE id = ?"),
              {int(FeedStatus::Categorized), created.id, feedId});
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return created;
}

QList<MatchSuggestion> Banking::suggestMatches(qint64 feedId, int dayWindow) const
{
    const std::optional<BankFeedEntry> e = feedEntry(feedId);
    if (!e) {
        return {};
    }
    QList<MatchSuggestion> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT l.id, t.doc_id, l.date, l.debit - l.credit, COALESCE(p.display_name, ''),"
                               " COALESCE(t.memo, ''), COALESCE(t.number, ''), t.kind FROM journal_lines l"
                               " JOIN transactions t ON t.id = l.txn_id LEFT JOIN parties p ON p.id = l.party_id"
                               " WHERE l.account_id = ? AND l.voided = 0 AND l.cleared = 0"
                               " AND l.debit - l.credit = ? AND l.date >= ? AND l.date <= ?"),
             {e->accountId, e->amount.minor(), Sql::isoDate(e->date.addDays(-dayWindow)),
              Sql::isoDate(e->date.addDays(dayWindow))});
    while (q.next()) {
        MatchSuggestion s;
        s.journalLineId = q.value(0).toLongLong();
        s.docId = q.value(1).toLongLong();
        s.date = Sql::fromIso(q.value(2));
        s.amount = m_file.money(q.value(3).toLongLong());
        const QString payee = q.value(4).toString();
        const QString memo = q.value(5).toString();
        s.description = QStringLiteral("%1 %2 %3").arg(docKindName(DocKind(q.value(7).toInt())), q.value(6).toString(),
                                                       payee.isEmpty() ? memo : payee).simplified();
        const qint64 days = qAbs(s.date.daysTo(e->date));
        const int dateScore = dayWindow <= 0 ? 30 : int(30 - (30 * days) / (dayWindow + 1));
        const int payeeScore = qMax(payeeSimilarity(payee, e->payee), payeeSimilarity(memo, e->payee)) / 5;
        s.score = 50 + dateScore + payeeScore;
        out << s;
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const MatchSuggestion &a, const MatchSuggestion &b) { return a.score > b.score; });
    return out;
}

Result Banking::match(qint64 feedId, qint64 journalLineId)
{
    const std::optional<BankFeedEntry> e = feedEntry(feedId);
    if (!e || e->status != FeedStatus::New) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("only new lines can be matched"));
    }
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT l.account_id, l.debit - l.credit, l.cleared, l.voided, t.doc_id"
                               " FROM journal_lines l JOIN transactions t ON t.id = l.txn_id WHERE l.id = ?"),
             {journalLineId});
    if (!q.next() || q.value(0).toLongLong() != e->accountId || q.value(1).toLongLong() != e->amount.minor()
        || q.value(2).toInt() != 0 || q.value(3).toBool()) {
        return Result::failure(ErrorCode::Invalid,
                               QStringLiteral("a match must be an uncleared line of the same account and amount"));
    }
    const qint64 docId = q.value(4).toLongLong();
    q.finish();
    SqlTransaction txn(m_file);
    Sql::exec(m_file.database(), QStringLiteral("UPDATE journal_lines SET cleared = 1 WHERE id = ?"), {journalLineId});
    Sql::exec(m_file.database(), QStringLiteral("UPDATE bank_feed SET status = ?, doc_id = ? WHERE id = ?"),
              {int(FeedStatus::Matched), docId, feedId});
    m_file.audit(QStringLiteral("match"), QStringLiteral("bank_feed"), feedId, {},
                 QStringLiteral("{\"journal_line\":%1}").arg(journalLineId));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(docId);
}

Result Banking::createRule(const BankRule &rule)
{
    if (rule.name.trimmed().isEmpty() || !ChartOfAccounts(m_file).account(rule.accountId)) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a rule needs a name and a category account"));
    }
    QString error;
    const qint64 id = Sql::insert(
        m_file.database(),
        QStringLiteral("INSERT INTO bank_rules(name, priority, payee_contains, amount_min, amount_max, direction,"
                       " bank_account_id, account_id, party_id, class_id, memo, active) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"),
        {rule.name.trimmed(), rule.priority, rule.payeeContains,
         rule.amountMin ? QVariant(*rule.amountMin) : QVariant(), rule.amountMax ? QVariant(*rule.amountMax) : QVariant(),
         int(rule.direction), Sql::idOrNull(rule.bankAccountId), rule.accountId, Sql::idOrNull(rule.partyId),
         Sql::idOrNull(rule.classId), rule.memo, rule.active ? 1 : 0},
        &error);
    if (id == 0) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("bank_rule"), id, {}, rule.name);
    emit m_file.changed();
    return Result::success(id);
}

Result Banking::deleteRule(qint64 ruleId)
{
    Sql::exec(m_file.database(), QStringLiteral("DELETE FROM bank_rules WHERE id = ?"), {ruleId});
    m_file.audit(QStringLiteral("delete"), QStringLiteral("bank_rule"), ruleId, {}, {});
    emit m_file.changed();
    return Result::success(ruleId);
}

QList<BankRule> Banking::rules() const
{
    QList<BankRule> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT id, name, priority, payee_contains, amount_min, amount_max, direction,"
                               " bank_account_id, account_id, party_id, class_id, memo, active FROM bank_rules"
                               " ORDER BY priority DESC, id"));
    while (q.next()) {
        BankRule r;
        r.id = q.value(0).toLongLong();
        r.name = q.value(1).toString();
        r.priority = q.value(2).toInt();
        r.payeeContains = q.value(3).toString();
        if (!q.value(4).isNull()) {
            r.amountMin = q.value(4).toLongLong();
        }
        if (!q.value(5).isNull()) {
            r.amountMax = q.value(5).toLongLong();
        }
        r.direction = RuleDirection(q.value(6).toInt());
        r.bankAccountId = q.value(7).toLongLong();
        r.accountId = q.value(8).toLongLong();
        r.partyId = q.value(9).toLongLong();
        r.classId = q.value(10).toLongLong();
        r.memo = q.value(11).toString();
        r.active = q.value(12).toBool();
        out << r;
    }
    return out;
}

int Banking::applyRules(qint64 accountId)
{
    const QList<BankRule> ordered = rules(); // priority DESC, then id
    int matched = 0;
    SqlTransaction txn(m_file);
    for (const BankFeedEntry &entry : feed(accountId, FeedStatus::New)) {
        const auto it = std::find_if(ordered.cbegin(), ordered.cend(),
                                     [&entry](const BankRule &r) { return ruleMatches(r, entry); });
        if (it == ordered.cend()) {
            continue;
        }
        Sql::exec(m_file.database(),
                  QStringLiteral("UPDATE bank_feed SET suggested_account_id = ?, suggested_party_id = ?,"
                                 " suggested_class_id = ?, rule_id = ? WHERE id = ?"),
                  {it->accountId, Sql::idOrNull(it->partyId), Sql::idOrNull(it->classId), it->id, entry.id});
        ++matched;
    }
    txn.commit();
    return matched;
}

Result Banking::acceptSuggestion(qint64 feedId, const PostOptions &options)
{
    const std::optional<BankFeedEntry> e = feedEntry(feedId);
    if (!e || e->suggestedAccountId == 0) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("no rule suggestion for this line"));
    }
    return categorize(feedId, e->suggestedAccountId, e->suggestedPartyId, e->suggestedClassId, {}, options);
}

} // namespace QindaOffice::Books
