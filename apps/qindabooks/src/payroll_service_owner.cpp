// SPDX-License-Identifier: GPL-3.0-or-later
// PayrollService: owner pay, the estimated-tax planner over the books'
// profit, and due-date reminders (payroll_service.h).
#include "payroll_service.h"

#include "books_service.h"

#include "company_file.h"
#include "owner_pay.h"
#include "payroll_ledger.h"
#include "payroll_liabilities.h"
#include "payroll_records.h"
#include "reports.h"

#include "estimated_tax.h"
#include "payroll_calendar.h"
#include "payroll_tables.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;
using namespace ::QindaOffice::Books::Payroll;

namespace {

const QStringList kProfileMoney{QStringLiteral("projectedProfit"), QStringLiteral("otherIncome"),
                                QStringLiteral("wagesForSocialSecurity"), QStringLiteral("wagesForMedicare"),
                                QStringLiteral("businessAdjustments"), QStringLiteral("otherAdjustments"),
                                QStringLiteral("itemizedDeductions"), QStringLiteral("additionalDeductions"),
                                QStringLiteral("credits"), QStringLiteral("federalWithholding"),
                                QStringLiteral("coloradoWithholding"), QStringLiteral("priorYearTax"),
                                QStringLiteral("priorYearAgi"), QStringLiteral("priorYearColoradoTax")};

QString stateLabel(const Installment &in)
{
    switch (in.state) {
    case Installment::State::Paid: return QObject::tr("Paid");
    case Installment::State::PaidLate: return QObject::tr("Paid late");
    case Installment::State::Overdue: return QObject::tr("Overdue");
    case Installment::State::NotRequired: return QObject::tr("Not required");
    case Installment::State::Upcoming: break;
    }
    return QObject::tr("Upcoming");
}

QString stateKey(const Installment &in)
{
    static const char *const keys[] = {"upcoming", "paid", "paidLate", "overdue", "notRequired"};
    return QString::fromLatin1(keys[int(in.state)]);
}

} // namespace

QVariantMap PayrollService::estimateProfile() const
{
    if (file() == nullptr) {
        return {};
    }
    QVariantMap p = QJsonDocument::fromJson(PayrollRecords(*file()).setup().estimateProfile.toUtf8()).object().toVariantMap();
    if (!p.contains(QStringLiteral("filingStatus"))) {
        p.insert(QStringLiteral("filingStatus"), QStringLiteral("single"));
    }
    if (!p.contains(QStringLiteral("profitShare"))) {
        p.insert(QStringLiteral("profitShare"), QStringLiteral("100%"));
    }
    if (!p.contains(QStringLiteral("priorYearFull"))) {
        p.insert(QStringLiteral("priorYearFull"), true);
    }
    return p;
}

QVariantMap PayrollService::saveEstimateProfile(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    const MoneyFormat money = m_books->moneyFormat();
    for (const QString &key : kProfileMoney) {
        QString error;
        if (!map.value(key).toString().trimmed().isEmpty() && !money.parse(map.value(key), &error)) {
            return Format::failure(error);
        }
    }
    PayrollRecords records(*c);
    PayrollSetup s = records.setup();
    s.estimateProfile = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson(QJsonDocument::Compact));
    return Format::result(records.saveSetup(s));
}

QVariantMap PayrollService::estimate() const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    const QDate today = m_books->todayDate();
    QString error;
    const std::optional<PayrollTables> tables = PayrollTables::forYear(today.year(), &error);
    if (!tables) {
        return Format::failure(error);
    }
    const PayrollSetup setup = PayrollRecords(*c).setup();
    // The planner is for an owner taxed on the business's profit: a sole
    // proprietor or a partner (owner_pay.h ownerPaymentAllowed).
    if (!ownerPaymentAllowed(setup.entity, OwnerPaymentKind::FederalEstimate)) {
        return Format::failure(
            setup.entity == EntityTax::Nonprofit
                ? tr("A nonprofit has no owners taxed on its profit, so it has no owner estimates to plan.")
            : setup.entity == EntityTax::CCorporation
                ? tr("The planner is for a sole proprietor or partner, taxed on the business's profit. A C corporation "
                     "pays its own income tax (Form 1120-W estimates, not planned here); its owners are paid wages "
                     "and dividends.")
                : tr("The planner is for a sole proprietor or partner, taxed on the business's profit. An "
                     "S corporation's owners are paid wages with withholding; a tax professional can say what their "
                     "K-1 income needs."));
    }
    const MoneyFormat money = m_books->moneyFormat();
    const QVariantMap p = estimateProfile();
    const auto amount = [&](const char *key) {
        return money.money(money.parse(p.value(QString::fromLatin1(key))).value_or(0));
    };
    const auto optionalAmount = [&](const char *key) -> std::optional<Money> {
        if (p.value(QString::fromLatin1(key)).toString().trimmed().isEmpty()) {
            return std::nullopt;
        }
        return amount(key);
    };
    // The year's profit so far, from the books, and the owner's share of it.
    const Basis basis = c->settings().basis;
    const qint64 net = Reports(*c).profitAndLoss({QDate(today.year(), 1, 1), today, basis, {}, {}})
                           .money(QStringLiteral("total:net_income"), QStringLiteral("total"));
    QString shareText = p.value(QStringLiteral("profitShare")).toString().trimmed();
    if (!shareText.endsWith(QLatin1Char('%'))) {
        shareText += QLatin1Char('%');
    }
    const Decimal share = Decimal::parse(shareText).value_or(Decimal(1, 0));
    EstimatedTaxInput in;
    in.colorado = setup.colorado;
    in.status = filingStatusFromKey(p.value(QStringLiteral("filingStatus")).toString()).value_or(FilingStatus::Single);
    in.asOf = today;
    in.yearToDateProfit = money.money(net).multiplied(share);
    in.projectedProfit = optionalAmount("projectedProfit");
    in.otherIncome = amount("otherIncome");
    in.wagesForSocialSecurity = amount("wagesForSocialSecurity");
    in.wagesForMedicare = amount("wagesForMedicare");
    in.businessAdjustments = amount("businessAdjustments");
    in.otherAdjustments = amount("otherAdjustments");
    in.itemizedDeductions = amount("itemizedDeductions");
    in.additionalDeductions = amount("additionalDeductions");
    in.credits = amount("credits");
    in.federalWithholding = amount("federalWithholding");
    in.coloradoWithholding = amount("coloradoWithholding");
    in.priorYearTax = optionalAmount("priorYearTax");
    in.priorYearAgi = optionalAmount("priorYearAgi");
    in.priorYearColoradoTax = optionalAmount("priorYearColoradoTax");
    in.priorYearCoveredFullYear = p.value(QStringLiteral("priorYearFull"), true).toBool();
    in.payments = OwnerPay(*c).estimatedPayments(today.year());
    const EstimatedTaxResult r = planEstimatedTax(*tables, in);
    if (!r.ok) {
        return Format::failure(r.error);
    }
    const auto lines = [](const Worksheet &sheet) {
        QVariantList out;
        for (const WorksheetLine &l : sheet) {
            out << QVariantMap{{QStringLiteral("line"), l.line}, {QStringLiteral("text"), l.text},
                               {QStringLiteral("value"), l.valueText()}};
        }
        return out;
    };
    const auto plan = [&](const EstimatePlan &e) {
        QVariantList installments;
        for (const Installment &i : e.installments) {
            installments << QVariantMap{{QStringLiteral("number"), i.number},
                                        {QStringLiteral("due"), Format::isoDate(i.due)},
                                        {QStringLiteral("amountText"), money.text(i.amount.minor())},
                                        {QStringLiteral("paidText"), money.text(i.paid.minor())},
                                        {QStringLiteral("owed"), i.owed.minor()},
                                        {QStringLiteral("owedText"), money.text(i.owed.minor())},
                                        {QStringLiteral("owedPlain"), money.plain(i.owed.minor())},
                                        {QStringLiteral("state"), stateKey(i)},
                                        {QStringLiteral("stateLabel"), stateLabel(i)}};
        }
        return QVariantMap{{QStringLiteral("totalTaxText"), money.text(e.totalTax.minor())},
                           {QStringLiteral("requiredText"), money.text(e.requiredAnnualPayment.minor())},
                           {QStringLiteral("neededText"), money.text(e.estimatedPaymentsNeeded.minor())},
                           {QStringLiteral("basis"), e.safeHarborBasis},
                           {QStringLiteral("required"), e.paymentsRequired},
                           {QStringLiteral("reason"), e.reason},
                           {QStringLiteral("paidText"), money.text(e.paid.minor())},
                           {QStringLiteral("remainingText"), money.text(e.remaining.minor())},
                           {QStringLiteral("worksheet"), lines(e.worksheet)},
                           {QStringLiteral("installments"), installments}};
    };
    QStringList notes = r.notes;
    notes.prepend(tr("Profit from the books, %1 basis, January 1 to %2%3.")
                      .arg(basis == Basis::Cash ? tr("cash") : tr("accrual"), Format::isoDate(today),
                           share == Decimal(1, 0) ? QString() : tr(" × the owner's %1 share").arg(share.toPercentString())));
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("year"), tables->year()},
            {QStringLiteral("asOf"), Format::isoDate(today)},
            {QStringLiteral("ytdProfitText"), money.text(in.yearToDateProfit.minor())},
            {QStringLiteral("annualProfitText"), money.text(r.annualProfit.minor())},
            {QStringLiteral("annualized"), r.annualized},
            {QStringLiteral("seTaxText"), money.text(r.seTax.minor())},
            {QStringLiteral("qbiText"), money.text(r.qbiDeduction.minor())},
            {QStringLiteral("taxableIncomeText"), money.text(r.taxableIncome.minor())},
            {QStringLiteral("incomeTaxText"), money.text(r.incomeTax.minor())},
            {QStringLiteral("coloradoTaxText"), money.text(r.coloradoTax.minor())},
            {QStringLiteral("seWorksheet"), lines(r.seWorksheet)},
            {QStringLiteral("federal"), plan(r.federal)},
            // Colorado's plan only for a business that chose Colorado.
            {QStringLiteral("colorado"), setup.colorado ? QVariant(plan(r.colorado)) : QVariant()},
            {QStringLiteral("setAsidePercent"), r.setAsidePercent},
            {QStringLiteral("businessTaxText"), money.text(r.businessTax.minor())},
            {QStringLiteral("notes"), notes}};
}

QVariantMap PayrollService::setAsideFor(const QString &amountText) const
{
    const MoneyFormat money = m_books->moneyFormat();
    const qint64 draw = money.parse(amountText).value_or(0);
    const QVariantMap e = estimate();
    const int percent = e.value(QStringLiteral("ok")).toBool() ? e.value(QStringLiteral("setAsidePercent")).toInt() : 0;
    const Money amount = Payroll::setAsideFor(money.money(draw), percent);
    return {{QStringLiteral("percent"), percent}, {QStringLiteral("amount"), amount.minor()},
            {QStringLiteral("amountText"), money.text(amount.minor())}, {QStringLiteral("amountPlain"), money.plain(amount.minor())}};
}

QVariantMap PayrollService::payOwner(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    const MoneyFormat money = m_books->moneyFormat();
    QString error;
    const std::optional<qint64> amount = money.parse(map.value(QStringLiteral("amount")), &error);
    if (!amount) {
        return Format::failure(error);
    }
    OwnerPaymentRequest r;
    const QString kind = map.value(QStringLiteral("kind")).toString();
    r.kind = kind == QLatin1String("distribution") ? OwnerPaymentKind::Distribution
           : kind == QLatin1String("dividend")     ? OwnerPaymentKind::Dividend
                                                   : OwnerPaymentKind::Draw;
    r.ownerId = Format::id(map.value(QStringLiteral("ownerId")));
    r.bankAccountId = Format::id(map.value(QStringLiteral("bankAccountId")));
    r.amount = money.money(*amount);
    r.date = Format::date(map.value(QStringLiteral("date")));
    r.method = map.value(QStringLiteral("method")).toString() == QLatin1String("check") ? OwnerPaymentRequest::Method::Check
                                                                                        : OwnerPaymentRequest::Method::Transfer;
    r.checkNumber = map.value(QStringLiteral("checkNumber")).toString();
    r.toPrint = map.value(QStringLiteral("toPrint")).toBool();
    r.memo = map.value(QStringLiteral("memo")).toString().trimmed();
    QVariantMap out = Format::result(OwnerPay(*c).record(r));
    if (out.value(QStringLiteral("ok")).toBool() && r.kind == OwnerPaymentKind::Draw) {
        const QVariantMap aside = setAsideFor(money.plain(r.amount.minor()));
        out.insert(QStringLiteral("setAsidePercent"), aside.value(QStringLiteral("percent")));
        out.insert(QStringLiteral("setAside"), aside.value(QStringLiteral("amount")));
        out.insert(QStringLiteral("setAsideText"), aside.value(QStringLiteral("amountText")));
        out.insert(QStringLiteral("setAsidePlain"), aside.value(QStringLiteral("amountPlain")));
    }
    return out;
}

QVariantMap PayrollService::recordSetAside(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    const MoneyFormat money = m_books->moneyFormat();
    QString error;
    const std::optional<qint64> amount = money.parse(map.value(QStringLiteral("amount")), &error);
    if (!amount) {
        return Format::failure(error);
    }
    OwnerPaymentRequest r;
    r.kind = OwnerPaymentKind::SetAside;
    r.amount = money.money(*amount);
    r.date = Format::date(map.value(QStringLiteral("date")));
    r.bankAccountId = Format::id(map.value(QStringLiteral("bankAccountId")));
    r.toAccountId = Format::id(map.value(QStringLiteral("toAccountId")));
    return Format::result(OwnerPay(*c).record(r));
}

QVariantMap PayrollService::recordEstimatedPayment(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    const MoneyFormat money = m_books->moneyFormat();
    QString error;
    const std::optional<qint64> amount = money.parse(map.value(QStringLiteral("amount")), &error);
    if (!amount) {
        return Format::failure(error);
    }
    OwnerPaymentRequest r;
    r.kind = map.value(QStringLiteral("jurisdiction")).toString() == QLatin1String("colorado") ? OwnerPaymentKind::ColoradoEstimate
                                                                                              : OwnerPaymentKind::FederalEstimate;
    r.amount = money.money(*amount);
    r.date = Format::date(map.value(QStringLiteral("date")));
    r.taxYear = map.value(QStringLiteral("taxYear")).toInt();
    r.installment = map.value(QStringLiteral("installment")).toInt();
    r.bankAccountId = Format::id(map.value(QStringLiteral("bankAccountId")));
    r.ownerId = Format::id(map.value(QStringLiteral("ownerId")));
    r.reference = map.value(QStringLiteral("reference")).toString();
    return Format::result(OwnerPay(*c).record(r));
}

QVariantList PayrollService::ownerPayments(int year) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = m_books->moneyFormat();
    static const QStringList labels{tr("Owner's draw"), tr("Federal estimated tax"), tr("Colorado estimated tax"),
                                    tr("Tax set-aside"), tr("Shareholder distribution"), tr("Dividend")};
    for (const OwnerPaymentRecord &r : OwnerPay(*c).payments(QDate(year, 1, 1), QDate(year + 1, 1, 31), {}, true)) {
        out << QVariantMap{{QStringLiteral("id"), r.docId},
                           {QStringLiteral("kind"), ownerPaymentKindKey(r.kind)},
                           {QStringLiteral("kindLabel"), labels.value(int(r.kind))},
                           {QStringLiteral("date"), Format::isoDate(r.date)},
                           {QStringLiteral("ownerName"), r.ownerName},
                           {QStringLiteral("amountText"), money.text(r.amount.minor())},
                           {QStringLiteral("number"), r.number},
                           {QStringLiteral("memo"), r.memo},
                           {QStringLiteral("voided"), r.voided}};
    }
    return out;
}

QVariantMap PayrollService::voidOwnerPayment(qint64 id, const QString &reason)
{
    return file() != nullptr ? Format::result(OwnerPay(*file()).voidPayment(id, reason))
                             : Format::failure(tr("No company is open in this window."));
}

QVariantList PayrollService::upcoming(int days) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const QDate today = m_books->todayDate();
    const QDate horizon = today.addDays(days);
    const MoneyFormat money = m_books->moneyFormat();
    QList<std::pair<QDate, QVariantMap>> items;
    const PayrollSetup setup = PayrollRecords(*c).setup();
    if (setup.configured && ownerPaymentAllowed(setup.entity, OwnerPaymentKind::FederalEstimate)) {
        const QVariantMap e = estimate();
        for (const char *key : {"federal", "colorado"}) {
            const QVariantMap plan = e.value(QString::fromLatin1(key)).toMap();
            for (const QVariant &v : plan.value(QStringLiteral("installments")).toList()) {
                const QVariantMap i = v.toMap();
                const QDate due = Format::date(i.value(QStringLiteral("due")));
                if (i.value(QStringLiteral("owed")).toLongLong() > 0 && due <= horizon) {
                    items << std::pair{due, QVariantMap{{QStringLiteral("label"), tr("%1 estimated tax, payment %2")
                                                                                     .arg(QLatin1String(key) == QLatin1String("federal") ? tr("Federal") : tr("Colorado"))
                                                                                     .arg(i.value(QStringLiteral("number")).toInt())},
                                                        {QStringLiteral("date"), Format::isoDate(due)},
                                                        {QStringLiteral("amountText"), i.value(QStringLiteral("owedText"))},
                                                        {QStringLiteral("kind"), QStringLiteral("estimate")}}};
                }
            }
        }
    }
    for (const LiabilityDue &d : PayrollLiabilities(*c).liabilities(QDate(today.year() - 1, 1, 1), today, true)) {
        if (d.due.isValid() && d.due <= horizon) {
            items << std::pair{d.due, QVariantMap{{QStringLiteral("label"), d.label + QStringLiteral(", ") + d.period},
                                                  {QStringLiteral("date"), Format::isoDate(d.due)},
                                                  {QStringLiteral("amountText"), money.text(d.balance.minor())},
                                                  {QStringLiteral("kind"), QStringLiteral("liability")}}};
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &item : std::as_const(items)) {
        out << item.second;
    }
    return out;
}

void PayrollService::remindDueDates()
{
    const QVariantList due = upcoming(14);
    QStringList fresh;
    for (const QVariant &v : due) {
        const QVariantMap item = v.toMap();
        const QString key = item.value(QStringLiteral("label")).toString() + item.value(QStringLiteral("date")).toString();
        if (!m_reminded.contains(key)) {
            m_reminded.insert(key);
            fresh << tr("%1 — %2 by %3").arg(item.value(QStringLiteral("label")).toString(),
                                              item.value(QStringLiteral("amountText")).toString(),
                                              item.value(QStringLiteral("date")).toString());
        }
    }
    if (!fresh.isEmpty()) {
        emit m_books->notify(tr("Tax dates in the next two weeks"), fresh.join(QLatin1Char('\n')), QStringLiteral("info"));
    }
}

} // namespace QindaOffice::Apps::Books
