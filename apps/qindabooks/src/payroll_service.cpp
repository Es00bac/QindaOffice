// SPDX-License-Identifier: GPL-3.0-or-later
// PayrollService: setup, owners and employees (payroll_service.h).
#include "payroll_service.h"

#include "books_service.h"

#include "company_file.h"
#include "employees.h"
#include "lists.h"
#include "payroll_records.h"

#include "colorado_payroll.h"
#include "payroll_tables.h"

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;
using namespace ::QindaOffice::Books::Payroll;

namespace {

QString moneyText(const Money &m)
{
    return m.isZero() ? QString() : m.toPlainString();
}

// "50%" or "50" → 0.5; "" → `fallback`.
Decimal shareFrom(const QVariant &value, const Decimal &fallback)
{
    QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return fallback;
    }
    if (!text.endsWith(QLatin1Char('%'))) {
        text += QLatin1Char('%');
    }
    return Decimal::parse(text).value_or(fallback);
}

QString uiRateText(const UiRate &rate)
{
    QStringList parts;
    for (const UiRatePart &p : rate) {
        parts << p.rate.toPercentString();
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace

PayrollService::PayrollService(BooksService *books, QObject *parent)
    : QObject(parent)
    , m_books(books)
{
    connect(books, &BooksService::changed, this, &PayrollService::changed);
    connect(books, &BooksService::companyChanged, this, [this] {
        m_reminded.clear();
        emit changed();
    });
}

CompanyFile *PayrollService::file() const
{
    return m_books->file();
}

QString PayrollService::entity() const
{
    return file() != nullptr ? entityTaxKey(PayrollRecords(*file()).setup().entity) : QStringLiteral("unsure");
}

bool PayrollService::configured() const
{
    return file() != nullptr && PayrollRecords(*file()).setup().configured;
}

bool PayrollService::colorado() const
{
    return file() != nullptr && PayrollRecords(*file()).setup().colorado;
}

int PayrollService::tablesYear() const
{
    const int year = m_books->todayDate().year();
    return PayrollTables::forYear(year).has_value() ? year : 0;
}

QVariantMap PayrollService::setup() const
{
    if (file() == nullptr) {
        return {};
    }
    const PayrollSetup s = PayrollRecords(*file()).setup();
    QVariantList parts;
    for (const UiRatePart &p : s.uiRate) {
        parts << QVariantMap{{QStringLiteral("name"), p.name}, {QStringLiteral("rate"), p.rate.toPercentString()}};
    }
    return {{QStringLiteral("entity"), entityTaxKey(s.entity)},
            {QStringLiteral("configured"), s.configured},
            {QStringLiteral("ein"), s.ein.isEmpty() ? QString() : s.ein.left(2) + QLatin1Char('-') + s.ein.mid(2)},
            {QStringLiteral("einMasked"), maskedEin(s.ein)},
            {QStringLiteral("colorado"), s.colorado},
            {QStringLiteral("stateCode"), s.stateCode},
            {QStringLiteral("coloradoAccount"), s.coloradoAccount},
            {QStringLiteral("uiAccount"), s.uiAccount},
            {QStringLiteral("famliAccount"), s.famliAccount},
            {QStringLiteral("depositSchedule"), depositScheduleKey(s.depositSchedule)},
            {QStringLiteral("coloradoFrequency"), coloradoFrequencyKey(s.coloradoFrequency)},
            {QStringLiteral("uiRateParts"), parts},
            {QStringLiteral("uiRate"), uiRateText(s.uiRate)},
            {QStringLiteral("uiRateText"), s.uiRate.isEmpty() ? QString() : uiRateTotal(s.uiRate).toPercentString()},
            {QStringLiteral("uiHeavyConstruction"), s.uiHeavyConstruction},
            {QStringLiteral("smallEmployer"), s.smallEmployer},
            {QStringLiteral("famliEmployeeShare"), s.famliEmployeeShare.toPercentString()},
            {QStringLiteral("famliEmployerShare"), s.famliEmployerShare.toPercentString()},
            {QStringLiteral("optCity"), s.optCity},
            {QStringLiteral("optFiling"), s.optFiling},
            {QStringLiteral("roundFederalWithholding"), s.roundFederalWithholding},
            {QStringLiteral("payrollBankAccountId"), s.payrollBankAccountId},
            {QStringLiteral("setAsideAccountId"), s.setAsideAccountId},
            {QStringLiteral("bsoUserId"), s.bsoUserId},
            {QStringLiteral("w2ContactName"), s.w2ContactName},
            {QStringLiteral("w2ContactPhone"), s.w2ContactPhone},
            {QStringLiteral("w2ContactEmail"), s.w2ContactEmail},
            {QStringLiteral("signaturePin"), s.signaturePin}};
}

QVariantMap PayrollService::saveSetup(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    PayrollRecords records(*c);
    PayrollSetup s = records.setup();
    const auto has = [&map](const char *key) { return map.contains(QString::fromLatin1(key)); };
    const auto text = [&map](const char *key) { return map.value(QString::fromLatin1(key)).toString().trimmed(); };
    if (has("entity")) {
        s.entity = entityTaxFromKey(text("entity")).value_or(EntityTax::Unsure);
    }
    if (has("ein")) {
        if (!text("ein").isEmpty() && normalizedTaxId(text("ein"), 9).isEmpty()) {
            return Format::failure(tr("An EIN has nine digits (12-3456789)."));
        }
        s.ein = normalizedTaxId(text("ein"), 9);
    }
    if (has("colorado")) {
        s.colorado = map.value(QStringLiteral("colorado")).toBool();
    }
    for (const auto &[key, field] : {std::pair{"stateCode", &s.stateCode},
                                     std::pair{"coloradoAccount", &s.coloradoAccount}, std::pair{"uiAccount", &s.uiAccount},
                                     std::pair{"famliAccount", &s.famliAccount}, std::pair{"optCity", &s.optCity},
                                     std::pair{"optFiling", &s.optFiling}, std::pair{"bsoUserId", &s.bsoUserId},
                                     std::pair{"w2ContactName", &s.w2ContactName},
                                     std::pair{"w2ContactPhone", &s.w2ContactPhone},
                                     std::pair{"w2ContactEmail", &s.w2ContactEmail},
                                     std::pair{"signaturePin", &s.signaturePin}}) {
        if (has(key)) {
            *field = text(key);
        }
    }
    if (has("depositSchedule")) {
        s.depositSchedule = depositScheduleFromKey(text("depositSchedule")).value_or(DepositSchedule::Monthly);
    }
    if (has("coloradoFrequency")) {
        s.coloradoFrequency = coloradoFrequencyFromKey(text("coloradoFrequency")).value_or(ColoradoFrequency::Quarterly);
    }
    if (has("uiRate")) {
        // "1.53%, 0.17%, 1.35%" by CDLE's parts, or one assigned rate.
        UiRate rate;
        const QStringList pieces = text("uiRate").split(QLatin1Char(','), Qt::SkipEmptyParts);
        const std::optional<PayrollTables> tables = PayrollTables::forYear(m_books->todayDate().year());
        for (qsizetype i = 0; i < pieces.size(); ++i) {
            const Decimal r = shareFrom(pieces.at(i), Decimal(-1, 0));
            if (r.isNegative()) {
                return Format::failure(tr("\"%1\" is not a rate.").arg(pieces.at(i).trimmed()));
            }
            const QString name = pieces.size() == 1 ? tr("Assigned rate")
                : (tables && i < tables->colorado().uiIntroductoryRate.size() ? tables->colorado().uiIntroductoryRate.at(i).name
                                                                             : tr("Part %1").arg(i + 1));
            rate << UiRatePart{name, r};
        }
        s.uiRate = rate;
    }
    for (const auto &[key, field] : {std::pair{"uiHeavyConstruction", &s.uiHeavyConstruction},
                                     std::pair{"smallEmployer", &s.smallEmployer},
                                     std::pair{"roundFederalWithholding", &s.roundFederalWithholding}}) {
        if (has(key)) {
            *field = map.value(QString::fromLatin1(key)).toBool();
        }
    }
    if (has("famliEmployeeShare")) {
        s.famliEmployeeShare = shareFrom(map.value(QStringLiteral("famliEmployeeShare")), Decimal(5, 1));
    }
    if (has("famliEmployerShare")) {
        s.famliEmployerShare = shareFrom(map.value(QStringLiteral("famliEmployerShare")), Decimal(0, 0));
    }
    if (has("payrollBankAccountId")) {
        s.payrollBankAccountId = Format::id(map.value(QStringLiteral("payrollBankAccountId")));
    }
    if (has("setAsideAccountId")) {
        s.setAsideAccountId = Format::id(map.value(QStringLiteral("setAsideAccountId")));
    }
    const Result saved = records.saveSetup(s);
    if (!saved.ok) {
        return Format::result(saved);
    }
    return Format::result(records.ensurePayrollAccounts());
}

QVariantMap PayrollService::tablesInfo() const
{
    const int year = m_books->todayDate().year();
    QString error;
    const std::optional<PayrollTables> t = PayrollTables::forYear(year, &error);
    if (!t) {
        return {{QStringLiteral("ok"), false}, {QStringLiteral("year"), year}, {QStringLiteral("error"), error}};
    }
    QVariantList sources;
    for (const TableSource &s : t->sources()) {
        sources << QVariantMap{{QStringLiteral("title"), s.title}, {QStringLiteral("revised"), s.revised},
                               {QStringLiteral("url"), s.url}};
    }
    QVariantList unconfirmed;
    for (const UnconfirmedValue &u : t->unconfirmed()) {
        unconfirmed << QVariantMap{{QStringLiteral("path"), u.path}, {QStringLiteral("label"), u.label},
                                   {QStringLiteral("note"), u.note}};
    }
    QVariantList cities{QVariantMap{{QStringLiteral("key"), QString()}, {QStringLiteral("city"), tr("None")}}};
    for (const OccupationalPrivilegeTax &o : t->colorado().opt) {
        cities << QVariantMap{{QStringLiteral("key"), o.key},
                              // A city that repealed its tax (Aurora) stays listed, marked.
                              {QStringLiteral("city"), o.status.isEmpty() ? o.city : tr("%1 (repealed)").arg(o.city)},
                              {QStringLiteral("employeeText"), o.employeeMonthly.toPlainString()},
                              {QStringLiteral("employerText"), o.employerMonthly.toPlainString()},
                              {QStringLiteral("threshold"), o.monthlyThreshold.toPlainString()},
                              {QStringLiteral("filing"), o.filing},
                              {QStringLiteral("status"), o.status}};
    }
    return {{QStringLiteral("ok"), true},
            {QStringLiteral("year"), t->year()},
            {QStringLiteral("sources"), sources},
            {QStringLiteral("unconfirmed"), unconfirmed},
            {QStringLiteral("cities"), cities},
            {QStringLiteral("introductoryRateText"), uiRateTotal(t->colorado().uiIntroductoryRate).toPercentString()},
            // The thresholds the setup's hints quote, from the same tables.
            {QStringLiteral("coloradoMonthlyFrom"), usdText(t->colorado().withholdingMonthlyFrom)},
            {QStringLiteral("coloradoWeeklyAbove"), usdText(t->colorado().withholdingWeeklyAbove)},
            {QStringLiteral("famliSmallEmployerSize"), t->colorado().famliSmallEmployerSize},
            {QStringLiteral("introductoryRate"), uiRateText(t->colorado().uiIntroductoryRate)}};
}

QVariantList PayrollService::owners() const
{
    QVariantList out;
    if (file() == nullptr) {
        return out;
    }
    for (const Party &p : Lists(*file()).parties(PartyKind::Owner)) {
        out << QVariantMap{{QStringLiteral("id"), p.id}, {QStringLiteral("name"), p.displayName},
                           {QStringLiteral("address"), p.billingAddress}};
    }
    return out;
}

QVariantMap PayrollService::saveOwner(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    Lists lists(*c);
    Party p;
    const qint64 id = Format::id(map.value(QStringLiteral("id")));
    if (id != 0) {
        const std::optional<Party> existing = lists.party(id);
        if (!existing || existing->kind != PartyKind::Owner) {
            return Format::failure(tr("That owner no longer exists."));
        }
        p = *existing;
    }
    p.kind = PartyKind::Owner;
    p.displayName = map.value(QStringLiteral("name")).toString().trimmed();
    p.billingAddress = map.value(QStringLiteral("address")).toString();
    return Format::result(id != 0 ? lists.updateParty(p) : lists.createParty(p));
}

QVariantList PayrollService::employees() const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = m_books->moneyFormat();
    PayrollRecords records(*c);
    for (const Employee &plain : Employees(*c).employees(true)) {
        const EmployeePayroll e = records.employee(plain.id);
        const bool hourly = e.payType == EmployeePayroll::PayType::Hourly;
        const Money rate = Money(100, money.currency).multiplied(e.payRate);
        out << QVariantMap{{QStringLiteral("id"), plain.id},
                           {QStringLiteral("name"), e.exists ? e.fullName() : plain.name},
                           {QStringLiteral("hasPayroll"), e.exists},
                           {QStringLiteral("active"), plain.active},
                           {QStringLiteral("released"), e.releaseDate.isValid()},
                           {QStringLiteral("payTypeText"), !e.exists ? QString() : (hourly ? tr("Hourly") : tr("Salary"))},
                           {QStringLiteral("payText"), !e.exists ? QString()
                                                                 : (hourly ? tr("%1 an hour").arg(money.text(rate.minor()))
                                                                           : tr("%1 a year").arg(money.text(rate.minor())))},
                           {QStringLiteral("frequencyText"), e.exists ? payFrequencyLabel(e.frequency) : QString()},
                           {QStringLiteral("ssnMasked"), maskedSsn(e.ssn)},
                           {QStringLiteral("ownerEmployee"), e.ownerEmployee}};
    }
    return out;
}

QVariantMap PayrollService::employee(qint64 id) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return {};
    }
    const EmployeePayroll e = PayrollRecords(*c).employee(id);
    const W4Certificate &w = e.w4;
    const PayDeductions &d = e.deductions;
    QVariantList frequencies;
    for (const PayFrequency f : allPayFrequencies()) {
        frequencies << QVariantMap{{QStringLiteral("key"), payFrequencyKey(f)}, {QStringLiteral("label"), payFrequencyLabel(f)}};
    }
    static const QStringList retirementKeys{QStringLiteral("none"), QStringLiteral("401k"), QStringLiteral("simple")};
    return {{QStringLiteral("id"), id},
            {QStringLiteral("hasPayroll"), e.exists},
            {QStringLiteral("firstName"), e.firstName},
            {QStringLiteral("middleInitial"), e.middleInitial},
            {QStringLiteral("lastName"), e.lastName},
            {QStringLiteral("suffix"), e.suffix},
            {QStringLiteral("ssn"), e.ssn.isEmpty() ? QString() : e.ssn.left(3) + QLatin1Char('-') + e.ssn.mid(3, 2) + QLatin1Char('-') + e.ssn.right(4)},
            {QStringLiteral("ssnMasked"), maskedSsn(e.ssn)},
            {QStringLiteral("street"), e.street},
            {QStringLiteral("city"), e.city},
            {QStringLiteral("state"), e.state},
            {QStringLiteral("zip"), e.zip},
            {QStringLiteral("hireDate"), Format::isoDate(e.hireDate)},
            {QStringLiteral("releaseDate"), Format::isoDate(e.releaseDate)},
            {QStringLiteral("payType"), e.payType == EmployeePayroll::PayType::Hourly ? QStringLiteral("hourly") : QStringLiteral("salary")},
            {QStringLiteral("payRate"), e.payRate.isZero() ? QString() : e.payRate.toString()},
            {QStringLiteral("frequency"), payFrequencyKey(e.frequency)},
            {QStringLiteral("frequencies"), frequencies},
            {QStringLiteral("defaultHours"), e.defaultHours.isZero() ? QString() : e.defaultHours.toString()},
            {QStringLiteral("w4Form"), w.form == W4Certificate::Form::Before2020 ? 2019 : 2020},
            {QStringLiteral("w4Status"), w4StatusKey(w.status)},
            {QStringLiteral("step2"), w.step2Checked},
            {QStringLiteral("step3"), moneyText(w.step3Credits)},
            {QStringLiteral("step4a"), moneyText(w.step4aOtherIncome)},
            {QStringLiteral("step4b"), moneyText(w.step4bDeductions)},
            {QStringLiteral("step4c"), moneyText(w.step4cExtra)},
            {QStringLiteral("legacyStatus"), w4LegacyStatusKey(w.legacyStatus)},
            {QStringLiteral("allowances"), w.allowances},
            {QStringLiteral("legacyExtra"), moneyText(w.legacyExtra)},
            {QStringLiteral("exempt"), w.exempt},
            {QStringLiteral("w4OnFile"), e.w4OnFile},
            {QStringLiteral("dr0004Filed"), e.dr0004.filed},
            {QStringLiteral("dr0004Allowance"), e.dr0004.annualAllowance ? e.dr0004.annualAllowance->toPlainString() : QString()},
            {QStringLiteral("dr0004Extra"), moneyText(e.dr0004.additionalPerPeriod)},
            {QStringLiteral("coloradoWithholding"), e.coloradoWithholding},
            {QStringLiteral("optCity"), e.optCity},
            {QStringLiteral("ownerEmployee"), e.ownerEmployee},
            {QStringLiteral("shareholder2Percent"), e.shareholder2Percent},
            {QStringLiteral("section125"), moneyText(d.section125)},
            {QStringLiteral("retirementKind"), retirementKeys.value(int(d.retirement))},
            {QStringLiteral("retirementAmount"), moneyText(d.retirementAmount)},
            {QStringLiteral("retirementPercent"), d.retirementPercent.isZero() ? QString() : d.retirementPercent.toPercentString()},
            {QStringLiteral("shareholderHealth"), moneyText(d.shareholderHealth)},
            {QStringLiteral("afterTax"), moneyText(d.afterTax)},
            {QStringLiteral("afterTaxLabel"), d.afterTaxLabel},
            {QStringLiteral("customItems"), customItemsFor(e)}};
}

QVariantMap PayrollService::saveEmployee(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    const MoneyFormat money = m_books->moneyFormat();
    QString problem;
    const auto amount = [&](const char *key) -> Money {
        QString error;
        const std::optional<qint64> v = money.parse(map.value(QString::fromLatin1(key)), &error);
        if (!v && problem.isEmpty()) {
            problem = error;
        }
        return Money(v.value_or(0), money.currency);
    };
    const auto decimal = [&](const char *key) -> Decimal {
        const QString text = map.value(QString::fromLatin1(key)).toString().trimmed();
        const std::optional<Decimal> d = text.isEmpty() ? std::optional<Decimal>(Decimal()) : Decimal::parse(text);
        if (!d && problem.isEmpty()) {
            problem = tr("\"%1\" is not a number.").arg(text);
        }
        return d.value_or(Decimal());
    };
    qint64 id = Format::id(map.value(QStringLiteral("id")));
    EmployeePayroll e = id != 0 ? PayrollRecords(*c).employee(id) : EmployeePayroll{};
    const auto text = [&map](const char *key) { return map.value(QString::fromLatin1(key)).toString().trimmed(); };
    e.firstName = text("firstName");
    e.middleInitial = text("middleInitial");
    e.lastName = text("lastName");
    e.suffix = text("suffix");
    if (map.contains(QStringLiteral("ssn"))) {
        e.ssn = text("ssn");
    }
    e.street = text("street");
    e.city = text("city");
    e.state = text("state");
    e.zip = text("zip");
    e.hireDate = Format::date(map.value(QStringLiteral("hireDate")));
    e.releaseDate = Format::date(map.value(QStringLiteral("releaseDate")));
    e.payType = text("payType") == QLatin1String("hourly") ? EmployeePayroll::PayType::Hourly : EmployeePayroll::PayType::Salary;
    e.payRate = decimal("payRate");
    e.frequency = payFrequencyFromKey(text("frequency")).value_or(PayFrequency::Biweekly);
    e.defaultHours = decimal("defaultHours");
    W4Certificate &w = e.w4;
    w.form = map.value(QStringLiteral("w4Form")).toInt() == 2019 ? W4Certificate::Form::Before2020 : W4Certificate::Form::From2020;
    w.status = w4StatusFromKey(text("w4Status")).value_or(W4Status::SingleOrMarriedSeparately);
    w.step2Checked = map.value(QStringLiteral("step2")).toBool();
    w.step3Credits = amount("step3");
    w.step4aOtherIncome = amount("step4a");
    w.step4bDeductions = amount("step4b");
    w.step4cExtra = amount("step4c");
    w.legacyStatus = w4LegacyStatusFromKey(text("legacyStatus")).value_or(W4LegacyStatus::Single);
    w.allowances = map.value(QStringLiteral("allowances")).toInt();
    w.legacyExtra = amount("legacyExtra");
    w.exempt = map.value(QStringLiteral("exempt")).toBool();
    e.w4OnFile = map.value(QStringLiteral("w4OnFile"), true).toBool();
    e.dr0004.filed = map.value(QStringLiteral("dr0004Filed")).toBool();
    e.dr0004.annualAllowance = text("dr0004Allowance").isEmpty() ? std::optional<Money>() : amount("dr0004Allowance");
    e.dr0004.additionalPerPeriod = amount("dr0004Extra");
    e.coloradoWithholding = map.value(QStringLiteral("coloradoWithholding"), true).toBool();
    e.optCity = text("optCity");
    e.ownerEmployee = map.value(QStringLiteral("ownerEmployee")).toBool();
    e.shareholder2Percent = map.value(QStringLiteral("shareholder2Percent")).toBool();
    PayDeductions &d = e.deductions;
    d.section125 = amount("section125");
    d.retirement = text("retirementKind") == QLatin1String("401k")     ? PayDeductions::Retirement::Plan401k
                   : text("retirementKind") == QLatin1String("simple") ? PayDeductions::Retirement::SimpleIra
                                                                       : PayDeductions::Retirement::None;
    d.retirementAmount = amount("retirementAmount");
    d.retirementPercent = shareFrom(map.value(QStringLiteral("retirementPercent")), Decimal());
    d.shareholderHealth = amount("shareholderHealth");
    d.afterTax = amount("afterTax");
    d.afterTaxLabel = text("afterTaxLabel");
    if (map.contains(QStringLiteral("customItems")) && problem.isEmpty()) {
        applyCustomItems(map.value(QStringLiteral("customItems")).toList(), &e, &problem);
    }
    if (!problem.isEmpty()) {
        return Format::failure(problem);
    }
    // A new employee joins M68's list and gets the payroll half in one
    // transaction: a refused payroll record leaves no bare employee behind.
    SqlTransaction txn(*c);
    if (id == 0) {
        Employee plain;
        plain.name = e.fullName();
        const Result made = Employees(*c).create(plain);
        if (!made.ok) {
            return Format::result(made);
        }
        id = made.id;
    }
    e.employeeId = id;
    const Result saved = PayrollRecords(*c).saveEmployee(e);
    if (!saved.ok) {
        return Format::result(saved);
    }
    if (!txn.commit()) {
        return Format::failure(txn.error());
    }
    return Format::success(id);
}

} // namespace QindaOffice::Apps::Books
