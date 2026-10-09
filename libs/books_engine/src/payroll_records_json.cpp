// SPDX-License-Identifier: GPL-3.0-or-later
// The payroll records' JSON columns and the employee audit view
// (payroll_records_p.h).
#include "payroll_records_p.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>

namespace QindaOffice::Books::PayrollRecordsJson {

using namespace Payroll;

namespace {

QString moneyText(const Money &m)
{
    return m.toPlainString();
}

Money moneyFrom(const QJsonValue &v, const QString &currency)
{
    return Money::parse(v.toString(), currency).value_or(Money(0, currency));
}

const QStringList kRetirementKeys{QStringLiteral("none"), QStringLiteral("401k"), QStringLiteral("simple")};

} // namespace

QString compact(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

Decimal decimalFrom(const QVariant &v)
{
    return Decimal::parse(v.toString()).value_or(Decimal());
}

QJsonObject w4Json(const W4Certificate &w)
{
    return {{QStringLiteral("form"), w.form == W4Certificate::Form::Before2020 ? 2019 : 2020},
            {QStringLiteral("status"), w4StatusKey(w.status)},
            {QStringLiteral("step2"), w.step2Checked},
            {QStringLiteral("step3"), moneyText(w.step3Credits)},
            {QStringLiteral("step4a"), moneyText(w.step4aOtherIncome)},
            {QStringLiteral("step4b"), moneyText(w.step4bDeductions)},
            {QStringLiteral("step4c"), moneyText(w.step4cExtra)},
            {QStringLiteral("legacyStatus"), w4LegacyStatusKey(w.legacyStatus)},
            {QStringLiteral("allowances"), w.allowances},
            {QStringLiteral("legacyExtra"), moneyText(w.legacyExtra)},
            {QStringLiteral("exempt"), w.exempt}};
}

W4Certificate w4From(const QJsonObject &o, const QString &cur)
{
    W4Certificate w;
    w.form = o.value(QStringLiteral("form")).toInt(2020) == 2019 ? W4Certificate::Form::Before2020
                                                                  : W4Certificate::Form::From2020;
    w.status = w4StatusFromKey(o.value(QStringLiteral("status")).toString()).value_or(W4Status::SingleOrMarriedSeparately);
    w.step2Checked = o.value(QStringLiteral("step2")).toBool();
    w.step3Credits = moneyFrom(o.value(QStringLiteral("step3")), cur);
    w.step4aOtherIncome = moneyFrom(o.value(QStringLiteral("step4a")), cur);
    w.step4bDeductions = moneyFrom(o.value(QStringLiteral("step4b")), cur);
    w.step4cExtra = moneyFrom(o.value(QStringLiteral("step4c")), cur);
    w.legacyStatus = w4LegacyStatusFromKey(o.value(QStringLiteral("legacyStatus")).toString()).value_or(W4LegacyStatus::Single);
    w.allowances = o.value(QStringLiteral("allowances")).toInt();
    w.legacyExtra = moneyFrom(o.value(QStringLiteral("legacyExtra")), cur);
    w.exempt = o.value(QStringLiteral("exempt")).toBool();
    return w;
}

QJsonObject dr0004Json(const ColoradoCertificate &c)
{
    return {{QStringLiteral("filed"), c.filed},
            {QStringLiteral("allowance"), c.annualAllowance ? QJsonValue(moneyText(*c.annualAllowance)) : QJsonValue()},
            {QStringLiteral("extra"), moneyText(c.additionalPerPeriod)}};
}

ColoradoCertificate dr0004From(const QJsonObject &o, const QString &cur)
{
    ColoradoCertificate c;
    c.filed = o.value(QStringLiteral("filed")).toBool();
    if (o.value(QStringLiteral("allowance")).isString()) {
        c.annualAllowance = moneyFrom(o.value(QStringLiteral("allowance")), cur);
    }
    c.additionalPerPeriod = moneyFrom(o.value(QStringLiteral("extra")), cur);
    return c;
}

QJsonObject deductionsJson(const PayDeductions &d)
{
    return {{QStringLiteral("section125"), moneyText(d.section125)},
            {QStringLiteral("retirement"), kRetirementKeys.value(int(d.retirement))},
            {QStringLiteral("retirementAmount"), moneyText(d.retirementAmount)},
            {QStringLiteral("retirementPercent"), d.retirementPercent.toString()},
            {QStringLiteral("shareholderHealth"), moneyText(d.shareholderHealth)},
            {QStringLiteral("afterTax"), moneyText(d.afterTax)},
            {QStringLiteral("afterTaxLabel"), d.afterTaxLabel}};
}

PayDeductions deductionsFrom(const QJsonObject &o, const QString &cur)
{
    PayDeductions d;
    d.section125 = moneyFrom(o.value(QStringLiteral("section125")), cur);
    d.retirement = PayDeductions::Retirement(qMax(0, int(kRetirementKeys.indexOf(o.value(QStringLiteral("retirement")).toString()))));
    d.retirementAmount = moneyFrom(o.value(QStringLiteral("retirementAmount")), cur);
    d.retirementPercent = Decimal::parse(o.value(QStringLiteral("retirementPercent")).toString()).value_or(Decimal());
    d.shareholderHealth = moneyFrom(o.value(QStringLiteral("shareholderHealth")), cur);
    d.afterTax = moneyFrom(o.value(QStringLiteral("afterTax")), cur);
    d.afterTaxLabel = o.value(QStringLiteral("afterTaxLabel")).toString();
    return d;
}

QJsonObject objectFrom(const QVariant &text)
{
    return QJsonDocument::fromJson(text.toString().toUtf8()).object();
}

// The audit view of an employee's payroll: everything but the SSN's digits.
QString employeeAuditJson(const EmployeePayroll &e)
{
    QJsonObject o{{QStringLiteral("name"), e.fullName()},
                  {QStringLiteral("ssn"), e.ssn.isEmpty() ? QStringLiteral("none") : maskedSsn(e.ssn)},
                  {QStringLiteral("payType"), e.payType == EmployeePayroll::PayType::Hourly ? QStringLiteral("hourly")
                                                                                             : QStringLiteral("salary")},
                  {QStringLiteral("payRate"), e.payRate.toString()},
                  {QStringLiteral("frequency"), payFrequencyKey(e.frequency)},
                  {QStringLiteral("w4"), w4Json(e.w4)},
                  {QStringLiteral("dr0004"), dr0004Json(e.dr0004)},
                  {QStringLiteral("deductions"), deductionsJson(e.deductions)},
                  {QStringLiteral("custom"), customJson(e)}};
    return compact(o);
}

QJsonObject customJson(const EmployeePayroll &e)
{
    QJsonArray exempt;
    QList<qint64> ids(e.exemptItems.cbegin(), e.exemptItems.cend());
    std::sort(ids.begin(), ids.end());
    for (const qint64 id : std::as_const(ids)) {
        exempt.append(QJsonValue(id));
    }
    QJsonObject withholding;
    for (auto it = e.withholding.cbegin(); it != e.withholding.cend(); ++it) {
        withholding.insert(QString::number(it.key()),
                           QJsonObject{{QStringLiteral("percent"), it.value().percent.toString()},
                                       {QStringLiteral("extra"), moneyText(it.value().extra)}});
    }
    return {{QStringLiteral("exempt"), exempt}, {QStringLiteral("withholding"), withholding}};
}

void customFrom(const QJsonObject &o, const QString &cur, EmployeePayroll *e)
{
    for (const QJsonValue &v : o.value(QStringLiteral("exempt")).toArray()) {
        e->exemptItems.insert(v.toInteger());
    }
    const QJsonObject withholding = o.value(QStringLiteral("withholding")).toObject();
    for (auto it = withholding.constBegin(); it != withholding.constEnd(); ++it) {
        const QJsonObject w = it.value().toObject();
        e->withholding.insert(it.key().toLongLong(),
                              EmployeePayroll::CustomWithholding{
                                  Decimal::parse(w.value(QStringLiteral("percent")).toString()).value_or(Decimal()),
                                  moneyFrom(w.value(QStringLiteral("extra")), cur)});
    }
}

} // namespace QindaOffice::Books::PayrollRecordsJson
