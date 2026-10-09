// SPDX-License-Identifier: GPL-3.0-or-later
// Owner draws, estimated-tax payments, set-asides and shareholder
// distributions (owner_pay.h).
#include "owner_pay.h"

#include "accounts.h"
#include "banking.h"
#include "company_file.h"
#include "documents.h"
#include "lists.h"
#include "payroll_records.h"
#include "purchases.h"
#include "sql_util.h"

#include <QSqlQuery>

namespace QindaOffice::Books {

using namespace Payroll;

namespace {

const QStringList kKindKeys{QStringLiteral("draw"), QStringLiteral("federalEstimate"), QStringLiteral("coloradoEstimate"),
                            QStringLiteral("setAside"), QStringLiteral("distribution")};

QString defaultMemo(const OwnerPaymentRequest &r, const QString &owner)
{
    switch (r.kind) {
    case OwnerPaymentKind::Draw:
        return QStringLiteral("Owner draw: %1").arg(owner);
    case OwnerPaymentKind::Distribution:
        return QStringLiteral("Shareholder distribution: %1").arg(owner);
    case OwnerPaymentKind::FederalEstimate:
        return QStringLiteral("Federal estimated tax %1, payment %2 of 4 (Form 1040-ES)").arg(r.taxYear).arg(r.installment);
    case OwnerPaymentKind::ColoradoEstimate:
        return QStringLiteral("Colorado estimated tax %1, payment %2 of 4 (DR 0104EP)").arg(r.taxYear).arg(r.installment);
    case OwnerPaymentKind::SetAside:
        return QStringLiteral("Tax set-aside");
    }
    return {};
}

} // namespace

QString ownerPaymentKindKey(OwnerPaymentKind kind)
{
    return kKindKeys.value(int(kind));
}

std::optional<OwnerPaymentKind> ownerPaymentKindFromKey(const QString &key)
{
    const qsizetype i = kKindKeys.indexOf(key);
    return i < 0 ? std::nullopt : std::optional<OwnerPaymentKind>(OwnerPaymentKind(i));
}

OwnerPay::OwnerPay(CompanyFile &file)
    : m_file(file)
{
}

Result OwnerPay::record(const OwnerPaymentRequest &request, const PostOptions &options)
{
    PayrollRecords records(m_file);
    PayrollSetup setup = records.setup();
    const bool estimate = request.kind == OwnerPaymentKind::FederalEstimate
        || request.kind == OwnerPaymentKind::ColoradoEstimate;
    if (request.kind == OwnerPaymentKind::Draw && setup.entity == EntityTax::SCorporation) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("an S corporation pays its owner wages through payroll "
                                                                  "and shareholder distributions, not draws"));
    }
    // Shareholder distributions are an S corporation's; a Schedule C or
    // partnership owner's money out is a draw ("not sure" keeps both).
    if (request.kind == OwnerPaymentKind::Distribution
        && (setup.entity == EntityTax::ScheduleC || setup.entity == EntityTax::Partnership)) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("only an S corporation pays shareholder distributions; "
                                                                  "a Schedule C or partnership owner takes draws"));
    }
    if (!request.amount.isPositive() || !request.date.isValid()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a payment needs a date and an amount above zero"));
    }
    if (estimate && (request.taxYear <= 0 || request.installment < 1 || request.installment > 4)) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("say which year's installment (1 to 4) this pays"));
    }
    const std::optional<Party> owner = Lists(m_file).party(request.ownerId);
    if (request.ownerId != 0 && (!owner || owner->kind != PartyKind::Owner)) {
        return Result::failure(ErrorCode::UnknownParty, QStringLiteral("choose an owner"));
    }
    const bool check = !estimate && request.kind != OwnerPaymentKind::SetAside
        && request.method == OwnerPaymentRequest::Method::Check;
    if (check && !owner) {
        return Result::failure(ErrorCode::UnknownParty, QStringLiteral("a check needs the owner to pay"));
    }
    const QString equityRole = request.kind == OwnerPaymentKind::Distribution ? PayrollRole::Distributions
                                                                             : PayrollRole::OwnerDraws;
    if (request.kind != OwnerPaymentKind::SetAside && !ChartOfAccounts(m_file).account(setup.account(equityRole))) {
        if (const Result made = records.ensurePayrollAccounts(); !made.ok) {
            return made;
        }
        setup = records.setup();
    }
    const qint64 bankId = request.bankAccountId != 0 ? request.bankAccountId : setup.payrollBankAccountId;
    const std::optional<Account> bank = ChartOfAccounts(m_file).account(bankId);
    if (!bank || bank->type != AccountType::Bank) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("choose the bank account the money comes from"));
    }
    const QString memo = request.memo.isEmpty() ? defaultMemo(request, owner ? owner->displayName : QString())
                                                : request.memo;
    SqlTransaction txn(m_file);
    Result made;
    if (request.kind == OwnerPaymentKind::SetAside) {
        const qint64 to = request.toAccountId != 0 ? request.toAccountId : setup.setAsideAccountId;
        if (!ChartOfAccounts(m_file).account(to) || to == bankId) {
            return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("choose the account the set-aside goes to"));
        }
        made = Banking(m_file).transfer(bankId, to, request.amount, request.date, memo, options, request.reference);
    } else if (check || estimate) {
        BusinessDocument doc;
        doc.kind = check ? DocKind::Check : DocKind::Expense;
        doc.partyId = request.ownerId;
        doc.date = request.date;
        doc.accountId = bankId;
        doc.number = check && !request.toPrint ? request.checkNumber.trimmed() : QString();
        doc.toPrint = check && request.toPrint;
        doc.reference = request.reference.trimmed();
        doc.memo = memo;
        doc.message = check ? memo : QString(); // the memo printed on the check
        DocumentLine line;
        line.accountId = setup.account(equityRole);
        line.description = memo;
        line.amount = request.amount;
        line.taxableFromItem = false;
        doc.lines << line;
        made = Purchases(m_file).createExpense(doc, options);
    } else {
        made = Banking(m_file).transfer(bankId, setup.account(equityRole), request.amount, request.date, memo, options,
                                        request.reference);
    }
    if (!made.ok) {
        return made;
    }
    QString error;
    if (!Sql::exec(m_file.database(),
                   QStringLiteral("INSERT INTO owner_payments(doc_id, kind, owner_id, tax_year, quarter) VALUES(?, ?, ?, ?, ?)"),
                   {made.id, ownerPaymentKindKey(request.kind), Sql::idOrNull(request.ownerId),
                    estimate ? QVariant(request.taxYear) : QVariant(), estimate ? QVariant(request.installment) : QVariant()},
                   &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(made.id);
}

Result OwnerPay::voidPayment(qint64 docId, const QString &reason, const PostOptions &options)
{
    if (Sql::scalarInt(m_file.database(), QStringLiteral("SELECT COUNT(*) FROM owner_payments WHERE doc_id = ?"), {docId})
        == 0) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such owner payment"));
    }
    return Ledger(m_file).voidDocument(docId, reason, options);
}

QList<OwnerPaymentRecord> OwnerPay::payments(const QDate &from, const QDate &to, std::optional<OwnerPaymentKind> kind,
                                             bool includeVoid) const
{
    QList<OwnerPaymentRecord> out;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q,
                 QStringLiteral("SELECT op.doc_id, op.kind, op.owner_id, d.kind, d.date, d.total, d.number, d.account_id,"
                                " d.to_account_id, op.tax_year, op.quarter, d.reference, d.memo, d.state"
                                " FROM owner_payments op JOIN documents d ON d.id = op.doc_id"
                                " WHERE (? = '' OR d.date >= ?) AND (? = '' OR d.date <= ?) AND (? = '' OR op.kind = ?)"
                                " AND (? = 1 OR d.state <> ?) ORDER BY d.date, op.doc_id"),
                 {Sql::isoDate(from), Sql::isoDate(from), Sql::isoDate(to), Sql::isoDate(to),
                  kind ? ownerPaymentKindKey(*kind) : QString(), kind ? ownerPaymentKindKey(*kind) : QString(),
                  includeVoid ? 1 : 0, int(DocState::Void)});
        while (q.next()) {
            OwnerPaymentRecord r;
            r.docId = q.value(0).toLongLong();
            r.kind = ownerPaymentKindFromKey(q.value(1).toString()).value_or(OwnerPaymentKind::Draw);
            r.ownerId = q.value(2).toLongLong();
            r.docKind = DocKind(q.value(3).toInt());
            r.date = Sql::fromIso(q.value(4));
            r.amount = Money(q.value(5).toLongLong(), m_file.currency());
            r.number = q.value(6).toString();
            r.bankAccountId = q.value(7).toLongLong();
            r.toAccountId = q.value(8).toLongLong();
            r.taxYear = q.value(9).toInt();
            r.installment = q.value(10).toInt();
            r.reference = q.value(11).toString();
            r.memo = q.value(12).toString();
            r.voided = q.value(13).toInt() == int(DocState::Void);
            out << r;
        }
    }
    Lists lists(m_file);
    for (OwnerPaymentRecord &r : out) {
        if (const std::optional<Party> p = lists.party(r.ownerId)) {
            r.ownerName = p->displayName;
        }
    }
    return out;
}

QList<EstimatedPayment> OwnerPay::estimatedPayments(int taxYear) const
{
    QList<EstimatedPayment> out;
    for (const OwnerPaymentRecord &r : payments()) {
        if (r.taxYear != taxYear) {
            continue;
        }
        if (r.kind == OwnerPaymentKind::FederalEstimate) {
            out << EstimatedPayment{EstimatedPayment::Jurisdiction::Federal, r.date, r.amount};
        } else if (r.kind == OwnerPaymentKind::ColoradoEstimate) {
            out << EstimatedPayment{EstimatedPayment::Jurisdiction::Colorado, r.date, r.amount};
        }
    }
    return out;
}

} // namespace QindaOffice::Books
