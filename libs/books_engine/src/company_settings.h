// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Company-wide settings stored in the company file's `settings` table
// (key/value). One company file holds exactly one business (D-045); these
// settings are its identity on invoices and its accounting defaults.

#include "books_types.h"

#include <QByteArray>
#include <QDate>
#include <QString>

namespace QindaOffice::Books {

struct CompanySettings {
    QString legalName;
    QString tradeName;       // "doing business as"; empty = legal name
    QString address;         // multi-line postal address
    QString email;
    QString phone;
    QString website;
    QString taxId;           // EIN / VAT / ABN … printed on invoices
    QString currency = QStringLiteral("USD"); // ISO 4217; fixed at creation
    QString locale = QStringLiteral("en_US"); // number/date formatting
    int fiscalYearStartMonth = 1;             // 1..12
    Basis basis = Basis::Accrual;             // default report basis
    QDate lockDate;                           // closed through this date
    QString invoicePrefix = QStringLiteral("INV-");
    qint64 nextInvoiceNumber = 1001;
    QString estimatePrefix = QStringLiteral("EST-");
    qint64 nextEstimateNumber = 1001;
    QString creditMemoPrefix = QStringLiteral("CM-");
    qint64 nextCreditMemoNumber = 1001;
    QString salesReceiptPrefix = QStringLiteral("SR-");
    qint64 nextSalesReceiptNumber = 1001;
    QString purchaseOrderPrefix = QStringLiteral("PO-"); // M68 (D-077)
    qint64 nextPurchaseOrderNumber = 1001;
    qint64 defaultTermsId = 0;                // 0 = Net 30 row created with the file
    QByteArray logo;                          // PNG/JPEG/SVG bytes as supplied
    QString accentColor = QStringLiteral("#1F5FBF"); // the one colour on invoices
    QString paymentInstructions;              // printed on invoices/statements
    QString invoiceFooter;

    [[nodiscard]] QString displayName() const { return tradeName.isEmpty() ? legalName : tradeName; }
    // First day of the fiscal year that contains `date`.
    [[nodiscard]] QDate fiscalYearStart(const QDate &date) const;
};

} // namespace QindaOffice::Books
