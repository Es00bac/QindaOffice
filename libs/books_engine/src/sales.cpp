// SPDX-License-Identifier: GPL-3.0-or-later
// Sales documents that post like an invoice: invoices, sales receipts,
// credit memos (and estimates, which never post). Payments, credits,
// refunds and statements are in sales_payments.cpp.
#include "sales.h"

#include "accounts.h"
#include "company_file.h"
#include "lists.h"
#include "sql_util.h"

#include <QJsonDocument>

namespace QindaOffice::Books {

namespace {

QString compact(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

// The invoice-shaped posting. `reverse` swaps every side (credit memo).
QList<PostingLine> salesPosting(const BusinessDocument &doc, const DocumentTotals &totals, qint64 debitAccount,
                                qint64 discountAccount, qint64 taxAccount, bool reverse)
{
    auto line = [reverse, &doc](qint64 account, qint64 amount, bool debit) {
        PostingLine p;
        p.accountId = account;
        p.partyId = doc.partyId;
        const bool isDebit = reverse ? !debit : debit;
        // A negative amount (a discount line) flips to the other side.
        const qint64 value = amount < 0 ? -amount : amount;
        const bool side = amount < 0 ? !isDebit : isDebit;
        p.debit = side ? value : 0;
        p.credit = side ? 0 : value;
        return p;
    };
    QList<PostingLine> lines;
    lines << line(debitAccount, totals.total.minor(), true);
    for (qsizetype i = 0; i < doc.lines.size(); ++i) {
        PostingLine p = line(doc.lines[i].accountId, totals.lineAmounts.at(i).minor(), false);
        p.itemId = doc.lines[i].itemId;
        p.classId = doc.lines[i].classId;
        p.memo = doc.lines[i].description;
        lines << p;
    }
    if (!totals.discount.isZero()) {
        lines << line(discountAccount, totals.discount.minor(), true);
    }
    for (const TaxShare &share : totals.taxes) {
        PostingLine p = line(taxAccount, share.amount.minor(), false);
        p.taxComponentId = share.componentId;
        p.taxAgencyId = share.agencyId;
        p.memo = share.name;
        lines << p;
    }
    return lines;
}

} // namespace

Sales::Sales(CompanyFile &file)
    : m_file(file)
{
}

int agingBucket(const QDate &dueDate, const QDate &asOf)
{
    const qint64 late = dueDate.daysTo(asOf);
    if (late <= 0) {
        return 0;
    }
    if (late <= 30) {
        return 1;
    }
    if (late <= 60) {
        return 2;
    }
    if (late <= 90) {
        return 3;
    }
    return 4;
}

Result Sales::validateSalesDocument(BusinessDocument &doc) const
{
    const std::optional<Party> customer = Lists(m_file).party(doc.partyId);
    if (!customer || customer->kind != PartyKind::Customer) {
        return Result::failure(ErrorCode::UnknownParty, QStringLiteral("choose a customer"));
    }
    if (!doc.date.isValid()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a date is required"));
    }
    if (doc.lines.isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("add at least one line"));
    }
    Documents documents(m_file);
    documents.fillFromItems(doc, false);
    ChartOfAccounts chart(m_file);
    for (const DocumentLine &line : std::as_const(doc.lines)) {
        if (line.accountId == 0 || !chart.account(line.accountId)) {
            return Result::failure(ErrorCode::UnknownAccount,
                                   QStringLiteral("line \"%1\" has no income account").arg(line.description));
        }
        if (!line.unitPrice.sameCurrency(m_file.money(0)) || !line.amount.sameCurrency(m_file.money(0))) {
            return Result::failure(ErrorCode::CurrencyMismatch,
                                   QStringLiteral("amounts must be in %1").arg(m_file.currency()));
        }
    }
    if (doc.taxRateId == 0) {
        doc.taxRateId = customer->taxRateId;
    }
    if (doc.termsId == 0) {
        doc.termsId = customer->termsId != 0 ? customer->termsId : m_file.settings().defaultTermsId;
    }
    if (!doc.dueDate.isValid()) {
        doc.dueDate = documents.dueDateFor(doc.date, doc.termsId);
    }
    if (doc.kind == DocKind::SalesReceipt && doc.accountId == 0) {
        // Stored on the document so Banking::undepositedPayments finds it.
        doc.accountId = chart.idForRole(SystemRole::UndepositedFunds);
    }
    const DocumentTotals totals = documents.finalize(doc);
    if (totals.total.isNegative()) {
        return Result::failure(ErrorCode::Invalid,
                               QStringLiteral("the total is negative; record a credit memo instead"));
    }
    return Result::success();
}

Result Sales::postSalesDocument(BusinessDocument &doc, const PostOptions &options, bool isNew,
                                const QString &reason)
{
    if (const Result valid = validateSalesDocument(doc); !valid.ok) {
        return valid;
    }
    Documents documents(m_file);
    ChartOfAccounts chart(m_file);
    Ledger ledger(m_file);
    const std::optional<BusinessDocument> before = isNew ? std::nullopt : documents.load(doc.id);
    if (!isNew && before && documents.appliedTo(doc.id) > doc.total && doc.kind == DocKind::Invoice) {
        return Result::failure(ErrorCode::OverApplied,
                               QStringLiteral("payments of %1 are already applied; the total cannot drop below that")
                                   .arg(documents.appliedTo(doc.id).toPlainString()));
    }
    if (!isNew && before && doc.kind == DocKind::CreditMemo && documents.appliedFrom(doc.id) > doc.total) {
        return Result::failure(ErrorCode::OverApplied, QStringLiteral("this credit is already applied"));
    }
    SqlTransaction txn(m_file);
    if (isNew && doc.number.isEmpty()) {
        doc.number = m_file.takeNextNumber(doc.kind);
    } else if (isNew && !doc.number.isEmpty()
               && Sql::scalarInt(m_file.database(),
                                 QStringLiteral("SELECT COUNT(*) FROM documents WHERE kind = ? AND number = ?"),
                                 {int(doc.kind), doc.number})
                   > 0) {
        return Result::failure(ErrorCode::Duplicate, QStringLiteral("number %1 is already used").arg(doc.number));
    }
    qint64 voidedTxn = 0;
    if (!isNew) {
        const Result voided = ledger.voidLiveTransaction(doc.id, reason, options);
        if (!voided.ok) {
            return voided;
        }
        voidedTxn = voided.id;
    }
    if (const Result w = documents.write(doc); !w.ok) {
        return w;
    }
    if (doc.state != DocState::Draft) {
        qint64 debitAccount = chart.idForRole(SystemRole::AccountsReceivable);
        if (doc.kind == DocKind::SalesReceipt) {
            debitAccount = doc.accountId != 0 ? doc.accountId : chart.idForRole(SystemRole::UndepositedFunds);
        }
        const DocumentTotals totals = documents.computeTotals(doc);
        const QList<PostingLine> lines =
            salesPosting(doc, totals, debitAccount, chart.idForRole(SystemRole::SalesDiscounts),
                         chart.idForRole(SystemRole::SalesTaxPayable), doc.kind == DocKind::CreditMemo);
        const Result posted = ledger.postLines(doc.id, doc.kind, doc.date, doc.number, doc.memo, lines, options);
        if (!posted.ok) {
            return posted;
        }
        if (voidedTxn != 0) {
            Sql::exec(m_file.database(), QStringLiteral("UPDATE transactions SET replaced_by = ? WHERE id = ?"),
                      {posted.id, voidedTxn});
        }
    }
    m_file.audit(isNew ? QStringLiteral("create") : QStringLiteral("edit"), QStringLiteral("document"), doc.id,
                 before ? compact(documents.toJson(*before)) : QString(), compact(documents.toJson(doc)), reason);
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(doc.id);
}

Result Sales::saveEstimate(BusinessDocument estimate)
{
    estimate.kind = DocKind::Estimate;
    if (estimate.state != DocState::Closed) {
        estimate.state = DocState::Open;
    }
    if (const Result valid = validateSalesDocument(estimate); !valid.ok) {
        return valid;
    }
    Documents documents(m_file);
    const std::optional<BusinessDocument> before =
        estimate.id == 0 ? std::nullopt : documents.load(estimate.id);
    SqlTransaction txn(m_file);
    if (estimate.number.isEmpty()) {
        estimate.number = m_file.takeNextNumber(DocKind::Estimate);
    }
    if (const Result w = documents.write(estimate); !w.ok) {
        return w;
    }
    m_file.audit(before ? QStringLiteral("edit") : QStringLiteral("create"), QStringLiteral("document"), estimate.id,
                 before ? compact(documents.toJson(*before)) : QString(), compact(documents.toJson(estimate)));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(estimate.id);
}

Result Sales::convertEstimateToInvoice(qint64 estimateId, const QDate &invoiceDate, const PostOptions &options)
{
    Documents documents(m_file);
    const std::optional<BusinessDocument> estimate = documents.load(estimateId);
    if (!estimate || estimate->kind != DocKind::Estimate) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such estimate"));
    }
    if (estimate->state != DocState::Open) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("this estimate is already closed or void"));
    }
    BusinessDocument invoice = *estimate;
    invoice.id = 0;
    invoice.kind = DocKind::Invoice;
    invoice.state = DocState::Open;
    invoice.number.clear();
    invoice.date = invoiceDate;
    invoice.dueDate = QDate();
    invoice.sourceDocId = estimateId;
    invoice.txnId = 0;
    for (DocumentLine &line : invoice.lines) {
        line.id = 0;
    }
    SqlTransaction txn(m_file);
    const Result created = createInvoice(invoice, options);
    if (!created.ok) {
        return created;
    }
    if (const Result closed = documents.setState(estimateId, DocState::Closed); !closed.ok) {
        return closed;
    }
    m_file.audit(QStringLiteral("convert"), QStringLiteral("document"), estimateId, {},
                 QStringLiteral("{\"invoice\":%1}").arg(created.id));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return created;
}

Result Sales::createInvoice(BusinessDocument invoice, const PostOptions &options)
{
    invoice.kind = DocKind::Invoice;
    invoice.id = 0;
    if (invoice.state != DocState::Draft) {
        invoice.state = DocState::Open;
    }
    return postSalesDocument(invoice, options, true, {});
}

Result Sales::updateInvoice(BusinessDocument invoice, const QString &reason, const PostOptions &options)
{
    const std::optional<BusinessDocument> before = Documents(m_file).load(invoice.id);
    if (!before || before->kind != DocKind::Invoice) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such invoice"));
    }
    if (before->state == DocState::Void) {
        return Result::failure(ErrorCode::AlreadyVoided, QStringLiteral("a void invoice cannot be edited"));
    }
    invoice.kind = DocKind::Invoice;
    invoice.number = invoice.number.isEmpty() ? before->number : invoice.number;
    invoice.state = before->state == DocState::Draft && invoice.state == DocState::Draft ? DocState::Draft
                                                                                         : DocState::Open;
    return postSalesDocument(invoice, options, false, reason);
}

Result Sales::postDraft(qint64 docId, const PostOptions &options)
{
    std::optional<BusinessDocument> doc = Documents(m_file).load(docId);
    if (!doc || doc->state != DocState::Draft) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("not a draft"));
    }
    doc->state = DocState::Open;
    return postSalesDocument(*doc, options, false, QStringLiteral("post draft"));
}

Result Sales::createSalesReceipt(BusinessDocument receipt, const PostOptions &options)
{
    receipt.kind = DocKind::SalesReceipt;
    receipt.id = 0;
    receipt.state = DocState::Open;
    return postSalesDocument(receipt, options, true, {});
}

Result Sales::createCreditMemo(BusinessDocument memo, const PostOptions &options)
{
    memo.kind = DocKind::CreditMemo;
    memo.id = 0;
    memo.state = DocState::Open;
    return postSalesDocument(memo, options, true, {});
}

} // namespace QindaOffice::Books
