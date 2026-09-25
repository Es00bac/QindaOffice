// SPDX-License-Identifier: GPL-3.0-or-later
// Receivables and payables: aging summary/detail, customer and vendor
// balances, open invoices and unpaid bills. Open amounts come from
// Documents::openBalance(id, asOf) — the same number statements and
// document status use (documents.h contract).
#include "reports.h"

#include "company_file.h"
#include "documents.h"
#include "lists.h"
#include "purchases.h"
#include "reports_util.h"
#include "sales.h"

#include <QMap>

namespace QindaOffice::Books {

using namespace ReportUtil;

namespace {

struct OpenItem {
    BusinessDocument doc;
    qint64 open = 0; // signed: credits negative
    int bucket = 0;
    qint64 daysLate = 0;
};

QList<OpenItem> openItems(CompanyFile &file, const QDate &asOf, bool receivable)
{
    Documents documents(file);
    DocumentFilter filter;
    filter.to = asOf;
    filter.includeDrafts = false;
    filter.kinds = receivable
        ? QList<DocKind>{DocKind::Invoice, DocKind::CreditMemo, DocKind::ReceivePayment}
        : QList<DocKind>{DocKind::Bill, DocKind::VendorCredit, DocKind::BillPayment};
    QList<OpenItem> out;
    for (const BusinessDocument &doc : documents.list(filter)) {
        const qint64 open = documents.openBalance(doc.id, asOf).minor();
        if (open == 0) {
            continue;
        }
        const bool charge = doc.kind == DocKind::Invoice || doc.kind == DocKind::Bill;
        const QDate due = charge && doc.dueDate.isValid() ? doc.dueDate : doc.date;
        out << OpenItem{doc, charge ? open : -open, charge ? agingBucket(due, asOf) : 0,
                        charge ? qMax<qint64>(0, due.daysTo(asOf)) : 0};
    }
    return out;
}

const QStringList kBucketIds{QStringLiteral("current"), QStringLiteral("1_30"), QStringLiteral("31_60"),
                             QStringLiteral("61_90"), QStringLiteral("over_90")};
const QStringList kBucketTitles{QStringLiteral("Current"), QStringLiteral("1 - 30"), QStringLiteral("31 - 60"),
                                QStringLiteral("61 - 90"), QStringLiteral("91 and over")};

} // namespace

ReportTable Reports::aging(const QDate &asOf, bool receivable, bool detail) const
{
    const QString side = receivable ? QStringLiteral("ar") : QStringLiteral("ap");
    ReportTable t = makeTable(m_file, side + (detail ? QStringLiteral("_aging_detail") : QStringLiteral("_aging_summary")),
                              (receivable ? QStringLiteral("A/R Aging ") : QStringLiteral("A/P Aging "))
                                  + (detail ? QStringLiteral("Detail") : QStringLiteral("Summary")),
                              periodText({}, asOf));
    const QList<OpenItem> items = openItems(m_file, asOf, receivable);
    Lists lists(m_file);
    auto nameOf = [&lists](qint64 id) {
        const std::optional<Party> p = lists.party(id);
        return p ? p->displayName : QString();
    };
    if (!detail) {
        t.columns = {{QStringLiteral("label"), receivable ? QStringLiteral("Customer") : QStringLiteral("Vendor"),
                      ColumnType::Text}};
        for (int b = 0; b < 5; ++b) {
            t.columns << ReportColumn{kBucketIds[b], kBucketTitles[b], ColumnType::Money};
        }
        t.columns << ReportColumn{QStringLiteral("total"), QStringLiteral("Total"), ColumnType::Money};
        QMap<QString, QPair<qint64, QList<qint64>>> byParty; // sorted by name
        for (const OpenItem &item : items) {
            auto &entry = byParty[nameOf(item.doc.partyId)];
            entry.first = item.doc.partyId;
            if (entry.second.isEmpty()) {
                entry.second = QList<qint64>(6, 0);
            }
            entry.second[item.bucket] += item.open;
            entry.second[5] += item.open;
        }
        QList<qint64> totals(6, 0);
        for (auto it = byParty.cbegin(); it != byParty.cend(); ++it) {
            ReportRow r = moneyRow(RowKind::Account, QStringLiteral("party:%1").arg(it.value().first), it.key(),
                                   it.value().second);
            r.drill.partyId = it.value().first;
            r.drill.to = asOf;
            t.rows << r;
            totals = add(totals, it.value().second);
        }
        t.rows << moneyRow(RowKind::Total, QStringLiteral("total"), QStringLiteral("Total"), totals);
        return t;
    }
    t.columns = {{QStringLiteral("date"), QStringLiteral("Date"), ColumnType::Date},
                 {QStringLiteral("type"), QStringLiteral("Type"), ColumnType::Text},
                 {QStringLiteral("number"), QStringLiteral("Num"), ColumnType::Text},
                 {QStringLiteral("name"), receivable ? QStringLiteral("Customer") : QStringLiteral("Vendor"), ColumnType::Text},
                 {QStringLiteral("due"), QStringLiteral("Due Date"), ColumnType::Date},
                 {QStringLiteral("past_due"), QStringLiteral("Past Due"), ColumnType::Number},
                 {QStringLiteral("open"), QStringLiteral("Open Balance"), ColumnType::Money}};
    qint64 grand = 0;
    for (int b = 0; b < 5; ++b) {
        qint64 subtotal = 0;
        QList<ReportRow> rows;
        for (const OpenItem &item : items) {
            if (item.bucket != b) {
                continue;
            }
            ReportRow r;
            r.kind = RowKind::Detail;
            r.depth = 1;
            r.key = QStringLiteral("doc:%1").arg(item.doc.id);
            r.cells = {ReportCell::ofDate(item.doc.date), ReportCell::ofText(docKindName(item.doc.kind)),
                       ReportCell::ofText(item.doc.number), ReportCell::ofText(nameOf(item.doc.partyId)),
                       ReportCell::ofDate(item.doc.dueDate), ReportCell::ofNumber(item.daysLate),
                       ReportCell::ofMoney(item.open)};
            r.drill.documentIds = {item.doc.id};
            rows << r;
            subtotal += item.open;
        }
        if (rows.isEmpty()) {
            continue;
        }
        ReportRow header;
        header.kind = RowKind::Section;
        header.key = QStringLiteral("section:") + kBucketIds[b];
        header.cells = {ReportCell::ofText(b == 0 ? QStringLiteral("Current")
                                                  : kBucketTitles[b] + QStringLiteral(" days past due"))};
        for (int i = 1; i < t.columns.size(); ++i) {
            header.cells << ReportCell::blank();
        }
        t.rows << header << rows;
        ReportRow total = header;
        total.kind = RowKind::Subtotal;
        total.key = QStringLiteral("total:") + kBucketIds[b];
        total.cells[0] = ReportCell::ofText(QStringLiteral("Total ") + header.cells[0].text);
        total.cells[6] = ReportCell::ofMoney(subtotal);
        t.rows << total;
        grand += subtotal;
    }
    ReportRow total;
    total.kind = RowKind::Total;
    total.key = QStringLiteral("total");
    total.cells = {ReportCell::ofText(QStringLiteral("Total")), ReportCell::blank(), ReportCell::blank(),
                   ReportCell::blank(), ReportCell::blank(), ReportCell::blank(), ReportCell::ofMoney(grand)};
    t.rows << total;
    return t;
}

ReportTable Reports::arAgingSummary(const QDate &asOf) const { return aging(asOf, true, false); }
ReportTable Reports::arAgingDetail(const QDate &asOf) const { return aging(asOf, true, true); }
ReportTable Reports::apAgingSummary(const QDate &asOf) const { return aging(asOf, false, false); }
ReportTable Reports::apAgingDetail(const QDate &asOf) const { return aging(asOf, false, true); }

ReportTable Reports::customerBalances(const QDate &asOf) const
{
    ReportTable t = makeTable(m_file, QStringLiteral("customer_balances"), QStringLiteral("Customer Balances"),
                              periodText({}, asOf));
    t.columns = {{QStringLiteral("label"), QStringLiteral("Customer"), ColumnType::Text},
                 {QStringLiteral("balance"), QStringLiteral("Balance"), ColumnType::Money}};
    Sales sales(m_file);
    qint64 total = 0;
    for (const Party &p : Lists(m_file).parties(PartyKind::Customer, true)) {
        const qint64 balance = sales.customerBalance(p.id, asOf).minor();
        if (balance == 0) {
            continue;
        }
        ReportRow r = moneyRow(RowKind::Account, QStringLiteral("party:%1").arg(p.id), p.displayName, {balance});
        r.drill.partyId = p.id;
        r.drill.to = asOf;
        t.rows << r;
        total += balance;
    }
    t.rows << moneyRow(RowKind::Total, QStringLiteral("total"), QStringLiteral("Total"), {total});
    return t;
}

ReportTable Reports::vendorBalances(const QDate &asOf) const
{
    ReportTable t = makeTable(m_file, QStringLiteral("vendor_balances"), QStringLiteral("Vendor Balances"),
                              periodText({}, asOf));
    t.columns = {{QStringLiteral("label"), QStringLiteral("Vendor"), ColumnType::Text},
                 {QStringLiteral("balance"), QStringLiteral("Balance"), ColumnType::Money}};
    Purchases purchases(m_file);
    qint64 total = 0;
    for (const Party &p : Lists(m_file).parties(PartyKind::Vendor, true)) {
        const qint64 balance = purchases.vendorBalance(p.id, asOf).minor();
        if (balance == 0) {
            continue;
        }
        ReportRow r = moneyRow(RowKind::Account, QStringLiteral("party:%1").arg(p.id), p.displayName, {balance});
        r.drill.partyId = p.id;
        r.drill.to = asOf;
        t.rows << r;
        total += balance;
    }
    t.rows << moneyRow(RowKind::Total, QStringLiteral("total"), QStringLiteral("Total"), {total});
    return t;
}

ReportTable Reports::openDocuments(const QDate &asOf, bool receivable) const
{
    ReportTable t = makeTable(m_file, receivable ? QStringLiteral("open_invoices") : QStringLiteral("unpaid_bills"),
                              receivable ? QStringLiteral("Open Invoices") : QStringLiteral("Unpaid Bills"),
                              periodText({}, asOf));
    t.columns = {{QStringLiteral("date"), QStringLiteral("Date"), ColumnType::Date},
                 {QStringLiteral("number"), QStringLiteral("Num"), ColumnType::Text},
                 {QStringLiteral("name"), receivable ? QStringLiteral("Customer") : QStringLiteral("Vendor"), ColumnType::Text},
                 {QStringLiteral("due"), QStringLiteral("Due Date"), ColumnType::Date},
                 {QStringLiteral("past_due"), QStringLiteral("Past Due"), ColumnType::Number},
                 {QStringLiteral("amount"), QStringLiteral("Amount"), ColumnType::Money},
                 {QStringLiteral("open"), QStringLiteral("Open Balance"), ColumnType::Money}};
    Lists lists(m_file);
    qint64 amount = 0;
    qint64 open = 0;
    for (const OpenItem &item : openItems(m_file, asOf, receivable)) {
        if (item.doc.kind != DocKind::Invoice && item.doc.kind != DocKind::Bill) {
            continue;
        }
        const std::optional<Party> p = lists.party(item.doc.partyId);
        ReportRow r;
        r.kind = RowKind::Detail;
        r.key = QStringLiteral("doc:%1").arg(item.doc.id);
        r.cells = {ReportCell::ofDate(item.doc.date), ReportCell::ofText(item.doc.number),
                   ReportCell::ofText(p ? p->displayName : QString()), ReportCell::ofDate(item.doc.dueDate),
                   ReportCell::ofNumber(item.daysLate), ReportCell::ofMoney(item.doc.total.minor()),
                   ReportCell::ofMoney(item.open)};
        r.drill.documentIds = {item.doc.id};
        t.rows << r;
        amount += item.doc.total.minor();
        open += item.open;
    }
    ReportRow total;
    total.kind = RowKind::Total;
    total.key = QStringLiteral("total");
    total.cells = {ReportCell::ofText(QStringLiteral("Total")), ReportCell::blank(), ReportCell::blank(),
                   ReportCell::blank(), ReportCell::blank(), ReportCell::ofMoney(amount), ReportCell::ofMoney(open)};
    t.rows << total;
    return t;
}

ReportTable Reports::openInvoices(const QDate &asOf) const { return openDocuments(asOf, true); }
ReportTable Reports::unpaidBills(const QDate &asOf) const { return openDocuments(asOf, false); }

} // namespace QindaOffice::Books
