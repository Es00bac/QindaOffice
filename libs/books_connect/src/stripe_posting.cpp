// SPDX-License-Identifier: GPL-3.0-or-later
// One Stripe balance transaction → bank-feed lines on the Stripe clearing
// account, plus the automatic postings (stripe_posting.h has the model).
#include "stripe_posting.h"

#include "banking.h"
#include "company_file.h"
#include "documents.h"
#include "sales.h"
#include "sql_util.h"

#include <QDateTime>
#include <QTimeZone>

namespace QindaOffice::Books::Connect {

namespace {

bool isSale(const QString &type)
{
    return type == QLatin1String("charge") || type == QLatin1String("payment");
}

bool isFee(const QString &type)
{
    return type == QLatin1String("stripe_fee") || type == QLatin1String("stripe_fx_fee")
        || type == QLatin1String("tax_fee") || type == QLatin1String("application_fee");
}

QString firstNonEmpty(std::initializer_list<QString> values)
{
    for (const QString &v : values) {
        if (!v.trimmed().isEmpty()) {
            return v.trimmed();
        }
    }
    return QString();
}

// Payee and memo for the gross line, from the expanded source when there.
void describe(const StripeBalanceTransaction &t, QString *payee, QString *memo)
{
    const QJsonObject billing = t.source.value(QStringLiteral("billing_details")).toObject();
    if (isSale(t.type)) {
        *payee = firstNonEmpty({billing.value(QStringLiteral("name")).toString(),
                                t.source.value(QStringLiteral("receipt_email")).toString(), t.description,
                                QStringLiteral("Stripe payment")});
    } else if (t.type == QLatin1String("payout")) {
        *payee = QStringLiteral("Stripe payout");
    } else if (isFee(t.type)) {
        *payee = QStringLiteral("Stripe");
    } else {
        *payee = firstNonEmpty({t.description, QStringLiteral("Stripe %1").arg(t.type)});
    }
    *memo = QStringLiteral("%1 · Stripe %2 %3")
                .arg(firstNonEmpty({t.description, t.source.value(QStringLiteral("description")).toString(), t.type}),
                     t.type, t.sourceId.isEmpty() ? t.id : t.sourceId)
                .simplified();
}

} // namespace

QDate stripeDate(qint64 unixSeconds)
{
    return QDateTime::fromSecsSinceEpoch(unixSeconds, QTimeZone::systemTimeZone()).date();
}

StripePosting::StripePosting(CompanyFile &file, StripeSettings settings, QString companyUuid)
    : m_file(file)
    , m_settings(std::move(settings))
    , m_companyUuid(std::move(companyUuid))
{
}

qint64 StripePosting::lineOf(qint64 docId, qint64 accountId) const
{
    return Sql::scalarInt(m_file.database(),
                          QStringLiteral("SELECT l.id FROM journal_lines l JOIN documents d ON d.txn_id = l.txn_id"
                                         " WHERE d.id = ? AND l.account_id = ? AND l.voided = 0 ORDER BY l.id LIMIT 1"),
                          {docId, accountId});
}

void StripePosting::post(const StripeBalanceTransaction &t, SyncReport *report)
{
    if (t.currency != m_file.currency().toLower()) {
        report->errors << QStringLiteral("Stripe transaction %1 is in %2 but these books are kept in %3; it was not "
                                         "imported.")
                              .arg(t.id, t.currency.toUpper(), m_file.currency());
        return;
    }
    const QDate date = stripeDate(t.created);
    QString payee;
    QString memo;
    describe(t, &payee, &memo);
    const QString feeFitid = t.id + QStringLiteral(":fee");
    QList<BankLine> lines;
    if (t.amount != 0) {
        lines << BankLine{date, t.amount, payee, memo, t.id, {}};
    }
    if (t.fee != 0) {
        lines << BankLine{date, -t.fee, QStringLiteral("Stripe"),
                          QStringLiteral("Stripe fee · %1").arg(t.sourceId.isEmpty() ? t.id : t.sourceId), feeFitid, {}};
    }
    if (lines.isEmpty()) {
        return;
    }
    Banking banking(m_file);
    // AGENT-GUARD: import and postings commit together (see the header).
    SqlTransaction outer(m_file);
    const ImportSummary summary = banking.importLines(m_settings.clearingAccountId, lines, QStringLiteral("stripe"));
    report->imported += summary.added;
    qint64 grossId = 0;
    qint64 feeId = 0;
    for (qint64 id : summary.ids) {
        const std::optional<BankFeedEntry> e = banking.feedEntry(id);
        if (e && e->fitid == t.id) {
            grossId = id;
        } else if (e && e->fitid == feeFitid) {
            feeId = id;
        }
    }
    if (grossId != 0) {
        if (isSale(t.type) && t.amount > 0) {
            applyToInvoice(t, grossId, date, report);
        } else if (t.type == QLatin1String("payout") && t.amount < 0) {
            bookPayout(t, grossId, date, report);
        } else if (isFee(t.type)) {
            bookFee(grossId, memo, report);
        }
    }
    if (feeId != 0) {
        bookFee(feeId, QStringLiteral("Stripe fee · %1").arg(t.sourceId.isEmpty() ? t.id : t.sourceId), report);
    }
    if (!outer.commit()) {
        report->fail(QStringLiteral("Could not save Stripe transaction %1: %2").arg(t.id, outer.error()));
    }
}

bool StripePosting::applyToInvoice(const StripeBalanceTransaction &t, qint64 feedId, const QDate &date,
                                   SyncReport *report)
{
    const QJsonObject metadata = t.source.value(QStringLiteral("metadata")).toObject();
    const QString company = metadata.value(kCompanyKey).toString();
    const qint64 invoiceId = metadata.value(kInvoiceKey).toString().toLongLong();
    if (!m_settings.autoApplyInvoices || (company.isEmpty() && invoiceId == 0)) {
        return false;
    }
    const QString number = metadata.value(kNumberKey).toString();
    if (company != m_companyUuid) {
        // AGENT-GUARD: businesses never mix — an invoice of another
        // company file is never looked up here, even when the id exists.
        report->errors << QStringLiteral("Stripe payment %1 is for invoice %2 of another company; it was left for "
                                         "review.")
                              .arg(t.sourceId, number.isEmpty() ? QString::number(invoiceId) : number);
        return false;
    }
    Documents documents(m_file);
    const std::optional<BusinessDocument> invoice = documents.load(invoiceId);
    const Money open = documents.openBalance(invoiceId);
    if (!invoice || invoice->kind != DocKind::Invoice || invoice->state != DocState::Open || !open.isPositive()) {
        report->errors << QStringLiteral("Stripe payment %1 names invoice %2, which is not open; the payment was "
                                         "left for review.")
                              .arg(t.sourceId, number.isEmpty() ? QString::number(invoiceId) : number);
        return false;
    }
    const Money gross = m_file.money(t.amount);
    ReceivePaymentInput input;
    input.customerId = invoice->partyId;
    input.date = date;
    input.amount = gross;
    input.depositAccountId = m_settings.clearingAccountId;
    input.reference = t.sourceId;
    input.memo = QStringLiteral("Stripe payment for %1").arg(invoice->number);
    input.applications << PaymentApplication{invoiceId, gross < open ? gross : open};
    SqlTransaction txn(m_file);
    const Result paid = Sales(m_file).receivePayment(input);
    const Result matched = paid.ok ? Banking(m_file).match(feedId, lineOf(paid.id, m_settings.clearingAccountId))
                                   : paid;
    if (!matched.ok || !txn.commit()) {
        report->errors << QStringLiteral("Stripe payment %1 could not be applied to %2: %3")
                              .arg(t.sourceId, invoice->number, matched.error);
        return false;
    }
    ++report->applied;
    ++report->matched;
    return true;
}

void StripePosting::bookPayout(const StripeBalanceTransaction &t, qint64 feedId, const QDate &date,
                               SyncReport *report)
{
    if (!m_settings.autoPostPayouts || m_settings.payoutAccountId == 0) {
        return;
    }
    const QString payoutId = t.sourceId.startsWith(QStringLiteral("po_")) ? t.sourceId : QString();
    SqlTransaction txn(m_file);
    Banking banking(m_file);
    const Result transfer = banking.transfer(m_settings.clearingAccountId, m_settings.payoutAccountId,
                                             m_file.money(-t.amount), date,
                                             QStringLiteral("Stripe payout %1").arg(t.sourceId), {}, payoutId);
    const Result matched =
        transfer.ok ? banking.match(feedId, lineOf(transfer.id, m_settings.clearingAccountId)) : transfer;
    if (!matched.ok || !txn.commit()) {
        report->errors << QStringLiteral("Stripe payout %1 could not be booked: %2").arg(t.sourceId, matched.error);
        return;
    }
    ++report->posted;
}

void StripePosting::bookFee(qint64 feedId, const QString &memo, SyncReport *report)
{
    if (!m_settings.autoPostFees || m_settings.feeAccountId == 0) {
        return;
    }
    SqlTransaction txn(m_file);
    const Result booked = Banking(m_file).categorize(feedId, m_settings.feeAccountId, 0, 0, memo);
    if (!booked.ok || !txn.commit()) {
        report->errors << QStringLiteral("A Stripe fee could not be booked: %1").arg(booked.error);
        return;
    }
    ++report->posted;
}

} // namespace QindaOffice::Books::Connect
