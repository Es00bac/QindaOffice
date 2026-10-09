// SPDX-License-Identifier: GPL-3.0-or-later
// Payroll setup and employee payroll records (payroll_records.h).
#include "payroll_records.h"

#include "accounts.h"
#include "company_file.h"
#include "employees.h"
#include "sql_util.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

namespace QindaOffice::Books {

using namespace Payroll;

namespace {

QString compact(const QJsonObject &o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString moneyText(const Money &m)
{
    return m.toPlainString();
}

Money moneyFrom(const QJsonValue &v, const QString &currency)
{
    return Money::parse(v.toString(), currency).value_or(Money(0, currency));
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

const QStringList kRetirementKeys{QStringLiteral("none"), QStringLiteral("401k"), QStringLiteral("simple")};

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
                  {QStringLiteral("deductions"), deductionsJson(e.deductions)}};
    return compact(o);
}

const QString kSettingsPrefix = QStringLiteral("payroll_");

} // namespace

QString entityTaxKey(EntityTax tax)
{
    switch (tax) {
    case EntityTax::Unsure: return QStringLiteral("unsure");
    case EntityTax::ScheduleC: return QStringLiteral("scheduleC");
    case EntityTax::SCorporation: return QStringLiteral("sCorporation");
    case EntityTax::Partnership: return QStringLiteral("partnership");
    }
    return {};
}

std::optional<EntityTax> entityTaxFromKey(const QString &key)
{
    for (const EntityTax t : {EntityTax::Unsure, EntityTax::ScheduleC, EntityTax::SCorporation, EntityTax::Partnership}) {
        if (entityTaxKey(t) == key) {
            return t;
        }
    }
    return std::nullopt;
}

QList<PayrollAccountSpec> payrollAccountRoles()
{
    const int expense = int(AccountType::Expense);
    const int liability = int(AccountType::OtherCurrentLiability);
    const int equity = int(AccountType::Equity);
    const QString expenses = QStringLiteral("Payroll Expenses");
    const QString liabilities = QStringLiteral("Payroll Liabilities");
    return {{PayrollRole::Wages, QStringLiteral("Wages"), expense, expenses},
            {PayrollRole::OfficerWages, QStringLiteral("Officer Wages"), expense, expenses},
            {PayrollRole::EmployerTaxes, QStringLiteral("Payroll Taxes"), expense, expenses},
            {PayrollRole::FederalWithholding, QStringLiteral("Federal Withholding"), liability, liabilities},
            {PayrollRole::SocialSecurity, QStringLiteral("Social Security"), liability, liabilities},
            {PayrollRole::Medicare, QStringLiteral("Medicare"), liability, liabilities},
            {PayrollRole::Futa, QStringLiteral("Federal Unemployment (FUTA)"), liability, liabilities},
            {PayrollRole::ColoradoWithholding, QStringLiteral("Colorado Withholding"), liability, liabilities},
            {PayrollRole::ColoradoUi, QStringLiteral("Colorado Unemployment (UI)"), liability, liabilities},
            {PayrollRole::Famli, QStringLiteral("Colorado FAMLI"), liability, liabilities},
            {PayrollRole::LocalOpt, QStringLiteral("Local Occupational Privilege Tax"), liability, liabilities},
            {PayrollRole::Section125, QStringLiteral("Health Insurance Deductions"), liability, liabilities},
            {PayrollRole::Retirement, QStringLiteral("Retirement Plan Deductions"), liability, liabilities},
            {PayrollRole::AfterTax, QStringLiteral("Other Payroll Deductions"), liability, liabilities},
            {PayrollRole::OwnerDraws, QStringLiteral("Owner Draws"), equity, QString()},
            {PayrollRole::Distributions, QStringLiteral("Shareholder Distributions"), equity, QString()}};
}

QString EmployeePayroll::fullName() const
{
    QStringList parts{firstName.trimmed()};
    if (!middleInitial.trimmed().isEmpty()) {
        parts << middleInitial.trimmed();
    }
    parts << lastName.trimmed();
    if (!suffix.trimmed().isEmpty()) {
        parts << suffix.trimmed();
    }
    parts.removeAll(QString());
    return parts.join(QLatin1Char(' '));
}

QString normalizedTaxId(const QString &text, int digits)
{
    QString out;
    for (const QChar c : text) {
        if (c.isDigit()) {
            out += c;
        } else if (c != QLatin1Char('-') && !c.isSpace()) {
            return {};
        }
    }
    return out.size() == digits ? out : QString();
}

QString maskedSsn(const QString &ssn)
{
    const QString digits = normalizedTaxId(ssn, 9);
    return digits.isEmpty() ? QString() : QStringLiteral("•••-••-") + digits.right(4);
}

QString maskedEin(const QString &ein)
{
    const QString digits = normalizedTaxId(ein, 9);
    return digits.isEmpty() ? QString() : QStringLiteral("••-•••") + digits.right(4);
}

PayrollRecords::PayrollRecords(CompanyFile &file)
    : m_file(file)
{
}

PayrollSetup PayrollRecords::setup() const
{
    QMap<QString, QString> v;
    QSqlQuery q(m_file.database());
    if (Sql::run(q, QStringLiteral("SELECT key, value FROM settings WHERE key LIKE 'payroll\\_%' ESCAPE '\\'"))) {
        while (q.next()) {
            v.insert(q.value(0).toString().mid(kSettingsPrefix.size()), q.value(1).toString());
        }
    }
    PayrollSetup s;
    s.entity = EntityTax(v.value(QStringLiteral("entity")).toInt());
    s.configured = v.value(QStringLiteral("configured")) == QLatin1String("1");
    s.ein = v.value(QStringLiteral("ein"));
    s.coloradoAccount = v.value(QStringLiteral("co_account"));
    s.uiAccount = v.value(QStringLiteral("ui_account"));
    s.famliAccount = v.value(QStringLiteral("famli_account"));
    s.depositSchedule = depositScheduleFromKey(v.value(QStringLiteral("deposit_schedule"))).value_or(DepositSchedule::Monthly);
    s.coloradoFrequency = coloradoFrequencyFromKey(v.value(QStringLiteral("co_frequency"))).value_or(ColoradoFrequency::Quarterly);
    for (const QJsonValue &part : QJsonDocument::fromJson(v.value(QStringLiteral("ui_rate")).toUtf8()).array()) {
        const QJsonObject o = part.toObject();
        s.uiRate << UiRatePart{o.value(QStringLiteral("name")).toString(),
                               Decimal::parse(o.value(QStringLiteral("rate")).toString()).value_or(Decimal())};
    }
    s.uiHeavyConstruction = v.value(QStringLiteral("ui_heavy")) == QLatin1String("1");
    s.smallEmployer = v.value(QStringLiteral("small_employer"), QStringLiteral("1")) == QLatin1String("1");
    s.famliEmployeeShare = decimalFrom(v.value(QStringLiteral("famli_ee_share"), QStringLiteral("0.5")));
    s.famliEmployerShare = decimalFrom(v.value(QStringLiteral("famli_er_share"), s.smallEmployer ? QStringLiteral("0")
                                                                                                  : QStringLiteral("0.5")));
    s.optCity = v.value(QStringLiteral("opt_city"));
    s.optFiling = v.value(QStringLiteral("opt_filing"), QStringLiteral("quarterly"));
    s.roundFederalWithholding = v.value(QStringLiteral("round_federal")) == QLatin1String("1");
    s.payrollBankAccountId = v.value(QStringLiteral("bank_account")).toLongLong();
    s.setAsideAccountId = v.value(QStringLiteral("setaside_account")).toLongLong();
    const QJsonObject accounts = QJsonDocument::fromJson(v.value(QStringLiteral("accounts")).toUtf8()).object();
    for (auto it = accounts.constBegin(); it != accounts.constEnd(); ++it) {
        s.accounts.insert(it.key(), it.value().toVariant().toLongLong());
    }
    s.estimateProfile = v.value(QStringLiteral("estimate_profile"));
    s.bsoUserId = v.value(QStringLiteral("bso_user_id"));
    s.w2ContactName = v.value(QStringLiteral("w2_contact_name"));
    s.w2ContactPhone = v.value(QStringLiteral("w2_contact_phone"));
    s.w2ContactEmail = v.value(QStringLiteral("w2_contact_email"));
    s.signaturePin = v.value(QStringLiteral("signature_pin"));
    return s;
}

Result PayrollRecords::saveSetup(const PayrollSetup &setup)
{
    if (!setup.ein.isEmpty() && normalizedTaxId(setup.ein, 9).isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("an EIN has nine digits (00-0000000)"));
    }
    if (setup.famliEmployeeShare.isNegative() || setup.famliEmployerShare.isNegative()
        || Decimal::compare(setup.famliEmployeeShare, Decimal(5, 1)) > 0) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("an employee pays at most half of the FAMLI premium"));
    }
    QJsonArray ui;
    for (const UiRatePart &part : setup.uiRate) {
        if (part.rate.isNegative()) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("a UI rate is not negative"));
        }
        ui.append(QJsonObject{{QStringLiteral("name"), part.name}, {QStringLiteral("rate"), part.rate.toString()}});
    }
    QJsonObject accounts;
    for (auto it = setup.accounts.constBegin(); it != setup.accounts.constEnd(); ++it) {
        accounts.insert(it.key(), QJsonValue(qint64(it.value())));
    }
    const QList<std::pair<QString, QString>> rows{
        {QStringLiteral("entity"), QString::number(int(setup.entity))},
        {QStringLiteral("configured"), QStringLiteral("1")},
        {QStringLiteral("ein"), normalizedTaxId(setup.ein, 9)},
        {QStringLiteral("co_account"), setup.coloradoAccount.trimmed()},
        {QStringLiteral("ui_account"), setup.uiAccount.trimmed()},
        {QStringLiteral("famli_account"), setup.famliAccount.trimmed()},
        {QStringLiteral("deposit_schedule"), depositScheduleKey(setup.depositSchedule)},
        {QStringLiteral("co_frequency"), coloradoFrequencyKey(setup.coloradoFrequency)},
        {QStringLiteral("ui_rate"), setup.uiRate.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(ui).toJson(QJsonDocument::Compact))},
        {QStringLiteral("ui_heavy"), setup.uiHeavyConstruction ? QStringLiteral("1") : QStringLiteral("0")},
        {QStringLiteral("small_employer"), setup.smallEmployer ? QStringLiteral("1") : QStringLiteral("0")},
        {QStringLiteral("famli_ee_share"), setup.famliEmployeeShare.toString()},
        {QStringLiteral("famli_er_share"), setup.famliEmployerShare.toString()},
        {QStringLiteral("opt_city"), setup.optCity},
        {QStringLiteral("opt_filing"), setup.optFiling},
        {QStringLiteral("round_federal"), setup.roundFederalWithholding ? QStringLiteral("1") : QStringLiteral("0")},
        {QStringLiteral("bank_account"), QString::number(setup.payrollBankAccountId)},
        {QStringLiteral("setaside_account"), QString::number(setup.setAsideAccountId)},
        {QStringLiteral("accounts"), QString::fromUtf8(QJsonDocument(accounts).toJson(QJsonDocument::Compact))},
        {QStringLiteral("estimate_profile"), setup.estimateProfile},
        {QStringLiteral("bso_user_id"), setup.bsoUserId.trimmed()},
        {QStringLiteral("w2_contact_name"), setup.w2ContactName.trimmed()},
        {QStringLiteral("w2_contact_phone"), setup.w2ContactPhone.trimmed()},
        {QStringLiteral("w2_contact_email"), setup.w2ContactEmail.trimmed()},
        {QStringLiteral("signature_pin"), setup.signaturePin.trimmed()},
    };
    const PayrollSetup before = this->setup();
    SqlTransaction txn(m_file);
    for (const auto &[key, value] : rows) {
        QString error;
        if (!Sql::exec(m_file.database(), QStringLiteral("INSERT OR REPLACE INTO settings(key, value) VALUES(?, ?)"),
                       {kSettingsPrefix + key, value}, &error)) {
            return Result::failure(ErrorCode::Storage, error);
        }
    }
    const auto auditJson = [](const PayrollSetup &s) {
        return compact({{QStringLiteral("entity"), entityTaxKey(s.entity)},
                        {QStringLiteral("ein"), s.ein.isEmpty() ? QStringLiteral("none") : maskedEin(s.ein)},
                        {QStringLiteral("depositSchedule"), depositScheduleKey(s.depositSchedule)},
                        {QStringLiteral("coloradoFrequency"), coloradoFrequencyKey(s.coloradoFrequency)},
                        {QStringLiteral("smallEmployer"), s.smallEmployer},
                        {QStringLiteral("optCity"), s.optCity}});
    };
    m_file.audit(QStringLiteral("edit"), QStringLiteral("payroll-setup"), 0, auditJson(before), auditJson(setup));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success();
}

Result PayrollRecords::ensurePayrollAccounts()
{
    PayrollSetup s = setup();
    ChartOfAccounts chart(m_file);
    SqlTransaction txn(m_file);
    QMap<QString, qint64> parents;
    const auto findOrCreate = [&](const QString &name, AccountType type, qint64 parentId) -> qint64 {
        for (const Account &a : chart.accountsOfType(type, true)) {
            if (a.name.compare(name, Qt::CaseInsensitive) == 0 && (parentId == 0 || a.parentId == parentId || a.parentId == 0)) {
                return a.id;
            }
        }
        Account a;
        a.name = name;
        a.type = type;
        a.parentId = parentId;
        const Result r = chart.create(a);
        return r.ok ? r.id : 0;
    };
    for (const PayrollAccountSpec &spec : payrollAccountRoles()) {
        const std::optional<Account> current = chart.account(s.account(spec.role));
        if (current && current->active) {
            continue;
        }
        qint64 parentId = 0;
        if (!spec.parent.isEmpty()) {
            if (!parents.contains(spec.parent)) {
                parents.insert(spec.parent, findOrCreate(spec.parent, AccountType(spec.type), 0));
            }
            parentId = parents.value(spec.parent);
            if (parentId == 0) {
                return Result::failure(ErrorCode::Storage, QStringLiteral("could not create %1").arg(spec.parent));
            }
        }
        const qint64 id = findOrCreate(spec.name, AccountType(spec.type), parentId);
        if (id == 0) {
            return Result::failure(ErrorCode::Storage, QStringLiteral("could not create the %1 account").arg(spec.name));
        }
        s.accounts.insert(spec.role, id);
    }
    if (const Result saved = saveSetup(s); !saved.ok) {
        return saved;
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success();
}

EmployeePayroll PayrollRecords::employee(qint64 employeeId) const
{
    EmployeePayroll e;
    e.employeeId = employeeId;
    const QString cur = m_file.currency();
    QSqlQuery q(m_file.database());
    if (!Sql::run(q, QStringLiteral("SELECT first_name, middle_initial, last_name, suffix, ssn, street, city, state, zip,"
                                    " hire_date, release_date, pay_type, pay_rate, pay_frequency, default_hours, w4,"
                                    " w4_on_file, dr0004, co_withholding, opt_city, owner_employee, shareholder_2pct,"
                                    " deductions FROM employee_payroll WHERE employee_id = ?"),
                  {employeeId})
        || !q.next()) {
        if (const std::optional<Employee> plain = Employees(m_file).employee(employeeId)) {
            const QStringList words = plain->name.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            e.firstName = words.value(0);
            e.lastName = words.mid(1).join(QLatin1Char(' '));
        }
        return e;
    }
    e.exists = true;
    e.firstName = q.value(0).toString();
    e.middleInitial = q.value(1).toString();
    e.lastName = q.value(2).toString();
    e.suffix = q.value(3).toString();
    e.ssn = q.value(4).toString();
    e.street = q.value(5).toString();
    e.city = q.value(6).toString();
    e.state = q.value(7).toString();
    e.zip = q.value(8).toString();
    e.hireDate = Sql::fromIso(q.value(9));
    e.releaseDate = Sql::fromIso(q.value(10));
    e.payType = EmployeePayroll::PayType(q.value(11).toInt());
    e.payRate = decimalFrom(q.value(12));
    e.frequency = payFrequencyFromKey(q.value(13).toString()).value_or(PayFrequency::Biweekly);
    e.defaultHours = decimalFrom(q.value(14));
    e.w4 = w4From(objectFrom(q.value(15)), cur);
    e.w4OnFile = q.value(16).toBool();
    e.dr0004 = dr0004From(objectFrom(q.value(17)), cur);
    e.coloradoWithholding = q.value(18).toBool();
    e.optCity = q.value(19).toString();
    e.ownerEmployee = q.value(20).toBool();
    e.shareholder2Percent = q.value(21).toBool();
    e.deductions = deductionsFrom(objectFrom(q.value(22)), cur);
    return e;
}

QList<EmployeePayroll> PayrollRecords::employees(bool includeReleased) const
{
    QList<EmployeePayroll> out;
    for (const Employee &plain : Employees(m_file).employees(includeReleased)) {
        EmployeePayroll e = employee(plain.id);
        if (e.exists && (includeReleased || !e.releaseDate.isValid())) {
            out << e;
        }
    }
    return out;
}

Result PayrollRecords::saveEmployee(const EmployeePayroll &record)
{
    if (!Employees(m_file).employee(record.employeeId)) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("add the employee to the Employees list first"));
    }
    if (record.firstName.trimmed().isEmpty() || record.lastName.trimmed().isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a W-2 needs the employee's first and last name"));
    }
    if (!record.ssn.isEmpty() && normalizedTaxId(record.ssn, 9).isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a Social Security number has nine digits"));
    }
    if (record.payRate.isNegative() || record.defaultHours.isNegative()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a pay rate or hours are not negative"));
    }
    const W4Certificate &w = record.w4;
    for (const Money &m : {w.step3Credits, w.step4aOtherIncome, w.step4bDeductions, w.step4cExtra, w.legacyExtra,
                           record.dr0004.additionalPerPeriod, record.deductions.section125,
                           record.deductions.retirementAmount, record.deductions.shareholderHealth,
                           record.deductions.afterTax}) {
        if (m.isNegative()) {
            return Result::failure(ErrorCode::Invalid, QStringLiteral("certificate and deduction amounts are not negative"));
        }
    }
    if (w.allowances < 0 || (record.dr0004.annualAllowance && record.dr0004.annualAllowance->isNegative())) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("allowances are not negative"));
    }
    const EmployeePayroll before = employee(record.employeeId);
    SqlTransaction txn(m_file);
    QString error;
    const bool ok = Sql::exec(
        m_file.database(),
        QStringLiteral("INSERT OR REPLACE INTO employee_payroll(employee_id, first_name, middle_initial, last_name, suffix,"
                       " ssn, street, city, state, zip, hire_date, release_date, pay_type, pay_rate, pay_frequency,"
                       " default_hours, w4, w4_on_file, dr0004, co_withholding, opt_city, owner_employee,"
                       " shareholder_2pct, deductions, updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"),
        {record.employeeId, record.firstName.trimmed(), record.middleInitial.trimmed(), record.lastName.trimmed(),
         record.suffix.trimmed(), normalizedTaxId(record.ssn, 9), record.street.trimmed(), record.city.trimmed(),
         record.state.trimmed().toUpper(), record.zip.trimmed(), Sql::isoDate(record.hireDate),
         Sql::isoDate(record.releaseDate), int(record.payType), record.payRate.toString(),
         payFrequencyKey(record.frequency), record.defaultHours.toString(), compact(w4Json(record.w4)),
         record.w4OnFile ? 1 : 0, compact(dr0004Json(record.dr0004)), record.coloradoWithholding ? 1 : 0,
         record.optCity, record.ownerEmployee ? 1 : 0, record.shareholder2Percent ? 1 : 0,
         compact(deductionsJson(record.deductions)), CompanyFile::nowStamp()},
        &error);
    if (!ok) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(before.exists ? QStringLiteral("edit") : QStringLiteral("create"), QStringLiteral("employee-payroll"),
                 record.employeeId, before.exists ? employeeAuditJson(before) : QString(), employeeAuditJson(record));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(record.employeeId);
}

Money PayrollRecords::grossPay(const EmployeePayroll &record, const std::optional<Decimal> &hours) const
{
    const Money dollar = m_file.money(100);
    if (record.payType == EmployeePayroll::PayType::Hourly) {
        return dollar.multiplied(rateTimes(record.payRate, hours.value_or(record.defaultHours)), RoundingMode::HalfUp);
    }
    return dollar.multiplied(record.payRate, RoundingMode::HalfUp).scaled(1, periodsPerYear(record.frequency),
                                                                        RoundingMode::HalfUp);
}

} // namespace QindaOffice::Books
