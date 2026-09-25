// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// A realistic small creative studio for the books_io tests: company
// settings with a logo, a customer with an address, items, a sales tax
// rate, an invoice with a discount and tax, a partial payment, an
// expense, and a few more documents for the other forms.

#include "accounts.h"
#include "company_file.h"
#include "lists.h"
#include "purchases.h"
#include "sales.h"
#include "tax.h"

#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QStandardPaths>
#include <QTest>

#include <poppler-qt6.h>

#include <memory>

namespace BooksIoFixture {

using namespace QindaOffice::Books;

inline QByteArray logoPng()
{
    QImage image(240, 240, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x1F, 0x5F, 0xBF));
    p.drawRoundedRect(QRectF(10, 10, 220, 220), 44, 44);
    p.setBrush(Qt::white);
    p.drawEllipse(QRectF(62, 62, 116, 116));
    p.setBrush(QColor(0x1F, 0x5F, 0xBF));
    p.drawEllipse(QRectF(94, 94, 52, 52));
    p.end();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

inline CompanySettings settings()
{
    CompanySettings s;
    s.legalName = QStringLiteral("Northwind Creative Studio LLC");
    s.tradeName = QStringLiteral("Northwind Creative");
    s.address = QStringLiteral("418 Alder Street, Suite 3\nPortland, OR 97204");
    s.email = QStringLiteral("hello@northwind.studio");
    s.phone = QStringLiteral("(503) 555-0142");
    s.website = QStringLiteral("northwind.studio");
    s.taxId = QStringLiteral("EIN 93-4412087");
    s.currency = QStringLiteral("USD");
    s.locale = QStringLiteral("en_US");
    s.logo = logoPng();
    s.paymentInstructions = QStringLiteral(
        "Pay by ACH to Northwind Creative Studio LLC, routing 121000248, account 4432019876, or by card at "
        "northwind.studio/pay. Please include the invoice number.");
    s.invoiceFooter = QStringLiteral("Thank you for your business.");
    return s;
}

struct Company {
    std::unique_ptr<CompanyFile> file;
    qint64 customer = 0;
    qint64 vendor = 0;
    qint64 checking = 0;
    qint64 taxRate = 0;
    qint64 itemBrand = 0;
    qint64 itemIllustration = 0;
    qint64 itemPrint = 0;
    qint64 invoice = 0;
    qint64 payment = 0;
};

inline Money usd(qint64 cents)
{
    return Money(cents, QStringLiteral("USD"));
}

inline Company build(const QString &path)
{
    Company c;
    QString error;
    c.file = CompanyFile::create(path, settings(), QStringLiteral("creative"), &error);
    if (!c.file) {
        qFatal("fixture company: %s", qPrintable(error));
    }
    CompanyFile &f = *c.file;
    ChartOfAccounts chart(f);
    Lists lists(f);
    c.checking = chart.find(QStringLiteral("Checking"));
    chart.setOpeningBalance(c.checking, usd(1500000), QDate(2025, 12, 31));

    const qint64 agency = SalesTax(f).createAgency(TaxAgency{0, QStringLiteral("Multnomah County"), {}, RoundingMode::HalfUp, true}).id;
    c.taxRate = SalesTax(f).createSimpleRate(agency, QStringLiteral("Sales tax"), *Decimal::parse(QStringLiteral("8.25%"))).id;

    Party customer;
    customer.kind = PartyKind::Customer;
    customer.displayName = QStringLiteral("Harbor & Pine Coffee Co.");
    customer.contactName = QStringLiteral("Maya Okafor");
    customer.email = QStringLiteral("accounts@harborpine.coffee");
    customer.billingAddress = QStringLiteral("2201 NW Raleigh Street\nPortland, OR 97210");
    customer.taxRateId = c.taxRate;
    c.customer = lists.createParty(customer).id;
    Party vendor;
    vendor.kind = PartyKind::Vendor;
    vendor.displayName = QStringLiteral("Pixel Print Works");
    vendor.billingAddress = QStringLiteral("77 SE Water Ave\nPortland, OR 97214");
    vendor.is1099 = true;
    c.vendor = lists.createParty(vendor).id;

    const qint64 fees = chart.find(QStringLiteral("Project Fees"));
    const qint64 production = chart.find(QStringLiteral("Production Costs"));
    Item brand{0, QStringLiteral("Brand identity"), ItemKind::Service,
               QStringLiteral("Brand identity system: logo suite, colour and type guidelines"), QStringLiteral("SV-100"),
               usd(240000), usd(0), fees, 0, false, true};
    c.itemBrand = lists.createItem(brand).id;
    Item illustration{0, QStringLiteral("Illustration"), ItemKind::Service, QStringLiteral("Illustration (hourly)"),
                      QStringLiteral("SV-110"), usd(9500), usd(0), fees, 0, false, true};
    c.itemIllustration = lists.createItem(illustration).id;
    Item print{0, QStringLiteral("Printed menus"), ItemKind::NonInventory, QStringLiteral("Printed menu cards, 100 lb cover"),
               QStringLiteral("PR-210"), usd(185), usd(90), fees, production, true, true};
    c.itemPrint = lists.createItem(print).id;

    BusinessDocument inv;
    inv.partyId = c.customer;
    inv.date = QDate(2026, 3, 2);
    inv.discount = Discount::ofPercent(*Decimal::parse(QStringLiteral("10%")));
    inv.message = QStringLiteral("Thanks for a great kickoff, Maya. The final logo files ship with the brand book.");
    DocumentLine l1;
    l1.itemId = c.itemBrand;
    DocumentLine l2;
    l2.itemId = c.itemIllustration;
    l2.quantity = *Decimal::parse(QStringLiteral("12.5"));
    DocumentLine l3;
    l3.itemId = c.itemPrint;
    l3.quantity = Decimal::fromInteger(400);
    DocumentLine l4;
    l4.accountId = fees;
    l4.description = QStringLiteral("Rush turnaround on the spring menu (two business days)");
    l4.amount = usd(15000);
    inv.lines = {l1, l2, l3, l4};
    const Result created = Sales(f).createInvoice(inv);
    if (!created.ok) {
        qFatal("fixture invoice: %s", qPrintable(created.error));
    }
    c.invoice = created.id;
    ReceivePaymentInput pay;
    pay.customerId = c.customer;
    pay.date = QDate(2026, 3, 16);
    pay.amount = usd(150000);
    pay.reference = QStringLiteral("Check 2291");
    pay.applications = {{c.invoice, usd(150000)}};
    c.payment = Sales(f).receivePayment(pay).id;

    BusinessDocument expense;
    expense.kind = DocKind::Expense;
    expense.partyId = c.vendor;
    expense.date = QDate(2026, 3, 9);
    expense.accountId = c.checking;
    DocumentLine e1;
    e1.accountId = production;
    e1.description = QStringLiteral("Menu printing");
    e1.amount = usd(36000);
    DocumentLine e2;
    e2.accountId = chart.find(QStringLiteral("Software and Subscriptions"));
    e2.description = QStringLiteral("Design software");
    e2.amount = usd(8999);
    expense.lines = {e1, e2};
    Purchases(f).createExpense(expense);
    return c;
}

// Poppler text of every page.
inline QStringList pdfPages(const QString &path)
{
    QStringList pages;
    std::unique_ptr<Poppler::Document> doc = Poppler::Document::load(path);
    if (!doc) {
        return pages;
    }
    for (int i = 0; i < doc->numPages(); ++i) {
        pages << doc->page(i)->text(QRectF());
    }
    return pages;
}

// Rasterises page 1 into the validation gallery inbox (skipped silently
// when the home directory is not writable).
inline QString saveGalleryPng(const QString &pdf, const QString &name, int page = 0)
{
    std::unique_ptr<Poppler::Document> doc = Poppler::Document::load(pdf);
    if (!doc || page >= doc->numPages()) {
        return {};
    }
    doc->setRenderHint(Poppler::Document::Antialiasing);
    doc->setRenderHint(Poppler::Document::TextAntialiasing);
    const QImage image = doc->page(page)->renderToImage(110, 110);
    const QString dir = QDir::homePath() + QStringLiteral("/.local/share/qindaoffice-validation-gallery/inbox");
    if (!QDir().mkpath(dir)) {
        return {};
    }
    const QString path = dir + QLatin1Char('/') + name;
    return image.save(path) ? path : QString();
}

} // namespace BooksIoFixture
