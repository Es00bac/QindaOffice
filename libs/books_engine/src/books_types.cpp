// SPDX-License-Identifier: GPL-3.0-or-later
#include "books_types.h"

namespace QindaOffice::Books {

QString docKindName(DocKind kind)
{
    switch (kind) {
    case DocKind::JournalEntry: return QStringLiteral("Journal Entry");
    case DocKind::Invoice: return QStringLiteral("Invoice");
    case DocKind::SalesReceipt: return QStringLiteral("Sales Receipt");
    case DocKind::ReceivePayment: return QStringLiteral("Payment");
    case DocKind::CreditMemo: return QStringLiteral("Credit Memo");
    case DocKind::Refund: return QStringLiteral("Refund");
    case DocKind::Estimate: return QStringLiteral("Estimate");
    case DocKind::Bill: return QStringLiteral("Bill");
    case DocKind::BillPayment: return QStringLiteral("Bill Payment");
    case DocKind::Expense: return QStringLiteral("Expense");
    case DocKind::Check: return QStringLiteral("Check");
    case DocKind::CreditCardCharge: return QStringLiteral("Credit Card Charge");
    case DocKind::VendorCredit: return QStringLiteral("Vendor Credit");
    case DocKind::Transfer: return QStringLiteral("Transfer");
    case DocKind::Deposit: return QStringLiteral("Deposit");
    case DocKind::SalesTaxPayment: return QStringLiteral("Sales Tax Payment");
    case DocKind::InvoiceTemplate: return QStringLiteral("Recurring Template");
    case DocKind::OpeningBalance: return QStringLiteral("Opening Balance");
    case DocKind::PurchaseOrder: return QStringLiteral("Purchase Order");
    }
    return QStringLiteral("Transaction");
}

bool docKindPosts(DocKind kind)
{
    return kind != DocKind::Estimate && kind != DocKind::InvoiceTemplate && kind != DocKind::PurchaseOrder;
}

bool docKindIsReceivable(DocKind kind)
{
    return kind == DocKind::Invoice || kind == DocKind::CreditMemo;
}

bool docKindIsPayable(DocKind kind)
{
    return kind == DocKind::Bill || kind == DocKind::VendorCredit;
}

QString documentStatusName(DocumentStatus status)
{
    switch (status) {
    case DocumentStatus::Draft: return QStringLiteral("draft");
    case DocumentStatus::Open: return QStringLiteral("open");
    case DocumentStatus::Partial: return QStringLiteral("partial");
    case DocumentStatus::Paid: return QStringLiteral("paid");
    case DocumentStatus::Overdue: return QStringLiteral("overdue");
    case DocumentStatus::Void: return QStringLiteral("void");
    case DocumentStatus::Pending: return QStringLiteral("pending");
    case DocumentStatus::Closed: return QStringLiteral("closed");
    case DocumentStatus::PartiallyReceived: return QStringLiteral("partiallyReceived");
    }
    return QString();
}

} // namespace QindaOffice::Books
