// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: the company, its settings, money text and the clock.
#include "books_service.h"

#include "accounts.h"
#include "books_document.h"
#include "chart_templates.h"
#include "company_file.h"
#include "lists.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

QString basisKey(Basis basis)
{
    return basis == Basis::Cash ? QStringLiteral("cash") : QStringLiteral("accrual");
}

// AGENT-CONTRACT (settings keys, shared with the lead's QML — the setup
// wizard, the preferences page): name, legalName, tradeName, address,
// email, phone, website, taxId, currency, locale, fiscalYearStartMonth,
// basis, invoicePrefix, nextInvoiceNumber, estimatePrefix,
// nextEstimateNumber, creditMemoPrefix, nextCreditMemoNumber,
// salesReceiptPrefix, nextSalesReceiptNumber, defaultTermsId,
// invoiceNotes, paymentInstructions, accentColor, lockDate (read-only),
// hasLogo (read-only). Unknown keys are ignored.
QVariantMap settingsToMap(const CompanySettings &s)
{
    return {
        {QStringLiteral("name"), s.displayName()},
        {QStringLiteral("legalName"), s.legalName},
        {QStringLiteral("tradeName"), s.tradeName},
        {QStringLiteral("address"), s.address},
        {QStringLiteral("email"), s.email},
        {QStringLiteral("phone"), s.phone},
        {QStringLiteral("website"), s.website},
        {QStringLiteral("taxId"), s.taxId},
        {QStringLiteral("currency"), s.currency},
        {QStringLiteral("locale"), s.locale},
        {QStringLiteral("fiscalYearStartMonth"), s.fiscalYearStartMonth},
        {QStringLiteral("basis"), basisKey(s.basis)},
        {QStringLiteral("invoicePrefix"), s.invoicePrefix},
        {QStringLiteral("nextInvoiceNumber"), s.nextInvoiceNumber},
        {QStringLiteral("estimatePrefix"), s.estimatePrefix},
        {QStringLiteral("nextEstimateNumber"), s.nextEstimateNumber},
        {QStringLiteral("creditMemoPrefix"), s.creditMemoPrefix},
        {QStringLiteral("nextCreditMemoNumber"), s.nextCreditMemoNumber},
        {QStringLiteral("salesReceiptPrefix"), s.salesReceiptPrefix},
        {QStringLiteral("nextSalesReceiptNumber"), s.nextSalesReceiptNumber},
        {QStringLiteral("defaultTermsId"), s.defaultTermsId},
        {QStringLiteral("invoiceNotes"), s.invoiceFooter},
        {QStringLiteral("paymentInstructions"), s.paymentInstructions},
        {QStringLiteral("accentColor"), s.accentColor},
        {QStringLiteral("lockDate"), Format::isoDate(s.lockDate)},
        {QStringLiteral("hasLogo"), !s.logo.isEmpty()},
    };
}

// Applies the keys present in `map` over `s`. False with `error` when a
// value is out of range.
bool applySettings(const QVariantMap &map, CompanySettings *s, QString *error)
{
    const auto text = [&map](const char *key, QString *field) {
        const QString k = QString::fromLatin1(key);
        if (map.contains(k)) {
            *field = map.value(k).toString().trimmed();
        }
    };
    const auto number = [&map](const char *key, qint64 *field) {
        const QString k = QString::fromLatin1(key);
        if (map.contains(k) && map.value(k).toDouble() >= 1) {
            *field = qint64(map.value(k).toDouble());
        }
    };
    text("legalName", &s->legalName);
    if (map.contains(QStringLiteral("name"))) {
        // `name` is what the business is called; the legal name defaults
        // to it and the trade name only differs when the two do.
        const QString name = map.value(QStringLiteral("name")).toString().trimmed();
        if (s->legalName.isEmpty()) {
            s->legalName = name;
        }
        s->tradeName = name == s->legalName ? QString() : name;
    } else {
        text("tradeName", &s->tradeName);
    }
    if (map.contains(QStringLiteral("address"))) {
        s->address = map.value(QStringLiteral("address")).toString(); // keep line breaks
    }
    text("email", &s->email);
    text("phone", &s->phone);
    text("website", &s->website);
    text("taxId", &s->taxId);
    if (map.contains(QStringLiteral("currency"))) {
        const QString code = map.value(QStringLiteral("currency")).toString().trimmed().toUpper();
        if (code.size() != 3) {
            *error = QObject::tr("Choose a three-letter currency code (USD, EUR, …).");
            return false;
        }
        s->currency = code;
    }
    text("locale", &s->locale);
    if (map.contains(QStringLiteral("fiscalYearStartMonth"))) {
        const int month = map.value(QStringLiteral("fiscalYearStartMonth")).toInt();
        if (month < 1 || month > 12) {
            *error = QObject::tr("The fiscal year starts in a month from 1 to 12.");
            return false;
        }
        s->fiscalYearStartMonth = month;
    }
    if (map.contains(QStringLiteral("basis"))) {
        s->basis = map.value(QStringLiteral("basis")).toString() == QLatin1String("cash") ? Basis::Cash
                                                                                          : Basis::Accrual;
    }
    text("invoicePrefix", &s->invoicePrefix);
    number("nextInvoiceNumber", &s->nextInvoiceNumber);
    text("estimatePrefix", &s->estimatePrefix);
    number("nextEstimateNumber", &s->nextEstimateNumber);
    text("creditMemoPrefix", &s->creditMemoPrefix);
    number("nextCreditMemoNumber", &s->nextCreditMemoNumber);
    text("salesReceiptPrefix", &s->salesReceiptPrefix);
    number("nextSalesReceiptNumber", &s->nextSalesReceiptNumber);
    if (map.contains(QStringLiteral("defaultTermsId"))) {
        s->defaultTermsId = Format::id(map.value(QStringLiteral("defaultTermsId")));
    }
    if (map.contains(QStringLiteral("invoiceNotes"))) {
        s->invoiceFooter = map.value(QStringLiteral("invoiceNotes")).toString();
    }
    if (map.contains(QStringLiteral("paymentInstructions"))) {
        s->paymentInstructions = map.value(QStringLiteral("paymentInstructions")).toString();
    }
    text("accentColor", &s->accentColor);
    if (s->legalName.isEmpty()) {
        *error = QObject::tr("The company needs a name.");
        return false;
    }
    return true;
}

} // namespace

BooksService::BooksService(BooksDocument *document, QObject *parent)
    : QObject(parent)
    , m_document(document)
{
    const QDate pinned = QDate::fromString(qEnvironmentVariable("QQO_BOOKS_TODAY"), Qt::ISODate);
    if (pinned.isValid()) {
        m_today = pinned;
    }
    if (m_document != nullptr) {
        connect(m_document, &BooksDocument::companyChanged, this, [this] {
            ++m_revision;
            emit companyChanged();
            emit changed();
        });
        connect(m_document, &BooksDocument::booksChanged, this, [this] {
            ++m_revision;
            emit changed();
        });
    }
}

CompanyFile *BooksService::file() const
{
    return m_document != nullptr ? m_document->company() : nullptr;
}

MoneyFormat BooksService::moneyFormat() const
{
    MoneyFormat f;
    if (CompanyFile *c = file()) {
        f.currency = c->currency();
        f.locale = QLocale(c->settings().locale);
    }
    return f;
}

QDate BooksService::todayDate() const
{
    return m_today.isValid() ? m_today : QDate::currentDate();
}

void BooksService::setToday(const QDate &date)
{
    if (m_today == date) {
        return;
    }
    m_today = date;
    emit todayChanged();
}

QVariantMap BooksService::noCompany() const
{
    return Format::failure(tr("No company is open in this window."));
}

QString BooksService::nameOfAccount(qint64 id) const
{
    if (file() == nullptr || id == 0) {
        return QString();
    }
    const std::optional<Account> a = ChartOfAccounts(*file()).account(id);
    return a ? a->fullName : QString();
}

QString BooksService::nameOfParty(qint64 id) const
{
    if (file() == nullptr || id == 0) {
        return QString();
    }
    const std::optional<Party> p = Lists(*file()).party(id);
    return p ? p->displayName : QString();
}

QString BooksService::localPath(const QUrl &url)
{
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

bool BooksService::writeFile(const QString &path, const QByteArray &bytes, QString *error) const
{
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
        if (error != nullptr) {
            *error = out.errorString();
        }
        return false;
    }
    return true;
}

// ---- company -----------------------------------------------------------------

QString BooksService::companyName() const
{
    return file() != nullptr ? file()->settings().displayName() : QString();
}

QVariantMap BooksService::settings() const
{
    return file() != nullptr ? settingsToMap(file()->settings()) : settingsToMap(CompanySettings{});
}

QVariantMap BooksService::saveSettings(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    CompanySettings s = c->settings();
    QString error;
    if (!applySettings(map, &s, &error)) {
        return Format::failure(error);
    }
    return Format::result(c->setSettings(s));
}

QString BooksService::currency() const
{
    return file() != nullptr ? file()->currency() : QString();
}

QString BooksService::lockDate() const
{
    return file() != nullptr ? Format::isoDate(file()->lockDate()) : QString();
}

QString BooksService::formatMoney(qint64 minor) const
{
    return moneyFormat().text(minor);
}

QString BooksService::formatPlain(qint64 minor) const
{
    return moneyFormat().plain(minor);
}

QVariantMap BooksService::parseMoney(const QString &text) const
{
    QString error;
    const std::optional<qint64> minor = moneyFormat().parse(text, &error);
    return {{QStringLiteral("ok"), minor.has_value()},
            {QStringLiteral("minor"), minor.value_or(0)},
            {QStringLiteral("error"), error}};
}

QVariantList BooksService::templates() const
{
    QVariantList out;
    for (const ChartTemplateInfo &t : chartTemplates()) {
        out << QVariantMap{{QStringLiteral("id"), t.id},
                           {QStringLiteral("name"), t.title},
                           {QStringLiteral("description"), t.description}};
    }
    return out;
}

QVariantList BooksService::currencies() const
{
    // The common ones first with their names, then every other code the
    // engine knows (its exponent table), by code.
    static const QList<QPair<const char *, const char *>> named{
        {"USD", QT_TR_NOOP("US Dollar")},        {"EUR", QT_TR_NOOP("Euro")},
        {"GBP", QT_TR_NOOP("British Pound")},    {"CAD", QT_TR_NOOP("Canadian Dollar")},
        {"AUD", QT_TR_NOOP("Australian Dollar")}, {"NZD", QT_TR_NOOP("New Zealand Dollar")},
        {"CHF", QT_TR_NOOP("Swiss Franc")},      {"JPY", QT_TR_NOOP("Japanese Yen")},
        {"SEK", QT_TR_NOOP("Swedish Krona")},    {"NOK", QT_TR_NOOP("Norwegian Krone")},
        {"DKK", QT_TR_NOOP("Danish Krone")},     {"MXN", QT_TR_NOOP("Mexican Peso")},
        {"INR", QT_TR_NOOP("Indian Rupee")},
    };
    QVariantList out;
    QStringList seen;
    for (const auto &[code, name] : named) {
        seen << QString::fromLatin1(code);
        out << QVariantMap{{QStringLiteral("code"), QString::fromLatin1(code)},
                           {QStringLiteral("name"), tr(name)},
                           {QStringLiteral("symbol"), currencyInfo(QString::fromLatin1(code)).symbol}};
    }
    for (const QString &code : knownCurrencyCodes()) {
        if (!seen.contains(code)) {
            out << QVariantMap{{QStringLiteral("code"), code},
                               {QStringLiteral("name"), code},
                               {QStringLiteral("symbol"), currencyInfo(code).symbol}};
        }
    }
    return out;
}

QVariantMap BooksService::createCompany(const QUrl &fileUrl, const QVariantMap &map, const QString &templateId)
{
    if (m_document == nullptr) {
        return Format::failure(tr("This window has no document."));
    }
    if (file() != nullptr) {
        // Rule 1: one company per window. New Company opens a new window.
        return Format::failure(tr("This window already has a company open. Use File ▸ New Company for another."));
    }
    CompanySettings s;
    QString error;
    if (!applySettings(map, &s, &error)) {
        return Format::failure(error);
    }
    QString path = fileUrl.isEmpty() ? QString() : localPath(fileUrl);
    bool ok = false;
    if (path.isEmpty()) {
        // No file chosen yet: a scratch company, dirty until Save As.
        ok = m_document->createUntitled(s, templateId, &error);
    } else {
        if (QFileInfo(path).suffix().isEmpty()) {
            path += QStringLiteral(".") + QString::fromLatin1(BooksDocument::kSuffix);
        }
        if (QFile::exists(path)) {
            return Format::failure(tr("%1 already exists. Choose another name, or open it instead.").arg(path));
        }
        ok = m_document->createCompany(path, s, templateId, &error);
    }
    if (!ok) {
        return Format::failure(error);
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("path"), file()->path());
    return out;
}

QVariantMap BooksService::setLockDate(const QString &isoDate, const QString &reason)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const QDate date = Format::date(isoDate);
    if (!isoDate.trimmed().isEmpty() && !date.isValid()) {
        return Format::failure(tr("\"%1\" is not a date.").arg(isoDate));
    }
    return Format::result(c->setLockDate(date, reason));
}

} // namespace QindaOffice::Apps::Books
