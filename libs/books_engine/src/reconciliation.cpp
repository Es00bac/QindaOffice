// SPDX-License-Identifier: GPL-3.0-or-later
#include "reconciliation.h"

#include "accounts.h"
#include "company_file.h"
#include "sql_util.h"

#include <QSqlQuery>

namespace QindaOffice::Books {

namespace {

bool debitNormal(CompanyFile &file, qint64 accountId)
{
    const std::optional<Account> a = ChartOfAccounts(file).account(accountId);
    return !a || accountTypeInfo(a->type).normalBalance == NormalBalance::Debit;
}

} // namespace

Reconciliation::Reconciliation(CompanyFile &file)
    : m_file(file)
{
}

std::optional<ReconciliationSession> Reconciliation::session(qint64 id) const
{
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT id, account_id, statement_date, beginning_balance, ending_balance, state,"
                               " finished_at FROM reconciliations WHERE id = ?"),
             {id});
    if (!q.next()) {
        return std::nullopt;
    }
    return ReconciliationSession{q.value(0).toLongLong(), q.value(1).toLongLong(), Sql::fromIso(q.value(2)),
                                 m_file.money(q.value(3).toLongLong()), m_file.money(q.value(4).toLongLong()),
                                 ReconciliationState(q.value(5).toInt()), q.value(6).toString()};
}

std::optional<ReconciliationSession> Reconciliation::inProgress(qint64 accountId) const
{
    const qint64 id = Sql::scalarInt(m_file.database(),
                                     QStringLiteral("SELECT id FROM reconciliations WHERE account_id = ? AND state = 0"),
                                     {accountId});
    return id == 0 ? std::nullopt : session(id);
}

QList<ReconciliationSession> Reconciliation::history(qint64 accountId) const
{
    QList<qint64> ids;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT id FROM reconciliations WHERE account_id = ? ORDER BY id"), {accountId});
        while (q.next()) {
            ids << q.value(0).toLongLong();
        }
    }
    QList<ReconciliationSession> out;
    for (qint64 id : ids) {
        if (const std::optional<ReconciliationSession> s = session(id)) {
            out << *s;
        }
    }
    return out;
}

Result Reconciliation::start(qint64 accountId, const QDate &statementDate, const Money &endingBalance)
{
    const std::optional<Account> account = ChartOfAccounts(m_file).account(accountId);
    if (!account || !accountTypeInfo(account->type).isBalanceSheet()) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("reconcile a bank or credit card account"));
    }
    if (inProgress(accountId)) {
        return Result::failure(ErrorCode::Duplicate, QStringLiteral("a reconciliation of this account is in progress"));
    }
    if (!statementDate.isValid()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a statement date is required"));
    }
    const qint64 raw = Sql::scalarInt(m_file.database(),
                                      QStringLiteral("SELECT COALESCE(SUM(debit) - SUM(credit), 0) FROM journal_lines"
                                                     " WHERE account_id = ? AND voided = 0 AND cleared = 2"),
                                      {accountId});
    const qint64 beginning = debitNormal(m_file, accountId) ? raw : -raw;
    QString error;
    const qint64 id = Sql::insert(
        m_file.database(),
        QStringLiteral("INSERT INTO reconciliations(account_id, statement_date, ending_balance, beginning_balance,"
                       " state, started_at) VALUES(?,?,?,?,0,?)"),
        {accountId, Sql::isoDate(statementDate), endingBalance.minor(), beginning, CompanyFile::nowStamp()}, &error);
    if (id == 0) {
        return Result::failure(ErrorCode::Storage, error);
    }
    emit m_file.changed();
    return Result::success(id);
}

QList<ReconcileCandidate> Reconciliation::candidates(qint64 sessionId) const
{
    const std::optional<ReconciliationSession> s = session(sessionId);
    if (!s) {
        return {};
    }
    const bool normalDebit = debitNormal(m_file, s->accountId);
    QList<ReconcileCandidate> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT l.id, t.doc_id, l.date, t.kind, t.number, COALESCE(p.display_name, ''), l.memo,"
                               " l.debit - l.credit, EXISTS(SELECT 1 FROM reconciliation_marks m"
                               " WHERE m.reconciliation_id = ? AND m.line_id = l.id)"
                               " FROM journal_lines l JOIN transactions t ON t.id = l.txn_id"
                               " LEFT JOIN parties p ON p.id = l.party_id"
                               " WHERE l.account_id = ? AND l.voided = 0 AND l.cleared <> 2 AND l.date <= ?"
                               " ORDER BY l.date, l.id"),
             {sessionId, s->accountId, Sql::isoDate(s->statementDate)});
    while (q.next()) {
        ReconcileCandidate c;
        c.lineId = q.value(0).toLongLong();
        c.docId = q.value(1).toLongLong();
        c.date = Sql::fromIso(q.value(2));
        c.kind = DocKind(q.value(3).toInt());
        c.number = q.value(4).toString();
        c.payee = q.value(5).toString();
        c.memo = q.value(6).toString();
        const qint64 raw = q.value(7).toLongLong();
        c.amount = m_file.money(normalDebit ? raw : -raw);
        c.marked = q.value(8).toBool();
        out << c;
    }
    return out;
}

Result Reconciliation::mark(qint64 sessionId, const QList<qint64> &lineIds, bool marked)
{
    const std::optional<ReconciliationSession> s = session(sessionId);
    if (!s || s->state != ReconciliationState::InProgress) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("no reconciliation in progress"));
    }
    QList<qint64> allowed;
    for (const ReconcileCandidate &c : candidates(sessionId)) {
        allowed << c.lineId;
    }
    SqlTransaction txn(m_file);
    for (qint64 line : lineIds) {
        if (!allowed.contains(line)) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("line %1 cannot be reconciled here").arg(line));
        }
        Sql::exec(m_file.database(),
                  marked ? QStringLiteral("INSERT OR IGNORE INTO reconciliation_marks(reconciliation_id, line_id) VALUES(?, ?)")
                         : QStringLiteral("DELETE FROM reconciliation_marks WHERE reconciliation_id = ? AND line_id = ?"),
                  {sessionId, line});
        Sql::exec(m_file.database(), QStringLiteral("UPDATE journal_lines SET cleared = ? WHERE id = ?"),
                  {marked ? 1 : 0, line});
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(sessionId);
}

Money Reconciliation::clearedBalance(qint64 sessionId) const
{
    const std::optional<ReconciliationSession> s = session(sessionId);
    if (!s) {
        return m_file.money(0);
    }
    const qint64 raw = Sql::scalarInt(m_file.database(),
                                      QStringLiteral("SELECT COALESCE(SUM(l.debit) - SUM(l.credit), 0)"
                                                     " FROM reconciliation_marks m JOIN journal_lines l ON l.id = m.line_id"
                                                     " WHERE m.reconciliation_id = ? AND l.voided = 0"),
                                      {sessionId});
    return s->beginningBalance + m_file.money(debitNormal(m_file, s->accountId) ? raw : -raw);
}

Money Reconciliation::difference(qint64 sessionId) const
{
    const std::optional<ReconciliationSession> s = session(sessionId);
    return s ? s->endingBalance - clearedBalance(sessionId) : m_file.money(0);
}

Result Reconciliation::finish(qint64 sessionId)
{
    const std::optional<ReconciliationSession> s = session(sessionId);
    if (!s || s->state != ReconciliationState::InProgress) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("no reconciliation in progress"));
    }
    const Money diff = difference(sessionId);
    if (!diff.isZero()) {
        return Result::failure(ErrorCode::NotZero,
                               QStringLiteral("the difference is %1; it must be zero to finish").arg(diff.toPlainString()));
    }
    SqlTransaction txn(m_file);
    Sql::exec(m_file.database(),
              QStringLiteral("UPDATE journal_lines SET cleared = 2, reconciliation_id = ? WHERE id IN"
                             " (SELECT line_id FROM reconciliation_marks WHERE reconciliation_id = ?)"),
              {sessionId, sessionId});
    Sql::exec(m_file.database(), QStringLiteral("UPDATE reconciliations SET state = 1, finished_at = ? WHERE id = ?"),
              {CompanyFile::nowStamp(), sessionId});
    m_file.audit(QStringLiteral("reconcile"), QStringLiteral("account"), s->accountId, {},
                 QStringLiteral("{\"session\":%1,\"statement_date\":\"%2\",\"ending\":\"%3\"}")
                     .arg(sessionId)
                     .arg(Sql::isoDate(s->statementDate), s->endingBalance.toPlainString()));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(sessionId);
}

Result Reconciliation::cancel(qint64 sessionId)
{
    const std::optional<ReconciliationSession> s = session(sessionId);
    if (!s || s->state != ReconciliationState::InProgress) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("no reconciliation in progress"));
    }
    SqlTransaction txn(m_file);
    Sql::exec(m_file.database(),
              QStringLiteral("UPDATE journal_lines SET cleared = 0 WHERE id IN"
                             " (SELECT line_id FROM reconciliation_marks WHERE reconciliation_id = ?)"),
              {sessionId});
    Sql::exec(m_file.database(), QStringLiteral("DELETE FROM reconciliation_marks WHERE reconciliation_id = ?"),
              {sessionId});
    Sql::exec(m_file.database(), QStringLiteral("DELETE FROM reconciliations WHERE id = ?"), {sessionId});
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(sessionId);
}

Result Reconciliation::undoLast(qint64 accountId)
{
    if (inProgress(accountId)) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("finish or cancel the reconciliation in progress first"));
    }
    const qint64 last = Sql::scalarInt(m_file.database(),
                                       QStringLiteral("SELECT MAX(id) FROM reconciliations WHERE account_id = ? AND state = 1"),
                                       {accountId});
    if (last == 0) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("nothing to undo"));
    }
    SqlTransaction txn(m_file);
    Sql::exec(m_file.database(),
              QStringLiteral("UPDATE journal_lines SET cleared = 1, reconciliation_id = NULL WHERE reconciliation_id = ?"),
              {last});
    Sql::exec(m_file.database(), QStringLiteral("UPDATE reconciliations SET state = 2 WHERE id = ?"), {last});
    m_file.audit(QStringLiteral("undo-reconcile"), QStringLiteral("account"), accountId, {},
                 QStringLiteral("{\"session\":%1}").arg(last));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(last);
}

ReportTable Reconciliation::report(qint64 sessionId) const
{
    ReportTable t;
    t.id = QStringLiteral("reconciliation");
    t.title = QStringLiteral("Reconciliation Report");
    t.company = m_file.settings().displayName();
    t.currency = m_file.currency();
    const std::optional<ReconciliationSession> s = session(sessionId);
    if (!s) {
        return t;
    }
    const std::optional<Account> account = ChartOfAccounts(m_file).account(s->accountId);
    t.subtitle = QStringLiteral("%1 · statement ending %2")
                     .arg(account ? account->fullName : QString(), s->statementDate.toString(Qt::ISODate));
    t.columns = {{QStringLiteral("date"), QStringLiteral("Date"), ColumnType::Date},
                 {QStringLiteral("type"), QStringLiteral("Type"), ColumnType::Text},
                 {QStringLiteral("number"), QStringLiteral("Num"), ColumnType::Text},
                 {QStringLiteral("name"), QStringLiteral("Name"), ColumnType::Text},
                 {QStringLiteral("amount"), QStringLiteral("Amount"), ColumnType::Money}};
    auto summary = [&t](RowKind kind, const QString &key, const QString &label, qint64 amount) {
        ReportRow r;
        r.kind = kind;
        r.key = key;
        r.cells = {ReportCell::blank(), ReportCell::ofText(label), ReportCell::blank(), ReportCell::blank(),
                   ReportCell::ofMoney(amount)};
        t.rows << r;
    };
    // Lines of this session: marked (in progress) or stamped (finished).
    QList<ReconcileCandidate> cleared;
    QList<ReconcileCandidate> uncleared;
    const bool normalDebit = debitNormal(m_file, s->accountId);
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT l.id, t.doc_id, l.date, t.kind, t.number, COALESCE(p.display_name, ''),"
                                   " l.debit - l.credit, (COALESCE(l.reconciliation_id, 0) = ? OR EXISTS(SELECT 1 FROM"
                                   " reconciliation_marks m WHERE m.reconciliation_id = ? AND m.line_id = l.id)),"
                                   " l.cleared FROM journal_lines l JOIN transactions t ON t.id = l.txn_id"
                                   " LEFT JOIN parties p ON p.id = l.party_id WHERE l.account_id = ? AND l.voided = 0"
                                   " AND l.date <= ? ORDER BY l.date, l.id"),
                 {sessionId, sessionId, s->accountId, Sql::isoDate(s->statementDate)});
        while (q.next()) {
            ReconcileCandidate c;
            c.lineId = q.value(0).toLongLong();
            c.docId = q.value(1).toLongLong();
            c.date = Sql::fromIso(q.value(2));
            c.kind = DocKind(q.value(3).toInt());
            c.number = q.value(4).toString();
            c.payee = q.value(5).toString();
            c.amount = m_file.money(normalDebit ? q.value(6).toLongLong() : -q.value(6).toLongLong());
            if (q.value(7).toBool()) {
                cleared << c;
            } else if (q.value(8).toInt() != 2) {
                uncleared << c;
            }
        }
    }
    auto details = [&t](const QList<ReconcileCandidate> &lines, bool increases) {
        qint64 sum = 0;
        for (const ReconcileCandidate &c : lines) {
            if (c.amount.isNegative() == increases) {
                continue;
            }
            ReportRow r;
            r.kind = RowKind::Detail;
            r.depth = 1;
            r.key = QStringLiteral("line:%1").arg(c.lineId);
            r.cells = {ReportCell::ofDate(c.date), ReportCell::ofText(docKindName(c.kind)), ReportCell::ofText(c.number),
                       ReportCell::ofText(c.payee), ReportCell::ofMoney(c.amount.minor())};
            r.drill.documentIds = {c.docId};
            t.rows << r;
            sum += c.amount.minor();
        }
        return sum;
    };
    summary(RowKind::Total, QStringLiteral("beginning"), QStringLiteral("Beginning balance"), s->beginningBalance.minor());
    summary(RowKind::Section, QStringLiteral("section:cleared_in"), QStringLiteral("Cleared deposits and other increases"), 0);
    t.rows.last().cells[4] = ReportCell::blank();
    const qint64 in = details(cleared, true);
    summary(RowKind::Subtotal, QStringLiteral("cleared_in"), QStringLiteral("Total cleared increases"), in);
    summary(RowKind::Section, QStringLiteral("section:cleared_out"), QStringLiteral("Cleared payments and other decreases"), 0);
    t.rows.last().cells[4] = ReportCell::blank();
    const qint64 out = details(cleared, false);
    summary(RowKind::Subtotal, QStringLiteral("cleared_out"), QStringLiteral("Total cleared decreases"), out);
    const qint64 clearedBalance = s->beginningBalance.minor() + in + out;
    summary(RowKind::Total, QStringLiteral("cleared_balance"), QStringLiteral("Cleared balance"), clearedBalance);
    summary(RowKind::Total, QStringLiteral("statement_ending"), QStringLiteral("Statement ending balance"),
            s->endingBalance.minor());
    summary(RowKind::Total, QStringLiteral("difference"), QStringLiteral("Difference"),
            s->endingBalance.minor() - clearedBalance);
    summary(RowKind::Section, QStringLiteral("section:uncleared"), QStringLiteral("Uncleared as of statement date"), 0);
    t.rows.last().cells[4] = ReportCell::blank();
    const qint64 unclearedIn = details(uncleared, true);
    const qint64 unclearedOut = details(uncleared, false);
    summary(RowKind::Total, QStringLiteral("register_balance"), QStringLiteral("Register balance as of statement date"),
            clearedBalance + unclearedIn + unclearedOut);
    return t;
}

} // namespace QindaOffice::Books
