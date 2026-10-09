// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Private to BooksService's document files (books_service_documents.cpp,
// books_service_document_map.cpp): the kind groups the workspaces list and
// the small rules both files apply. Not part of the QML API.

#include "books_types.h"

#include <QList>
#include <QVariantMap>

namespace QindaOffice::Apps::Books::Kinds {

namespace Engine = ::QindaOffice::Books;

// AGENT-CONTRACT (documents() kind groups, with the views): "sales" =
// invoice, salesReceipt, creditMemo, payment (estimates are asked for by
// kind); "purchases" (alias "expenses") = bill, expense, check,
// creditCardCharge, vendorCredit, billPayment; "orders" (M68) = purchase
// orders; "journal" = journal entries, transfers, deposits, opening
// balances, sales-tax payments.
inline const QList<Engine::DocKind> kSalesGroup{Engine::DocKind::Invoice, Engine::DocKind::SalesReceipt,
                                                Engine::DocKind::CreditMemo, Engine::DocKind::ReceivePayment};
inline const QList<Engine::DocKind> kExpenseKinds{Engine::DocKind::Bill,    Engine::DocKind::BillPayment,
                                                  Engine::DocKind::Expense, Engine::DocKind::Check,
                                                  Engine::DocKind::CreditCardCharge, Engine::DocKind::VendorCredit};
inline const QList<Engine::DocKind> kJournalGroup{Engine::DocKind::JournalEntry, Engine::DocKind::Transfer,
                                                  Engine::DocKind::Deposit, Engine::DocKind::OpeningBalance,
                                                  Engine::DocKind::SalesTaxPayment};

// Purchases (and purchase orders) carry no discount or sales tax.
inline bool isPurchase(Engine::DocKind kind)
{
    return kExpenseKinds.contains(kind) || kind == Engine::DocKind::PurchaseOrder;
}

// Kinds whose open balance means something (targets and sources of
// applications); a purchase order's "balance" is what is left to receive.
inline bool tracksBalance(Engine::DocKind kind)
{
    return kind == Engine::DocKind::Invoice || kind == Engine::DocKind::Bill || kind == Engine::DocKind::ReceivePayment
        || kind == Engine::DocKind::CreditMemo || kind == Engine::DocKind::BillPayment
        || kind == Engine::DocKind::VendorCredit;
}

// The editor's empty trailing row: nothing chosen, typed or priced.
inline bool isBlankLine(const QVariantMap &l)
{
    const QString amount = l.value(QStringLiteral("amount")).toString().trimmed();
    const auto id = [&l](const char *key) { return qint64(l.value(QString::fromLatin1(key)).toDouble()); };
    return id("itemId") == 0 && id("accountId") == 0 && id("poLineId") == 0 && id("timeEntryId") == 0
        && l.value(QStringLiteral("description")).toString().trimmed().isEmpty()
        && l.value(QStringLiteral("rate")).toString().trimmed().isEmpty() && (amount.isEmpty() || amount == QLatin1String("0"));
}

} // namespace QindaOffice::Apps::Books::Kinds
