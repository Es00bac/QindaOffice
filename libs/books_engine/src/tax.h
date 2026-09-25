// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Sales tax: agencies (who you remit to), components (one rate charged for
// one agency) and rates (what a document applies: one or several
// components, e.g. state 6% + county 1.25% + city 1%).
//
// Computation (docs/books.md "Sales tax"): per component,
//   tax = taxable base × component rate
// rounded ONCE per document with the agency's rounding mode (half-up
// unless the agency's rule says half-even). Collected tax credits the
// Sales Tax Payable system account on lines tagged with component and
// agency, so the liability report and payments work per agency.

#include "books_types.h"
#include "decimal.h"
#include "documents.h"
#include "money.h"

#include <QDate>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

struct TaxAgency {
    qint64 id = 0;
    QString name;
    QString registrationId;
    RoundingMode rounding = RoundingMode::HalfUp;
    bool active = true;
};

struct TaxComponent {
    qint64 id = 0;
    qint64 agencyId = 0;
    QString name;
    Decimal rate; // fraction: 0.0825 for 8.25% (parse "8.25%")
    bool active = true;
};

struct TaxRate {
    qint64 id = 0;
    QString name;
    QList<TaxComponent> components;
    bool active = true;
    [[nodiscard]] Decimal combinedRate() const;
};

class SalesTax {
public:
    explicit SalesTax(CompanyFile &file);

    Result createAgency(const TaxAgency &agency);
    Result createComponent(const TaxComponent &component);
    Result createRate(const QString &name, const QList<qint64> &componentIds);
    // One agency component + a rate of its own in one call.
    Result createSimpleRate(qint64 agencyId, const QString &name, const Decimal &rate);

    [[nodiscard]] QList<TaxAgency> agencies() const;
    [[nodiscard]] std::optional<TaxAgency> agency(qint64 id) const;
    [[nodiscard]] std::optional<TaxRate> rate(qint64 id) const;
    [[nodiscard]] QList<TaxRate> rates() const;

    // Per-component tax on `taxableBase`.
    [[nodiscard]] QList<TaxShare> compute(qint64 rateId, const Money &taxableBase) const;

    // Remittance: Dr Sales Tax Payable (tagged with the agency) / Cr bank.
    Result recordPayment(qint64 agencyId, qint64 bankAccountId, const Money &amount,
                         const QDate &date, const QString &memo = {});
    // Outstanding liability to one agency (collected − paid) as of a date.
    [[nodiscard]] Money liability(qint64 agencyId, const QDate &asOf = {}) const;

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
