// SPDX-License-Identifier: GPL-3.0-or-later
// Reads payroll_<year>.json (payroll_tables_reader_p.h). The layout is
// documented in docs/payroll-tax-tables.md "The data file".
#include "payroll_tables_reader_p.h"

#include <QJsonArray>

namespace QindaOffice::Books::Payroll {

namespace {

const QList<std::pair<W4Status, const char *>> kW4Keys{
    {W4Status::MarriedJointly, "marriedJointly"},
    {W4Status::SingleOrMarriedSeparately, "singleOrMarriedSeparately"},
    {W4Status::HeadOfHousehold, "headOfHousehold"},
};

} // namespace

bool TablesReader::fail(const QString &path, const QString &why)
{
    if (m_error.isEmpty()) {
        m_error = QStringLiteral("%1: %2").arg(path, why);
    }
    return false;
}

QJsonValue TablesReader::value(const QString &path)
{
    QJsonValue v = m_root;
    for (const QString &part : path.split(QLatin1Char('.'))) {
        if (!v.isObject() || !v.toObject().contains(part)) {
            fail(path, QStringLiteral("missing"));
            return {};
        }
        v = v.toObject().value(part);
    }
    return v;
}

Money TablesReader::money(const QString &path)
{
    const QJsonValue v = value(path);
    const std::optional<Money> m = Money::parse(v.toString(), kCurrency);
    if (!v.isString() || !m) {
        fail(path, QStringLiteral("not an amount"));
        return usd(0);
    }
    return *m;
}

Decimal TablesReader::rate(const QString &path)
{
    const QJsonValue v = value(path);
    const std::optional<Decimal> d = Decimal::parse(v.toString());
    if (!v.isString() || !d || d->isNegative()) {
        fail(path, QStringLiteral("not a rate"));
        return {};
    }
    return *d;
}

int TablesReader::integer(const QString &path)
{
    const QJsonValue v = value(path);
    if (!v.isDouble() || v.toInt(-1) < 0) {
        fail(path, QStringLiteral("not a count"));
        return 0;
    }
    return v.toInt();
}

QString TablesReader::text(const QString &path, bool required)
{
    const QJsonValue v = value(path);
    if (!v.isString() || (required && v.toString().isEmpty())) {
        fail(path, QStringLiteral("not text"));
        return {};
    }
    return v.toString();
}

QDate TablesReader::date(const QString &path)
{
    const QDate d = QDate::fromString(text(path), Qt::ISODate);
    if (!d.isValid()) {
        fail(path, QStringLiteral("not a yyyy-MM-dd date"));
    }
    return d;
}

QList<QDate> TablesReader::dates(const QString &path)
{
    QList<QDate> out;
    const QJsonArray array = value(path).toArray();
    for (const QJsonValue &v : array) {
        const QDate d = QDate::fromString(v.toString(), Qt::ISODate);
        if (!d.isValid()) {
            fail(path, QStringLiteral("\"%1\" is not a yyyy-MM-dd date").arg(v.toString()));
            return {};
        }
        out << d;
    }
    if (out.isEmpty()) {
        fail(path, QStringLiteral("no dates"));
    }
    return out;
}

// Rows [A at least, B less than ("" = no limit), C base, D rate]. The
// shape is checked: the first row starts at zero, each row starts where the
// one before ends, only the last row is open-ended.
RateSchedule TablesReader::schedule(const QString &path)
{
    RateSchedule out;
    const QJsonArray rows = value(path).toArray();
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const QJsonArray row = rows.at(i).toArray();
        const QString where = QStringLiteral("%1[%2]").arg(path).arg(i);
        const std::optional<Money> over = Money::parse(row.at(0).toString(), kCurrency);
        const QString upToText = row.at(1).toString();
        const std::optional<Money> upTo = upToText.isEmpty() ? std::optional<Money>() : Money::parse(upToText, kCurrency);
        const std::optional<Money> base = Money::parse(row.at(2).toString(), kCurrency);
        const std::optional<Decimal> r = Decimal::parse(row.at(3).toString());
        if (row.size() != 4 || !over || !base || !r || (!upToText.isEmpty() && !upTo)) {
            fail(where, QStringLiteral("a row is [at least, less than, base, rate]"));
            return {};
        }
        const bool last = i == rows.size() - 1;
        if ((i == 0 && !over->isZero()) || (i > 0 && (!out.last().upTo || *out.last().upTo != *over))
            || (upTo.has_value() == last) || (upTo && *upTo <= *over)) {
            fail(where, QStringLiteral("rows must run from zero without gaps and end open"));
            return {};
        }
        out << Bracket{*over, upTo, *base, *r};
    }
    if (out.isEmpty()) {
        fail(path, QStringLiteral("no rows"));
    }
    return out;
}

QMap<FilingStatus, Money> TablesReader::byFilingStatus(const QString &path)
{
    QMap<FilingStatus, Money> out;
    for (const FilingStatus s : allFilingStatuses()) {
        out.insert(s, money(path + QLatin1Char('.') + filingStatusKey(s)));
    }
    return out;
}

QMap<FilingStatus, RateSchedule> TablesReader::schedulesByFilingStatus(const QString &path)
{
    QMap<FilingStatus, RateSchedule> out;
    for (const FilingStatus s : allFilingStatuses()) {
        out.insert(s, schedule(path + QLatin1Char('.') + filingStatusKey(s)));
    }
    return out;
}

QMap<W4Status, RateSchedule> TablesReader::schedulesByW4Status(const QString &path)
{
    QMap<W4Status, RateSchedule> out;
    for (const auto &[status, key] : kW4Keys) {
        out.insert(status, schedule(path + QLatin1Char('.') + QString::fromLatin1(key)));
    }
    return out;
}

UiRate TablesReader::uiRate(const QString &path)
{
    UiRate out;
    const QJsonArray parts = value(path).toArray();
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const QJsonObject o = parts.at(i).toObject();
        const std::optional<Decimal> r = Decimal::parse(o.value(QStringLiteral("rate")).toString());
        if (o.value(QStringLiteral("name")).toString().isEmpty() || !r || r->isNegative()) {
            fail(QStringLiteral("%1[%2]").arg(path).arg(i), QStringLiteral("a part is {name, rate}"));
            return {};
        }
        out << UiRatePart{o.value(QStringLiteral("name")).toString(), *r};
    }
    if (out.isEmpty()) {
        fail(path, QStringLiteral("no rate parts"));
    }
    return out;
}

QMap<PayFrequency, int> TablesReader::payPeriods(const QString &path)
{
    QMap<PayFrequency, int> out;
    const QJsonObject o = value(path).toObject();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        const std::optional<PayFrequency> f = payFrequencyFromKey(it.key());
        if (!f || it.value().toInt() <= 0) {
            fail(path + QLatin1Char('.') + it.key(), QStringLiteral("not a pay frequency with a period count"));
            return {};
        }
        out.insert(*f, it.value().toInt());
    }
    if (out.isEmpty()) {
        fail(path, QStringLiteral("no pay periods"));
    }
    return out;
}

void TablesReader::readFederal(PayrollTables &out)
{
    FederalWithholdingTables &w = out.m_withholding;
    w.payPeriods = payPeriods(QStringLiteral("federal.withholding.payPeriods"));
    w.step2UncheckedMarriedJointly = money(QStringLiteral("federal.withholding.step2Unchecked.marriedJointly"));
    w.step2UncheckedOther = money(QStringLiteral("federal.withholding.step2Unchecked.other"));
    w.allowanceValue = money(QStringLiteral("federal.withholding.allowance"));
    w.standard = schedulesByW4Status(QStringLiteral("federal.withholding.standard"));
    w.step2Checkbox = schedulesByW4Status(QStringLiteral("federal.withholding.step2Checkbox"));
    w.legacyMarried = schedule(QStringLiteral("federal.withholding.before2020.married"));
    w.legacySingle = schedule(QStringLiteral("federal.withholding.before2020.single"));

    FicaTables &f = out.m_fica;
    f.socialSecurityRate = rate(QStringLiteral("federal.fica.socialSecurityRate"));
    f.socialSecurityWageBase = money(QStringLiteral("federal.fica.socialSecurityWageBase"));
    f.medicareRate = rate(QStringLiteral("federal.fica.medicareRate"));
    f.additionalMedicareRate = rate(QStringLiteral("federal.fica.additionalMedicareRate"));
    f.additionalMedicareThreshold = money(QStringLiteral("federal.fica.additionalMedicareThreshold"));

    FutaTables &u = out.m_futa;
    u.grossRate = rate(QStringLiteral("federal.futa.rate"));
    u.maxCredit = rate(QStringLiteral("federal.futa.maxCredit"));
    u.wageBase = money(QStringLiteral("federal.futa.wageBase"));
    u.depositThreshold = money(QStringLiteral("federal.futa.depositThreshold"));
    u.creditReductionNote = text(QStringLiteral("federal.futa.creditReductionNote"));
    const QJsonObject reductions = value(QStringLiteral("federal.futa.creditReduction")).toObject();
    for (auto it = reductions.constBegin(); it != reductions.constEnd(); ++it) {
        u.creditReduction.insert(it.key(), rate(QStringLiteral("federal.futa.creditReduction.") + it.key()));
    }

    SupplementalTables &s = out.m_supplemental;
    s.flatRate = rate(QStringLiteral("federal.supplemental.flatRate"));
    s.mandatoryRate = rate(QStringLiteral("federal.supplemental.mandatoryRate"));
    s.mandatoryThreshold = money(QStringLiteral("federal.supplemental.mandatoryThreshold"));

    DepositTables &d = out.m_deposits;
    d.lookbackThreshold = money(QStringLiteral("federal.deposits.lookbackThreshold"));
    d.nextDayThreshold = money(QStringLiteral("federal.deposits.nextDayThreshold"));
    d.quarterlyDeMinimis = money(QStringLiteral("federal.deposits.quarterlyDeMinimis"));
    d.form944Limit = money(QStringLiteral("federal.deposits.form944Limit"));
    RetirementLimits &r = out.m_retirement;
    r.deferral401k = money(QStringLiteral("federal.retirement.deferralLimit401k"));
    r.catchUp401k = money(QStringLiteral("federal.retirement.catchUp401k"));
    r.deferralSimple = money(QStringLiteral("federal.retirement.deferralLimitSimple"));
    r.catchUpSimple = money(QStringLiteral("federal.retirement.catchUpSimple"));
    out.m_federalHolidays = dates(QStringLiteral("federal.holidays"));
}

void TablesReader::readEstimated(PayrollTables &out)
{
    EstimatedTaxTables &e = out.m_estimated;
    e.rateSchedules = schedulesByFilingStatus(QStringLiteral("estimatedTax.rateSchedules"));
    e.standardDeduction = byFilingStatus(QStringLiteral("estimatedTax.standardDeduction"));
    e.seNetEarningsFactor = rate(QStringLiteral("estimatedTax.selfEmployment.netEarningsFactor"));
    e.seSocialSecurityRate = rate(QStringLiteral("estimatedTax.selfEmployment.socialSecurityRate"));
    e.seMedicareRate = rate(QStringLiteral("estimatedTax.selfEmployment.medicareRate"));
    e.seMinimumNetEarnings = money(QStringLiteral("estimatedTax.selfEmployment.minimumNetEarnings"));
    e.seSocialSecurityWageBase = money(QStringLiteral("estimatedTax.selfEmployment.socialSecurityWageBase"));
    e.seDeductionShare = rate(QStringLiteral("estimatedTax.selfEmployment.deductionShare"));
    e.additionalMedicareRate = rate(QStringLiteral("estimatedTax.additionalMedicare.rate"));
    e.additionalMedicareThreshold = byFilingStatus(QStringLiteral("estimatedTax.additionalMedicare.threshold"));
    e.qbiRate = rate(QStringLiteral("estimatedTax.qbi.rate"));
    e.qbiThreshold = byFilingStatus(QStringLiteral("estimatedTax.qbi.threshold"));
    e.qbiMinimumDeduction = money(QStringLiteral("estimatedTax.qbi.minimumDeduction"));
    e.qbiMinimumQualifiedIncome = money(QStringLiteral("estimatedTax.qbi.minimumQualifiedIncome"));
    e.safeHarborCurrentYear = rate(QStringLiteral("estimatedTax.safeHarbor.currentYear"));
    e.safeHarborPriorYear = rate(QStringLiteral("estimatedTax.safeHarbor.priorYear"));
    e.safeHarborPriorYearHighIncome = rate(QStringLiteral("estimatedTax.safeHarbor.priorYearHighIncome"));
    e.highIncomeAgi = money(QStringLiteral("estimatedTax.safeHarbor.highIncomeAgi"));
    e.highIncomeAgiSeparate = money(QStringLiteral("estimatedTax.safeHarbor.highIncomeAgiSeparate"));
    e.minimumBalanceDue = money(QStringLiteral("estimatedTax.minimumBalanceDue"));
    e.dueDates = dates(QStringLiteral("estimatedTax.dueDates"));
}

void TablesReader::readColorado(PayrollTables &out)
{
    ColoradoTables &c = out.m_colorado;
    c.incomeTaxRate = rate(QStringLiteral("colorado.incomeTaxRate"));
    ColoradoWithholdingTables &w = c.withholding;
    w.rate = rate(QStringLiteral("colorado.withholding.rate"));
    w.payPeriods = payPeriods(QStringLiteral("colorado.withholding.payPeriods"));
    w.defaultAllowanceMarriedJointly = money(QStringLiteral("colorado.withholding.defaultAllowance.marriedJointly"));
    w.defaultAllowanceOther = money(QStringLiteral("colorado.withholding.defaultAllowance.other"));
    w.wholeDollars = value(QStringLiteral("colorado.withholding.wholeDollars")).toBool();
    c.uiWageBase = money(QStringLiteral("colorado.ui.wageBase"));
    c.uiIntroductoryRate = uiRate(QStringLiteral("colorado.ui.introductoryRate"));
    c.uiHeavyConstructionRate = uiRate(QStringLiteral("colorado.ui.heavyConstructionRate"));
    c.famliRate = rate(QStringLiteral("colorado.famli.rate"));
    c.famliMaxEmployeeShare = rate(QStringLiteral("colorado.famli.maxEmployeeShare"));
    c.famliSmallEmployerSize = integer(QStringLiteral("colorado.famli.smallEmployerSize"));
    c.famliWageBase = money(QStringLiteral("colorado.famli.wageBase"));
    const QJsonObject cities = value(QStringLiteral("colorado.opt")).toObject();
    for (auto it = cities.constBegin(); it != cities.constEnd(); ++it) {
        const QString base = QStringLiteral("colorado.opt.") + it.key();
        OccupationalPrivilegeTax opt;
        opt.key = it.key();
        opt.city = text(base + QStringLiteral(".city"));
        opt.employeeMonthly = money(base + QStringLiteral(".employeeMonthly"));
        opt.employerMonthly = money(base + QStringLiteral(".employerMonthly"));
        opt.monthlyThreshold = money(base + QStringLiteral(".monthlyThreshold"));
        opt.thresholdInclusive = text(base + QStringLiteral(".threshold")) == QLatin1String("atLeast");
        if (!opt.thresholdInclusive && text(base + QStringLiteral(".threshold")) != QLatin1String("moreThan")) {
            fail(base + QStringLiteral(".threshold"), QStringLiteral("\"atLeast\" or \"moreThan\""));
        }
        opt.employerOnOwners = value(base + QStringLiteral(".employerOnOwners")).toBool();
        opt.dueDay = integer(base + QStringLiteral(".dueDay"));
        opt.filing = text(base + QStringLiteral(".filing"));
        opt.status = text(base + QStringLiteral(".status"), false);
        c.opt << opt;
    }
    c.estimatedSafeHarborCurrentYear = rate(QStringLiteral("colorado.estimated.safeHarborCurrentYear"));
    c.estimatedSafeHarborPriorYear = rate(QStringLiteral("colorado.estimated.safeHarborPriorYear"));
    c.estimatedSafeHarborPriorYearHighIncome = rate(QStringLiteral("colorado.estimated.safeHarborPriorYearHighIncome"));
    c.estimatedHighIncomeAgi = money(QStringLiteral("colorado.estimated.highIncomeAgi"));
    c.estimatedHighIncomeAgiSeparate = money(QStringLiteral("colorado.estimated.highIncomeAgiSeparate"));
    c.estimatedMinimumBalanceDue = money(QStringLiteral("colorado.estimated.minimumBalanceDue"));
    c.estimatedDueDates = dates(QStringLiteral("colorado.estimated.dueDates"));
    c.withholdingMonthlyFrom = money(QStringLiteral("colorado.filing.monthlyFrom"));
    c.withholdingWeeklyAbove = money(QStringLiteral("colorado.filing.weeklyAbove"));
    out.m_coloradoHolidays = dates(QStringLiteral("colorado.holidays"));
}

bool TablesReader::read(PayrollTables &out)
{
    out.m_year = integer(QStringLiteral("taxYear"));
    for (const QJsonValue &v : value(QStringLiteral("sources")).toArray()) {
        const QJsonObject o = v.toObject();
        out.m_sources << TableSource{o.value(QStringLiteral("id")).toString(), o.value(QStringLiteral("title")).toString(),
                                     o.value(QStringLiteral("url")).toString(), o.value(QStringLiteral("revised")).toString(),
                                     o.value(QStringLiteral("checked")).toString()};
    }
    for (const QJsonValue &v : value(QStringLiteral("unconfirmed")).toArray()) {
        const QJsonObject o = v.toObject();
        out.m_unconfirmed << UnconfirmedValue{o.value(QStringLiteral("path")).toString(),
                                              o.value(QStringLiteral("label")).toString(),
                                              o.value(QStringLiteral("note")).toString()};
    }
    readFederal(out);
    readEstimated(out);
    readColorado(out);
    if (m_error.isEmpty() && out.m_sources.isEmpty()) {
        fail(QStringLiteral("sources"), QStringLiteral("every table names its sources"));
    }
    return m_error.isEmpty();
}

} // namespace QindaOffice::Books::Payroll
