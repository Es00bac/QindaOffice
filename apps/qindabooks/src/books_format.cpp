// SPDX-License-Identifier: GPL-3.0-or-later
#include "books_format.h"

#include "money.h"

#include <QCoreApplication>
#include <QDateTime>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("QindaBooks", text);
}

struct KindRow {
    DocKind kind;
    const char *key;
};

// AGENT-CONTRACT: these keys are the QML API's document kinds (and the
// `kind` argument of BooksHost::documentEditorRequested). Never rename.
constexpr KindRow kKinds[] = {
    {DocKind::Invoice, "invoice"},
    {DocKind::Estimate, "estimate"},
    {DocKind::SalesReceipt, "salesReceipt"},
    {DocKind::ReceivePayment, "payment"},
    {DocKind::CreditMemo, "creditMemo"},
    {DocKind::Refund, "refund"},
    {DocKind::Bill, "bill"},
    {DocKind::BillPayment, "billPayment"},
    {DocKind::Expense, "expense"},
    {DocKind::Check, "check"},
    {DocKind::CreditCardCharge, "creditCardCharge"},
    {DocKind::VendorCredit, "vendorCredit"},
    {DocKind::JournalEntry, "journalEntry"},
    {DocKind::Transfer, "transfer"},
    {DocKind::Deposit, "deposit"},
    {DocKind::SalesTaxPayment, "salesTaxPayment"},
    {DocKind::InvoiceTemplate, "recurringTemplate"},
    {DocKind::OpeningBalance, "openingBalance"},
    {DocKind::PurchaseOrder, "purchaseOrder"}, // M68 (D-077)
};

QString rowKindKey(RowKind kind)
{
    switch (kind) {
    case RowKind::Section: return QStringLiteral("section");
    case RowKind::Account: return QStringLiteral("account");
    case RowKind::Detail: return QStringLiteral("detail");
    case RowKind::Subtotal: return QStringLiteral("subtotal");
    case RowKind::Total: return QStringLiteral("total");
    case RowKind::Spacer: return QStringLiteral("spacer");
    }
    return QStringLiteral("detail");
}

QString columnTypeKey(ColumnType type)
{
    switch (type) {
    case ColumnType::Text: return QStringLiteral("text");
    case ColumnType::Money: return QStringLiteral("money");
    case ColumnType::Number: return QStringLiteral("number");
    case ColumnType::Percent: return QStringLiteral("percent");
    case ColumnType::Date: return QStringLiteral("date");
    case ColumnType::Duration: return QStringLiteral("duration"); // M68: minutes, shown h:mm
    }
    return QStringLiteral("text");
}

} // namespace

// ---- MoneyFormat -----------------------------------------------------------

QString MoneyFormat::text(qint64 minor) const
{
    return Money(minor, currency).format(locale, true);
}

QString MoneyFormat::plain(qint64 minor) const
{
    return Money(minor, currency).toPlainString();
}

double MoneyFormat::chart(qint64 minor) const
{
    // AGENT-GUARD: the ONE place a double meets an amount — chart geometry
    // for the dashboard (`…Chart` fields). Never parse it back.
    return double(minor) / double(pow10i(currencyInfo(currency).exponent));
}

std::optional<qint64> MoneyFormat::parse(const QVariant &value, QString *error) const
{
    const int type = value.typeId();
    if (type == QMetaType::Int || type == QMetaType::LongLong || type == QMetaType::UInt
        || type == QMetaType::ULongLong || type == QMetaType::Double) {
        // Minor units handed back from our own maps (JS hands every number
        // over as a double; an integral one is exact up to 2^53).
        const double d = value.toDouble();
        if (d != double(qint64(d))) {
            if (error != nullptr) {
                *error = tr("%1 is not a whole number of minor units").arg(value.toString());
            }
            return std::nullopt;
        }
        return qint64(d);
    }
    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return 0;
    }
    std::optional<Money> money = Money::parse(text, currency, locale);
    if (!money) {
        money = Money::parse(text, currency, QLocale::c());
    }
    if (!money) {
        if (error != nullptr) {
            *error = tr("\"%1\" is not an amount in %2 (at most %3 decimals)")
                         .arg(text, currency)
                         .arg(currencyInfo(currency).exponent);
        }
        return std::nullopt;
    }
    return money->minor();
}

// ---- Format ------------------------------------------------------------------

namespace Format {

QString isoDate(const QDate &date)
{
    return date.isValid() ? date.toString(Qt::ISODate) : QString();
}

QDate date(const QVariant &value)
{
    if (value.typeId() == QMetaType::QDate) {
        return value.toDate();
    }
    if (value.typeId() == QMetaType::QDateTime) {
        return value.toDateTime().date();
    }
    return QDate::fromString(value.toString().trimmed().left(10), Qt::ISODate);
}

qint64 id(const QVariant &value)
{
    return value.isValid() ? qint64(value.toDouble()) : 0;
}

QString kindKey(DocKind kind)
{
    for (const KindRow &row : kKinds) {
        if (row.kind == kind) {
            return QString::fromLatin1(row.key);
        }
    }
    return QStringLiteral("journalEntry");
}

std::optional<DocKind> kindFromKey(const QString &key)
{
    for (const KindRow &row : kKinds) {
        if (key == QLatin1String(row.key)) {
            return row.kind;
        }
    }
    return std::nullopt;
}

QString kindLabel(DocKind kind)
{
    return docKindName(kind);
}

QString statusKey(DocumentStatus status)
{
    return documentStatusName(status);
}

QString statusLabel(DocumentStatus status)
{
    switch (status) {
    case DocumentStatus::Draft: return tr("Draft");
    case DocumentStatus::Open: return tr("Open");
    case DocumentStatus::Partial: return tr("Partially paid");
    case DocumentStatus::Paid: return tr("Paid");
    case DocumentStatus::Overdue: return tr("Overdue");
    case DocumentStatus::Void: return tr("Void");
    case DocumentStatus::Pending: return tr("Pending");
    case DocumentStatus::Closed: return tr("Closed");
    case DocumentStatus::PartiallyReceived: return tr("Partially received");
    }
    return QString();
}

QVariantMap result(const Result &r)
{
    return {{QStringLiteral("ok"), r.ok},
            {QStringLiteral("id"), r.id},
            {QStringLiteral("error"), r.error},
            {QStringLiteral("code"), int(r.code)}};
}

QVariantMap failure(const QString &error)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("id"), 0}, {QStringLiteral("error"), error}};
}

QVariantMap success(qint64 id)
{
    return {{QStringLiteral("ok"), true}, {QStringLiteral("id"), id}, {QStringLiteral("error"), QString()}};
}

QVariantMap drill(const DrillDown &d)
{
    QVariantList docs;
    for (qint64 id : d.documentIds) {
        docs << id;
    }
    return {{QStringLiteral("accountId"), d.accountId},
            {QStringLiteral("partyId"), d.partyId},
            {QStringLiteral("itemId"), d.itemId},
            {QStringLiteral("from"), isoDate(d.from)},
            {QStringLiteral("to"), isoDate(d.to)},
            {QStringLiteral("documentIds"), docs},
            {QStringLiteral("empty"), d.isEmpty()}};
}

DrillDown drillFrom(const QVariantMap &map)
{
    DrillDown d;
    d.accountId = id(map.value(QStringLiteral("accountId")));
    d.partyId = id(map.value(QStringLiteral("partyId")));
    d.itemId = id(map.value(QStringLiteral("itemId")));
    d.from = date(map.value(QStringLiteral("from")));
    d.to = date(map.value(QStringLiteral("to")));
    for (const QVariant &v : map.value(QStringLiteral("documentIds")).toList()) {
        d.documentIds << id(v);
    }
    return d;
}

QVariantMap report(const ReportTable &table, const MoneyFormat &money)
{
    QVariantList columns;
    for (const ReportColumn &c : table.columns) {
        columns << QVariantMap{{QStringLiteral("id"), c.id},
                               {QStringLiteral("title"), c.title},
                               {QStringLiteral("type"), columnTypeKey(c.type)}};
    }
    QVariantList rows;
    for (const ReportRow &r : table.rows) {
        QVariantList cells;
        for (const ReportCell &cell : r.cells) {
            QString text = cell.display(table.currency, money.locale);
            if (cell.type == ColumnType::Date && !cell.empty) {
                text = money.locale.toString(cell.date, QLocale::ShortFormat);
            }
            cells << QVariantMap{{QStringLiteral("text"), text},
                                 {QStringLiteral("value"), cell.value},
                                 {QStringLiteral("type"), columnTypeKey(cell.type)},
                                 {QStringLiteral("negative"), !cell.empty && cell.value < 0
                                                                  && cell.type != ColumnType::Text},
                                 {QStringLiteral("empty"), cell.empty}};
        }
        rows << QVariantMap{{QStringLiteral("kind"), rowKindKey(r.kind)},
                            {QStringLiteral("depth"), r.depth},
                            {QStringLiteral("key"), r.key},
                            {QStringLiteral("cells"), cells},
                            {QStringLiteral("drill"), drill(r.drill)}};
    }
    return {{QStringLiteral("id"), table.id},
            {QStringLiteral("title"), table.title},
            {QStringLiteral("company"), table.company},
            {QStringLiteral("subtitle"), table.subtitle},
            {QStringLiteral("currency"), table.currency},
            {QStringLiteral("columns"), columns},
            {QStringLiteral("rows"), rows},
            {QStringLiteral("notes"), table.notes}};
}

QString percentText(const Decimal &fraction)
{
    QString text = fraction.toPercentString();
    if (text.endsWith(QLatin1Char('%'))) {
        text.chop(1);
    }
    return text;
}

} // namespace Format
} // namespace QindaOffice::Apps::Books
