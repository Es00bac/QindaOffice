// SPDX-License-Identifier: GPL-3.0-or-later
// PayrollService: the company's own payroll tax items and each employee's
// settings for them (payroll_service.h; D-081 amendment).
#include "payroll_service.h"

#include "books_service.h"

#include "accounts.h"
#include "company_file.h"
#include "payroll_items.h"
#include "payroll_records.h"

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;
using namespace ::QindaOffice::Books::Payroll;

namespace {

// "2.7%" or "2.7" → 0.027; "" → zero; nullopt for anything else.
std::optional<Decimal> percentFrom(const QVariant &value)
{
    QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return Decimal();
    }
    if (!text.endsWith(QLatin1Char('%'))) {
        text += QLatin1Char('%');
    }
    return Decimal::parse(text);
}

QString sideLabel(CustomItem::Side side)
{
    return side == CustomItem::Side::Employee ? QObject::tr("Employee (withheld)") : QObject::tr("Employer (company pays)");
}

QString kindLabel(CustomItem::Kind kind)
{
    return kind == CustomItem::Kind::Withholding ? QObject::tr("Withholding, rate set per employee")
                                                 : QObject::tr("Percentage of wages");
}

QString frequencyLabel(PayrollItem::Frequency frequency)
{
    switch (frequency) {
    case PayrollItem::Frequency::Monthly: return QObject::tr("Monthly");
    case PayrollItem::Frequency::Quarterly: return QObject::tr("Quarterly");
    case PayrollItem::Frequency::Annually: return QObject::tr("Annually");
    }
    return {};
}

} // namespace

QVariantList PayrollService::payrollItems() const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = m_books->moneyFormat();
    ChartOfAccounts chart(*c);
    for (const PayrollItem &i : PayrollItems(*c).items(true)) {
        const std::optional<Account> account = chart.account(i.accountId);
        out << QVariantMap{{QStringLiteral("id"), i.id},
                           {QStringLiteral("name"), i.name},
                           {QStringLiteral("side"), customSideKey(i.side)},
                           {QStringLiteral("sideLabel"), sideLabel(i.side)},
                           {QStringLiteral("kind"), customKindKey(i.kind)},
                           {QStringLiteral("kindLabel"), kindLabel(i.kind)},
                           {QStringLiteral("rate"), i.kind == CustomItem::Kind::Tax ? i.rate.toPercentString() : QString()},
                           {QStringLiteral("wageBase"), i.wageBase ? money.plain(i.wageBase->minor()) : QString()},
                           {QStringLiteral("wageBaseText"), i.wageBase ? money.text(i.wageBase->minor()) : QString()},
                           {QStringLiteral("annualLimit"), i.annualLimit ? money.plain(i.annualLimit->minor()) : QString()},
                           {QStringLiteral("accountId"), i.accountId},
                           {QStringLiteral("accountName"), account ? account->name : QString()},
                           {QStringLiteral("agency"), i.agency},
                           {QStringLiteral("frequency"), payrollItemFrequencyKey(i.frequency)},
                           {QStringLiteral("frequencyLabel"), frequencyLabel(i.frequency)},
                           {QStringLiteral("dueDay"), i.dueDay},
                           {QStringLiteral("active"), i.active}};
    }
    return out;
}

QVariantMap PayrollService::savePayrollItem(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    const MoneyFormat money = m_books->moneyFormat();
    PayrollItem i;
    i.id = Format::id(map.value(QStringLiteral("id")));
    i.name = map.value(QStringLiteral("name")).toString();
    i.side = customSideFromKey(map.value(QStringLiteral("side")).toString()).value_or(CustomItem::Side::Employer);
    i.kind = customKindFromKey(map.value(QStringLiteral("kind")).toString()).value_or(CustomItem::Kind::Tax);
    const std::optional<Decimal> rate = percentFrom(map.value(QStringLiteral("rate")));
    if (!rate) {
        return Format::failure(tr("\"%1\" is not a percentage.").arg(map.value(QStringLiteral("rate")).toString()));
    }
    i.rate = *rate;
    for (const auto &[key, field] : {std::pair{"wageBase", &i.wageBase}, std::pair{"annualLimit", &i.annualLimit}}) {
        const QString text = map.value(QString::fromLatin1(key)).toString().trimmed();
        if (text.isEmpty()) {
            continue;
        }
        QString error;
        const std::optional<qint64> v = money.parse(text, &error);
        if (!v) {
            return Format::failure(error);
        }
        *field = money.money(*v);
    }
    i.accountId = Format::id(map.value(QStringLiteral("accountId")));
    i.agency = map.value(QStringLiteral("agency")).toString();
    i.frequency = payrollItemFrequencyFromKey(map.value(QStringLiteral("frequency")).toString())
                      .value_or(PayrollItem::Frequency::Quarterly);
    i.dueDay = map.value(QStringLiteral("dueDay")).toInt();
    i.active = map.value(QStringLiteral("active"), true).toBool();
    return Format::result(PayrollItems(*c).saveItem(i));
}

QVariantMap PayrollService::setPayrollItemActive(qint64 id, bool active)
{
    return file() != nullptr ? Format::result(PayrollItems(*file()).setActive(id, active))
                             : Format::failure(tr("No company is open in this window."));
}

QVariantList PayrollService::customItemsFor(const EmployeePayroll &e) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = m_books->moneyFormat();
    for (const PayrollItem &i : PayrollItems(*c).items()) {
        const EmployeePayroll::CustomWithholding w = e.withholding.value(i.id);
        out << QVariantMap{{QStringLiteral("id"), i.id},
                           {QStringLiteral("name"), i.name},
                           {QStringLiteral("kind"), customKindKey(i.kind)},
                           {QStringLiteral("kindLabel"), kindLabel(i.kind)},
                           {QStringLiteral("subject"), !e.exemptItems.contains(i.id)},
                           {QStringLiteral("percent"), w.percent.isZero() ? QString() : w.percent.toPercentString()},
                           {QStringLiteral("extra"), w.extra.isPositive() ? money.plain(w.extra.minor()) : QString()}};
    }
    return out;
}

bool PayrollService::applyCustomItems(const QVariantList &rows, EmployeePayroll *e, QString *error) const
{
    const MoneyFormat money = m_books->moneyFormat();
    e->exemptItems.clear();
    e->withholding.clear();
    for (const QVariant &v : rows) {
        const QVariantMap row = v.toMap();
        const qint64 id = Format::id(row.value(QStringLiteral("id")));
        if (!row.value(QStringLiteral("subject"), true).toBool()) {
            e->exemptItems.insert(id);
        }
        const std::optional<Decimal> percent = percentFrom(row.value(QStringLiteral("percent")));
        if (!percent) {
            *error = tr("\"%1\" is not a percentage.").arg(row.value(QStringLiteral("percent")).toString());
            return false;
        }
        const std::optional<qint64> extra = money.parse(row.value(QStringLiteral("extra")), error);
        if (!extra) {
            return false;
        }
        if (!percent->isZero() || *extra != 0) {
            e->withholding.insert(id, EmployeePayroll::CustomWithholding{*percent, money.money(*extra)});
        }
    }
    return true;
}

} // namespace QindaOffice::Apps::Books
