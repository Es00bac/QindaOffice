// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: payment terms, classes/tags and sales tax (agencies,
// rates, and — M63 — what is owed and paying it).
#include "books_service.h"

#include "accounts.h"
#include "company_file.h"
#include "lists.h"
#include "tax.h"

#include <QHash>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

// ---- terms, classes, tax ---------------------------------------------------------

QVariantList BooksService::terms() const
{
    QVariantList out;
    if (CompanyFile *c = file()) {
        for (const Terms &t : Lists(*c).terms()) {
            out << QVariantMap{{QStringLiteral("id"), t.id},
                               {QStringLiteral("name"), t.name},
                               {QStringLiteral("dueDays"), t.dueDays}};
        }
    }
    return out;
}

QVariantList BooksService::classes() const
{
    QVariantList out;
    if (CompanyFile *c = file()) {
        for (const ClassTag &t : Lists(*c).classes()) {
            out << QVariantMap{{QStringLiteral("id"), t.id},
                               {QStringLiteral("name"), t.name},
                               {QStringLiteral("parentId"), t.parentId}};
        }
    }
    return out;
}

QVariantMap BooksService::saveClass(const QVariantMap &map)
{
    if (file() == nullptr) {
        return noCompany();
    }
    ClassTag tag;
    tag.name = map.value(QStringLiteral("name")).toString().trimmed();
    tag.parentId = Format::id(map.value(QStringLiteral("parentId")));
    return Format::result(Lists(*file()).createClass(tag));
}

QVariantList BooksService::taxRates() const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    SalesTax tax(*c);
    QHash<qint64, QString> agencies;
    for (const TaxAgency &a : tax.agencies()) {
        agencies.insert(a.id, a.name);
    }
    for (const TaxRate &r : tax.rates()) {
        QVariantList components;
        for (const TaxComponent &comp : r.components) {
            components << QVariantMap{{QStringLiteral("id"), comp.id},
                                      {QStringLiteral("agencyId"), comp.agencyId},
                                      {QStringLiteral("agencyName"), agencies.value(comp.agencyId)},
                                      {QStringLiteral("name"), comp.name},
                                      {QStringLiteral("rate"), comp.rate.toPercentString()}};
        }
        QStringList agencyNames;
        for (const TaxComponent &comp : r.components) {
            if (!agencyNames.contains(agencies.value(comp.agencyId))) {
                agencyNames << agencies.value(comp.agencyId);
            }
        }
        out << QVariantMap{{QStringLiteral("id"), r.id},
                           {QStringLiteral("name"), r.name},
                           {QStringLiteral("percent"), Format::percentText(r.combinedRate())},
                           {QStringLiteral("rateText"), r.combinedRate().toPercentString()},
                           {QStringLiteral("rate"), r.combinedRate().toPercentString()},
                           {QStringLiteral("agencyName"), agencyNames.join(QStringLiteral(", "))},
                           {QStringLiteral("active"), r.active},
                           {QStringLiteral("components"), components}};
    }
    return out;
}

QVariantList BooksService::taxAgencies() const
{
    QVariantList out;
    if (CompanyFile *c = file()) {
        for (const TaxAgency &a : SalesTax(*c).agencies()) {
            out << QVariantMap{{QStringLiteral("id"), a.id},
                               {QStringLiteral("name"), a.name},
                               {QStringLiteral("registrationId"), a.registrationId},
                               {QStringLiteral("halfEven"), a.rounding == RoundingMode::HalfEven}};
        }
    }
    return out;
}

QVariantMap BooksService::saveTaxRate(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    if (Format::id(map.value(QStringLiteral("id"))) != 0) {
        // AGENT-NOTE: rates are referenced by posted documents; the engine
        // has no edit, so a changed rate is a new rate (and the old one stays).
        return Format::failure(tr("A tax rate cannot be changed once created. Add a new rate instead."));
    }
    SalesTax tax(*c);
    QString rateText = map.value(map.contains(QStringLiteral("percent")) ? QStringLiteral("percent")
                                                                         : QStringLiteral("rate"))
                           .toString()
                           .trimmed();
    if (!rateText.endsWith(QLatin1Char('%'))) {
        rateText += QLatin1Char('%'); // "8.25" is typed as a percentage
    }
    const std::optional<Decimal> rate = Decimal::parse(rateText);
    if (!rate || rate->isNegative()) {
        return Format::failure(tr("\"%1\" is not a tax rate.").arg(rateText));
    }
    qint64 agencyId = Format::id(map.value(QStringLiteral("agencyId")));
    const QString agencyName = map.value(QStringLiteral("agencyName")).toString().trimmed();
    for (const TaxAgency &a : tax.agencies()) {
        if (agencyId == 0 && !agencyName.isEmpty() && a.name.compare(agencyName, Qt::CaseInsensitive) == 0) {
            agencyId = a.id; // an agency the company already remits to
        }
    }
    if (agencyId == 0) {
        // A rate for a new agency, named by the user ("State Revenue").
        TaxAgency agency;
        agency.name = map.value(QStringLiteral("agencyName")).toString().trimmed();
        if (agency.name.isEmpty()) {
            agency.name = map.value(QStringLiteral("name")).toString().trimmed();
        }
        agency.rounding = map.value(QStringLiteral("halfEven")).toBool() ? RoundingMode::HalfEven : RoundingMode::HalfUp;
        const Result created = tax.createAgency(agency);
        if (!created.ok) {
            return Format::result(created);
        }
        agencyId = created.id;
    }
    return Format::result(tax.createSimpleRate(agencyId, map.value(QStringLiteral("name")).toString().trimmed(), *rate));
}

// ---- sales tax owed and paid (M63, D-071) ----------------------------------------

// AGENT-CONTRACT (QML, SalesTaxPaymentDialog.qml): one row per agency with
// {agencyId, name, owed, owedText, owedPlain} — owed = collected − paid as
// of the date (SalesTax::liability), in minor units and as text.
QVariantList BooksService::salesTaxOwed(const QString &asOfIso) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    QDate asOf = Format::date(asOfIso);
    if (!asOf.isValid()) {
        asOf = todayDate();
    }
    const MoneyFormat money = moneyFormat();
    SalesTax tax(*c);
    for (const TaxAgency &a : tax.agencies()) {
        const qint64 owed = tax.liability(a.id, asOf).minor();
        out << QVariantMap{{QStringLiteral("agencyId"), a.id},
                           {QStringLiteral("name"), a.name},
                           {QStringLiteral("owed"), owed},
                           {QStringLiteral("owedText"), money.text(owed)},
                           {QStringLiteral("owedPlain"), money.plain(owed)}};
    }
    return out;
}

QVariantMap BooksService::recordSalesTaxPayment(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const qint64 agency = Format::id(map.value(QStringLiteral("agencyId")));
    const qint64 bank = Format::id(map.value(QStringLiteral("bankAccountId")));
    const std::optional<Account> from = ChartOfAccounts(*c).account(bank);
    if (!from || (from->type != AccountType::Bank && from->type != AccountType::CreditCard)) {
        return Format::failure(tr("Choose the bank or card account the tax is paid from."));
    }
    QString error;
    const std::optional<qint64> amount = moneyFormat().parse(map.value(QStringLiteral("amount")), &error);
    if (!amount) {
        return Format::failure(error);
    }
    QDate date = Format::date(map.value(QStringLiteral("date")));
    if (!date.isValid()) {
        date = todayDate();
    }
    return Format::result(SalesTax(*c).recordPayment(agency, bank, c->money(*amount), date,
                                                     map.value(QStringLiteral("memo")).toString().trimmed()));
}

} // namespace QindaOffice::Apps::Books
