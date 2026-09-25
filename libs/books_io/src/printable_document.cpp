// SPDX-License-Identifier: GPL-3.0-or-later
#include "printable_document.h"

#include "company_file.h"
#include "connections.h"
#include "documents.h"
#include "lists.h"
#include "sales.h"

namespace QindaOffice::Books {

namespace {

QStringList splitLines(const QString &text)
{
    QStringList out;
    for (const QString &line : text.split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty()) {
            out << line.trimmed();
        }
    }
    return out;
}

void fillCompany(CompanyFile &file, PrintableDocument *out)
{
    const CompanySettings s = file.settings();
    out->companyName = s.displayName();
    out->companyLines = splitLines(s.address);
    QStringList contact;
    for (const QString &part : {s.email, s.phone, s.website}) {
        if (!part.trimmed().isEmpty()) {
            contact << part.trimmed();
        }
    }
    if (!contact.isEmpty()) {
        out->companyLines << contact.join(QStringLiteral("  ·  "));
    }
    out->taxId = s.taxId;
    out->logo = s.logo;
    out->accent = QColor(s.accentColor);
    if (!out->accent.isValid()) {
        out->accent = QColor(0x1F, 0x5F, 0xBF);
    }
    out->paymentInstructions = s.paymentInstructions;
    out->footer = s.invoiceFooter;
    out->locale = companyLocale(file);
}

void fillParty(CompanyFile &file, qint64 partyId, PrintableDocument *out)
{
    const std::optional<Party> party = Lists(file).party(partyId);
    if (!party) {
        return;
    }
    out->partyName = party->displayName;
    if (!party->companyName.isEmpty() && party->companyName != party->displayName) {
        out->partyLines << party->companyName;
    }
    if (!party->contactName.isEmpty()) {
        out->partyLines << QStringLiteral("Attn: ") + party->contactName;
    }
    out->partyLines << splitLines(party->billingAddress);
    if (!party->email.isEmpty()) {
        out->partyLines << party->email;
    }
}

QString termsName(CompanyFile &file, qint64 termsId)
{
    for (const Terms &t : Lists(file).terms(true)) {
        if (t.id == termsId) {
            return t.name;
        }
    }
    return QString();
}

} // namespace

QLocale companyLocale(CompanyFile &file)
{
    const QLocale locale(file.settings().locale);
    return locale.language() == QLocale::C ? QLocale(QLocale::English, QLocale::UnitedStates) : locale;
}

QString formatDate(const QDate &date, const QLocale &locale)
{
    return date.isValid() ? locale.toString(date, QStringLiteral("MMM d, yyyy")) : QString();
}

bool buildPrintableDocument(CompanyFile &file, qint64 docId, PrintableDocument *out, QString *error)
{
    Documents documents(file);
    const std::optional<BusinessDocument> doc = documents.load(docId);
    auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!doc) {
        return fail(QStringLiteral("no such document"));
    }
    if (doc->state == DocState::Void) {
        return fail(QStringLiteral("a void document is not printed"));
    }
    PrintableDocument p;
    QString numberLabel;
    switch (doc->kind) {
    case DocKind::Invoice:
        p.kind = PrintableDocument::Kind::Invoice;
        p.title = QStringLiteral("INVOICE");
        numberLabel = QStringLiteral("Invoice #");
        break;
    case DocKind::Estimate:
        p.kind = PrintableDocument::Kind::Estimate;
        p.title = QStringLiteral("ESTIMATE");
        numberLabel = QStringLiteral("Estimate #");
        break;
    case DocKind::SalesReceipt:
        p.kind = PrintableDocument::Kind::SalesReceipt;
        p.title = QStringLiteral("SALES RECEIPT");
        numberLabel = QStringLiteral("Receipt #");
        break;
    case DocKind::CreditMemo:
        p.kind = PrintableDocument::Kind::CreditMemo;
        p.title = QStringLiteral("CREDIT MEMO");
        numberLabel = QStringLiteral("Credit memo #");
        break;
    default:
        return fail(QStringLiteral("%1 has no printed form").arg(docKindName(doc->kind)));
    }
    fillCompany(file, &p);
    fillParty(file, doc->partyId, &p);
    const QLocale &loc = p.locale;
    p.partyLabel = doc->kind == DocKind::SalesReceipt ? QStringLiteral("Sold to") : QStringLiteral("Bill to");
    p.meta << qMakePair(numberLabel, doc->number) << qMakePair(QStringLiteral("Date"), formatDate(doc->date, loc));
    if (doc->kind == DocKind::Invoice) {
        const QString terms = termsName(file, doc->termsId);
        if (!terms.isEmpty()) {
            p.meta << qMakePair(QStringLiteral("Terms"), terms);
        }
        p.meta << qMakePair(QStringLiteral("Due date"), formatDate(doc->dueDate, loc));
    } else if (doc->kind == DocKind::Estimate && doc->dueDate.isValid()) {
        p.meta << qMakePair(QStringLiteral("Valid until"), formatDate(doc->dueDate, loc));
    }
    if (!doc->reference.isEmpty()) {
        p.meta << qMakePair(QStringLiteral("Reference"), doc->reference);
    }
    p.columns = {{QStringLiteral("Description"), 7, false},
                 {QStringLiteral("Qty"), 1, true},
                 {QStringLiteral("Rate"), 2, true},
                 {QStringLiteral("Amount"), 2, true}};
    const DocumentTotals totals = documents.computeTotals(*doc);
    for (qsizetype i = 0; i < doc->lines.size(); ++i) {
        const DocumentLine &line = doc->lines[i];
        const Money amount = i < totals.lineAmounts.size() ? totals.lineAmounts[i] : line.amount;
        p.rows << QStringList{line.description,
                              line.unitPrice.isZero() ? QString() : line.quantity.toString(),
                              line.unitPrice.isZero() ? QString() : line.unitPrice.format(loc, false),
                              amount.format(loc, false)};
    }
    p.totals << PrintableTotal{QStringLiteral("Subtotal"), totals.subtotal.format(loc), false};
    if (!totals.discount.isZero()) {
        const QString label = doc->discount.kind == Discount::Kind::Percent
            ? QStringLiteral("Discount (%1)").arg(doc->discount.percent.toPercentString())
            : QStringLiteral("Discount");
        p.totals << PrintableTotal{label, (-totals.discount).format(loc), false};
    }
    for (const TaxShare &share : totals.taxes) {
        p.totals << PrintableTotal{QStringLiteral("%1 (%2)").arg(share.name, share.rate.toPercentString()),
                                   share.amount.format(loc), false};
    }
    p.totals << PrintableTotal{QStringLiteral("Total"), totals.total.format(loc), true};
    switch (doc->kind) {
    case DocKind::Invoice: {
        const Money paid = documents.appliedTo(docId);
        const Money due = totals.total - paid;
        if (!paid.isZero()) {
            p.totals << PrintableTotal{QStringLiteral("Paid"), (-paid).format(loc), false};
        }
        p.totals << PrintableTotal{QStringLiteral("Balance due"), due.format(loc), true};
        p.highlightLabel = QStringLiteral("Balance due");
        p.highlightAmount = due.format(loc);
        if (const std::optional<PaymentLinkRecord> link = Connections(file).currentPaymentLink(docId)) {
            p.payOnlineUrl = link->url;
        }
        break;
    }
    case DocKind::Estimate:
        p.highlightLabel = QStringLiteral("Estimate total");
        p.highlightAmount = totals.total.format(loc);
        p.paymentInstructions.clear();
        break;
    case DocKind::SalesReceipt:
        p.totals << PrintableTotal{QStringLiteral("Amount paid"), totals.total.format(loc), true};
        p.highlightLabel = QStringLiteral("Amount paid");
        p.highlightAmount = totals.total.format(loc);
        p.paymentInstructions.clear();
        break;
    default: { // credit memo
        const Money remaining = documents.openBalance(docId);
        p.totals << PrintableTotal{QStringLiteral("Credit remaining"), remaining.format(loc), true};
        p.highlightLabel = QStringLiteral("Credit remaining");
        p.highlightAmount = remaining.format(loc);
        p.paymentInstructions.clear();
        break;
    }
    }
    p.message = doc->message;
    *out = p;
    return true;
}

bool buildPrintableStatement(CompanyFile &file, qint64 customerId, const QDate &from, const QDate &to,
                             PrintableDocument *out, QString *error)
{
    const std::optional<Party> party = Lists(file).party(customerId);
    if (!party || party->kind != PartyKind::Customer || !from.isValid() || !to.isValid() || to < from) {
        if (error != nullptr) {
            *error = QStringLiteral("a statement needs a customer and a valid period");
        }
        return false;
    }
    PrintableDocument p;
    p.kind = PrintableDocument::Kind::Statement;
    p.title = QStringLiteral("STATEMENT");
    fillCompany(file, &p);
    fillParty(file, customerId, &p);
    p.partyLabel = QStringLiteral("To");
    const QLocale &loc = p.locale;
    const CustomerStatement s = Sales(file).statement(customerId, from, to);
    p.meta << qMakePair(QStringLiteral("Statement date"), formatDate(to, loc))
           << qMakePair(QStringLiteral("Period"), formatDate(from, loc) + QStringLiteral(" – ") + formatDate(to, loc));
    p.columns = {{QStringLiteral("Date"), 2, false},
                 {QStringLiteral("Activity"), 6, false},
                 {QStringLiteral("Amount"), 2, true},
                 {QStringLiteral("Balance"), 2, true}};
    p.rows << QStringList{formatDate(from, loc), QStringLiteral("Balance forward"), QString(), s.opening.format(loc, false)};
    for (const StatementLine &line : s.lines) {
        p.rows << QStringList{formatDate(line.date, loc), line.description, line.amount.format(loc, false),
                              line.balance.format(loc, false)};
    }
    p.totals << PrintableTotal{QStringLiteral("Amount due"), s.closing.format(loc), true};
    p.highlightLabel = QStringLiteral("Amount due");
    p.highlightAmount = s.closing.format(loc);
    p.strip = {{QStringLiteral("Current"), s.aging.current.format(loc)},
               {QStringLiteral("1–30 days"), s.aging.days1to30.format(loc)},
               {QStringLiteral("31–60 days"), s.aging.days31to60.format(loc)},
               {QStringLiteral("61–90 days"), s.aging.days61to90.format(loc)},
               {QStringLiteral("Over 90 days"), s.aging.over90.format(loc)}};
    *out = p;
    return true;
}

} // namespace QindaOffice::Books
