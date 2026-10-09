// SPDX-License-Identifier: GPL-3.0-or-later
// CompanySettings persistence (the `settings` key/value table), the lock
// date and the document number counters.
#include "company_file.h"
#include "company_settings.h"
#include "sql_util.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

namespace QindaOffice::Books {

QDate CompanySettings::fiscalYearStart(const QDate &date) const
{
    const int month = qBound(1, fiscalYearStartMonth, 12);
    const int year = date.month() >= month ? date.year() : date.year() - 1;
    return QDate(year, month, 1);
}

namespace {

// AGENT-CONTRACT (storage): these keys are the settings table's schema.
// Add keys freely; never rename one (older files carry it).
QVariantMap toMap(const CompanySettings &s)
{
    return {
        {QStringLiteral("legal_name"), s.legalName},
        {QStringLiteral("trade_name"), s.tradeName},
        {QStringLiteral("address"), s.address},
        {QStringLiteral("email"), s.email},
        {QStringLiteral("phone"), s.phone},
        {QStringLiteral("website"), s.website},
        {QStringLiteral("tax_id"), s.taxId},
        {QStringLiteral("currency"), s.currency},
        {QStringLiteral("locale"), s.locale},
        {QStringLiteral("fiscal_year_start_month"), s.fiscalYearStartMonth},
        {QStringLiteral("basis"), int(s.basis)},
        {QStringLiteral("lock_date"), Sql::isoDate(s.lockDate)},
        {QStringLiteral("invoice_prefix"), s.invoicePrefix},
        {QStringLiteral("next_invoice_number"), s.nextInvoiceNumber},
        {QStringLiteral("estimate_prefix"), s.estimatePrefix},
        {QStringLiteral("next_estimate_number"), s.nextEstimateNumber},
        {QStringLiteral("credit_memo_prefix"), s.creditMemoPrefix},
        {QStringLiteral("next_credit_memo_number"), s.nextCreditMemoNumber},
        {QStringLiteral("sales_receipt_prefix"), s.salesReceiptPrefix},
        {QStringLiteral("next_sales_receipt_number"), s.nextSalesReceiptNumber},
        {QStringLiteral("purchase_order_prefix"), s.purchaseOrderPrefix},
        {QStringLiteral("next_purchase_order_number"), s.nextPurchaseOrderNumber},
        {QStringLiteral("default_terms_id"), s.defaultTermsId},
        {QStringLiteral("logo"), s.logo},
        {QStringLiteral("accent_color"), s.accentColor},
        {QStringLiteral("payment_instructions"), s.paymentInstructions},
        {QStringLiteral("invoice_footer"), s.invoiceFooter},
    };
}

QString settingsJson(const CompanySettings &s)
{
    QVariantMap map = toMap(s);
    map.remove(QStringLiteral("logo"));
    map.insert(QStringLiteral("logo_bytes"), s.logo.size());
    return QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact));
}

struct Counter {
    QString prefixKey;
    QString nextKey;
};

std::optional<Counter> counterFor(DocKind kind)
{
    switch (kind) {
    case DocKind::Invoice:
        return Counter{QStringLiteral("invoice_prefix"), QStringLiteral("next_invoice_number")};
    case DocKind::Estimate:
        return Counter{QStringLiteral("estimate_prefix"), QStringLiteral("next_estimate_number")};
    case DocKind::CreditMemo:
        return Counter{QStringLiteral("credit_memo_prefix"), QStringLiteral("next_credit_memo_number")};
    case DocKind::SalesReceipt:
        return Counter{QStringLiteral("sales_receipt_prefix"),
                       QStringLiteral("next_sales_receipt_number")};
    case DocKind::PurchaseOrder:
        return Counter{QStringLiteral("purchase_order_prefix"),
                       QStringLiteral("next_purchase_order_number")};
    default:
        return std::nullopt;
    }
}

} // namespace

CompanySettings CompanyFile::settings() const
{
    QVariantMap map;
    {
        QSqlQuery q(database());
        Sql::run(q, QStringLiteral("SELECT key, value FROM settings"));
        while (q.next()) {
            map.insert(q.value(0).toString(), q.value(1));
        }
    }
    CompanySettings s;
    auto text = [&](const char *key, QString &field) {
        if (map.contains(QLatin1String(key))) {
            field = map.value(QLatin1String(key)).toString();
        }
    };
    auto number = [&](const char *key, qint64 &field) {
        if (map.contains(QLatin1String(key))) {
            field = map.value(QLatin1String(key)).toLongLong();
        }
    };
    text("legal_name", s.legalName);
    text("trade_name", s.tradeName);
    text("address", s.address);
    text("email", s.email);
    text("phone", s.phone);
    text("website", s.website);
    text("tax_id", s.taxId);
    text("currency", s.currency);
    text("locale", s.locale);
    s.fiscalYearStartMonth = map.value(QStringLiteral("fiscal_year_start_month"), 1).toInt();
    s.basis = Basis(map.value(QStringLiteral("basis"), 0).toInt());
    s.lockDate = Sql::fromIso(map.value(QStringLiteral("lock_date")));
    text("invoice_prefix", s.invoicePrefix);
    number("next_invoice_number", s.nextInvoiceNumber);
    text("estimate_prefix", s.estimatePrefix);
    number("next_estimate_number", s.nextEstimateNumber);
    text("credit_memo_prefix", s.creditMemoPrefix);
    number("next_credit_memo_number", s.nextCreditMemoNumber);
    text("sales_receipt_prefix", s.salesReceiptPrefix);
    number("next_sales_receipt_number", s.nextSalesReceiptNumber);
    text("purchase_order_prefix", s.purchaseOrderPrefix);
    number("next_purchase_order_number", s.nextPurchaseOrderNumber);
    number("default_terms_id", s.defaultTermsId);
    s.logo = map.value(QStringLiteral("logo")).toByteArray();
    text("accent_color", s.accentColor);
    text("payment_instructions", s.paymentInstructions);
    text("invoice_footer", s.invoiceFooter);
    return s;
}

Result CompanyFile::setSettings(const CompanySettings &settings)
{
    if (settings.legalName.trimmed().isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a company needs a legal name"));
    }
    if (settings.fiscalYearStartMonth < 1 || settings.fiscalYearStartMonth > 12) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("fiscal year start month must be 1-12"));
    }
    const CompanySettings before = this->settings();
    const QString currency = settings.currency.trimmed().toUpper();
    if (!before.currency.isEmpty() && currency != before.currency && !m_currency.isEmpty()
        && Sql::scalarInt(database(), QStringLiteral("SELECT COUNT(*) FROM journal_lines")) > 0) {
        return Result::failure(ErrorCode::CurrencyMismatch,
                               QStringLiteral("the currency cannot change once transactions exist"));
    }
    SqlTransaction txn(*this);
    if (!txn.begun()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    QString error;
    const QVariantMap map = toMap(settings);
    for (auto it = map.cbegin(); it != map.cend(); ++it) {
        QVariant value = it.value();
        if (it.key() == QLatin1String("currency")) {
            value = currency;
        }
        if (!Sql::exec(database(), QStringLiteral("INSERT OR REPLACE INTO settings(key, value) VALUES(?, ?)"),
                       {it.key(), value}, &error)) {
            return Result::failure(ErrorCode::Storage, error);
        }
    }
    if (!audit(QStringLiteral("update"), QStringLiteral("settings"), 0,
               before.legalName.isEmpty() ? QString() : settingsJson(before), settingsJson(settings))
        || !txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    m_currency = currency;
    return Result::success();
}

QDate CompanyFile::lockDate() const
{
    return Sql::fromIso(Sql::scalar(database(), QStringLiteral("SELECT value FROM settings WHERE key = 'lock_date'")));
}

Result CompanyFile::setLockDate(const QDate &date, const QString &reason)
{
    const QDate before = lockDate();
    SqlTransaction txn(*this);
    QString error;
    if (!Sql::exec(database(), QStringLiteral("INSERT OR REPLACE INTO settings(key, value) VALUES('lock_date', ?)"),
                   {Sql::isoDate(date)}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    audit(QStringLiteral("lock"), QStringLiteral("settings"), 0,
          QStringLiteral("{\"lock_date\":\"%1\"}").arg(Sql::isoDate(before)),
          QStringLiteral("{\"lock_date\":\"%1\"}").arg(Sql::isoDate(date)), reason);
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success();
}

QString CompanyFile::peekNextNumber(DocKind kind) const
{
    const std::optional<Counter> counter = counterFor(kind);
    if (!counter) {
        return QString();
    }
    const QString prefix = Sql::scalar(database(), QStringLiteral("SELECT value FROM settings WHERE key = ?"),
                                       {counter->prefixKey}).toString();
    const qint64 next = Sql::scalarInt(database(), QStringLiteral("SELECT value FROM settings WHERE key = ?"),
                                       {counter->nextKey}, 1);
    return prefix + QString::number(next);
}

QString CompanyFile::takeNextNumber(DocKind kind)
{
    const std::optional<Counter> counter = counterFor(kind);
    if (!counter) {
        return QString();
    }
    const QString number = peekNextNumber(kind);
    const qint64 next = Sql::scalarInt(database(), QStringLiteral("SELECT value FROM settings WHERE key = ?"),
                                       {counter->nextKey}, 1);
    Sql::exec(database(), QStringLiteral("INSERT OR REPLACE INTO settings(key, value) VALUES(?, ?)"),
              {counter->nextKey, next + 1});
    return number;
}

} // namespace QindaOffice::Books
