// SPDX-License-Identifier: GPL-3.0-or-later
// PayrollService: Run Payroll, paychecks, liabilities and the files for
// filing (payroll_service.h).
#include "payroll_service.h"

#include "books_service.h"

#include "accounts.h"
#include "company_file.h"
#include "documents.h"
#include "efw2_export.h"
#include "payroll_ledger.h"
#include "payroll_liabilities.h"
#include "payroll_records.h"
#include "payroll_reports.h"
#include "report_output.h"

#include <QJsonArray>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;
using namespace ::QindaOffice::Books::Payroll;

namespace {

QDate lastDay(const QDate &d)
{
    return QDate(d.year(), d.month(), d.daysInMonth());
}

// The pay period that ends on the pay date, by frequency.
std::pair<QDate, QDate> periodFor(PayFrequency f, const QDate &pay)
{
    switch (f) {
    case PayFrequency::Daily: return {pay, pay};
    case PayFrequency::Weekly: return {pay.addDays(-6), pay};
    case PayFrequency::Biweekly: return {pay.addDays(-13), pay};
    case PayFrequency::Semimonthly:
        return pay.day() <= 15 ? std::pair{QDate(pay.year(), pay.month(), 1), QDate(pay.year(), pay.month(), 15)}
                               : std::pair{QDate(pay.year(), pay.month(), 16), lastDay(pay)};
    case PayFrequency::Monthly: return {QDate(pay.year(), pay.month(), 1), lastDay(pay)};
    case PayFrequency::Quarterly: return {pay.addMonths(-3).addDays(1), pay};
    case PayFrequency::Semiannual: return {pay.addMonths(-6).addDays(1), pay};
    case PayFrequency::Annual: return {pay.addYears(-1).addDays(1), pay};
    }
    return {pay, pay};
}

QVariantList worksheet(const QJsonArray &lines)
{
    QVariantList out;
    for (const QJsonValue &v : lines) {
        out << v.toObject().toVariantMap();
    }
    return out;
}

} // namespace

bool PayrollService::requestFrom(const QVariantMap &row, const QVariantMap &run, PaycheckRequest *r, QString *error) const
{
    const MoneyFormat money = m_books->moneyFormat();
    r->employeeId = Format::id(row.value(QStringLiteral("employeeId")));
    r->payDate = Format::date(run.value(QStringLiteral("payDate")));
    r->periodStart = Format::date(run.value(QStringLiteral("periodStart")));
    r->periodEnd = Format::date(run.value(QStringLiteral("periodEnd")));
    r->bankAccountId = Format::id(run.value(QStringLiteral("bankAccountId")));
    r->toPrint = run.value(QStringLiteral("toPrint")).toBool();
    r->checkNumber = row.value(QStringLiteral("checkNumber")).toString().trimmed();
    const QString hours = row.value(QStringLiteral("hours")).toString().trimmed();
    if (!hours.isEmpty()) {
        const std::optional<Decimal> h = Decimal::parse(hours);
        if (!h || h->isNegative()) {
            *error = tr("\"%1\" is not a number of hours.").arg(hours);
            return false;
        }
        r->hours = *h;
    }
    const QString pay = row.value(QStringLiteral("pay")).toString().trimmed();
    if (!pay.isEmpty()) {
        const std::optional<qint64> p = money.parse(pay, error);
        if (!p) {
            return false;
        }
        r->regular = money.money(*p);
    }
    const std::optional<qint64> bonus = money.parse(row.value(QStringLiteral("bonus")), error);
    if (!bonus) {
        return false;
    }
    r->bonus = money.money(*bonus);
    if (!r->payDate.isValid()) {
        *error = tr("Enter the pay date (yyyy-mm-dd).");
        return false;
    }
    return true;
}

QVariantMap PayrollService::payrollDraft(const QString &payDateIso) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return {};
    }
    QDate pay = Format::date(payDateIso);
    if (!pay.isValid()) {
        pay = m_books->todayDate();
    }
    PayrollRecords records(*c);
    const PayrollSetup setup = records.setup();
    const MoneyFormat money = m_books->moneyFormat();
    QVariantList rows;
    std::optional<PayFrequency> frequency;
    for (const EmployeePayroll &e : records.employees()) {
        const bool hourly = e.payType == EmployeePayroll::PayType::Hourly;
        frequency = frequency.value_or(e.frequency);
        rows << QVariantMap{{QStringLiteral("employeeId"), e.employeeId},
                            {QStringLiteral("name"), e.fullName()},
                            {QStringLiteral("hourly"), hourly},
                            {QStringLiteral("hours"), hourly ? e.defaultHours.toString() : QString()},
                            {QStringLiteral("pay"), QString()},
                            {QStringLiteral("payText"), money.text(records.grossPay(e).minor())},
                            {QStringLiteral("frequencyText"), payFrequencyLabel(e.frequency)},
                            {QStringLiteral("bonus"), QString()},
                            {QStringLiteral("checkNumber"), QString()}};
    }
    const auto [start, end] = periodFor(frequency.value_or(PayFrequency::Biweekly), pay);
    qint64 bank = setup.payrollBankAccountId;
    if (!ChartOfAccounts(*c).account(bank)) {
        const QList<Account> banks = ChartOfAccounts(*c).accountsOfType(AccountType::Bank);
        bank = banks.isEmpty() ? 0 : banks.first().id;
    }
    return {{QStringLiteral("payDate"), Format::isoDate(pay)},
            {QStringLiteral("periodStart"), Format::isoDate(start)},
            {QStringLiteral("periodEnd"), Format::isoDate(end)},
            {QStringLiteral("bankAccountId"), bank},
            {QStringLiteral("rows"), rows}};
}

QVariantMap PayrollService::previewPayroll(const QVariantMap &run) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    const MoneyFormat money = m_books->moneyFormat();
    PayrollLedger ledger(*c);
    QVariantList rows;
    qint64 totals[6] = {};
    bool allOk = true;
    for (const QVariant &v : run.value(QStringLiteral("rows")).toList()) {
        const QVariantMap row = v.toMap();
        PaycheckRequest request;
        QString error;
        PaycheckRecord r;
        if (requestFrom(row, run, &request, &error)) {
            r = ledger.preview(request, &error);
        }
        const PaycheckTaxes &t = r.taxes;
        const bool ok = error.isEmpty() && t.ok;
        allOk = allOk && ok;
        const auto text = [&](const Money &m) { return money.text(m.minor()); };
        rows << QVariantMap{{QStringLiteral("employeeId"), request.employeeId},
                            {QStringLiteral("name"), ok ? r.employeeName : row.value(QStringLiteral("name"))},
                            {QStringLiteral("ok"), ok},
                            {QStringLiteral("error"), error},
                            {QStringLiteral("gross"), t.gross.minor()},
                            {QStringLiteral("grossText"), text(t.gross)},
                            {QStringLiteral("fitText"), text(t.federalIncomeTax)},
                            {QStringLiteral("ssText"), text(t.socialSecurity)},
                            {QStringLiteral("medicareText"), text(t.medicare + t.additionalMedicare)},
                            {QStringLiteral("coloradoText"), text(t.coloradoIncomeTax)},
                            {QStringLiteral("famliText"), text(t.famliEmployee)},
                            {QStringLiteral("optText"), text(t.optEmployee)},
                            {QStringLiteral("deductionsText"), text(t.preTaxDeductions + t.afterTaxDeductions)},
                            {QStringLiteral("net"), t.netPay.minor()},
                            {QStringLiteral("netText"), text(t.netPay)},
                            {QStringLiteral("employerText"), text(t.employerTaxes)},
                            {QStringLiteral("notes"), t.notes},
                            {QStringLiteral("federalWorksheet"), worksheet(r.details.value(QStringLiteral("federal")).toArray())},
                            {QStringLiteral("coloradoWorksheet"), worksheet(r.details.value(QStringLiteral("colorado")).toArray())}};
        if (ok) {
            const qint64 values[6] = {t.gross.minor(), t.employeeTaxes.minor(), (t.preTaxDeductions + t.afterTaxDeductions).minor(),
                                      t.netPay.minor(), t.employerTaxes.minor(), (t.gross + t.employerTaxes).minor()};
            for (int i = 0; i < 6; ++i) {
                totals[i] += values[i];
            }
        }
    }
    return {{QStringLiteral("ok"), allOk && !rows.isEmpty()},
            {QStringLiteral("error"), rows.isEmpty() ? tr("Nobody is set up for payroll yet.") : QString()},
            {QStringLiteral("rows"), rows},
            {QStringLiteral("totals"), QVariantMap{{QStringLiteral("grossText"), money.text(totals[0])},
                                                   {QStringLiteral("withheldText"), money.text(totals[1])},
                                                   {QStringLiteral("deductionsText"), money.text(totals[2])},
                                                   {QStringLiteral("netText"), money.text(totals[3])},
                                                   {QStringLiteral("employerText"), money.text(totals[4])},
                                                   {QStringLiteral("costText"), money.text(totals[5])}}}};
}

QVariantMap PayrollService::runPayroll(const QVariantMap &run)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    QList<PaycheckRequest> requests;
    for (const QVariant &v : run.value(QStringLiteral("rows")).toList()) {
        PaycheckRequest request;
        QString error;
        if (!requestFrom(v.toMap(), run, &request, &error)) {
            return Format::failure(error);
        }
        requests << request;
    }
    QList<qint64> ids;
    const Result r = PayrollLedger(*c).createPaychecks(requests, &ids);
    QVariantMap out = Format::result(r);
    QVariantList list;
    for (const qint64 id : std::as_const(ids)) {
        list << id;
    }
    out.insert(QStringLiteral("ids"), list);
    return out;
}

QVariantList PayrollService::paychecks(const QString &fromIso, const QString &toIso) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = m_books->moneyFormat();
    for (const PaycheckRecord &r : PayrollLedger(*c).paychecks(Format::date(fromIso), Format::date(toIso), 0, true)) {
        out << QVariantMap{{QStringLiteral("id"), r.docId},
                           {QStringLiteral("date"), Format::isoDate(r.payDate)},
                           {QStringLiteral("number"), r.number},
                           {QStringLiteral("employeeName"), r.employeeName},
                           {QStringLiteral("grossText"), money.text(r.taxes.gross.minor())},
                           {QStringLiteral("withheldText"), money.text(r.taxes.employeeTaxes.minor())},
                           {QStringLiteral("netText"), money.text(r.taxes.netPay.minor())},
                           {QStringLiteral("employerText"), money.text(r.taxes.employerTaxes.minor())},
                           {QStringLiteral("voided"), r.voided},
                           {QStringLiteral("status"), r.voided ? tr("Void") : (r.replacesDocId != 0 ? tr("Adjusted") : QString())}};
    }
    return out;
}

QVariantMap PayrollService::paycheck(qint64 id) const
{
    CompanyFile *c = file();
    const std::optional<PaycheckRecord> r = c != nullptr ? PayrollLedger(*c).paycheck(id) : std::nullopt;
    if (!r) {
        return {};
    }
    const MoneyFormat money = m_books->moneyFormat();
    const PayrollYearToDate y = PayrollLedger(*c).yearToDate(r->employeeId, r->payDate, r->docId);
    const PaycheckTaxes &t = r->taxes;
    // AGENT-CONTRACT (with books_io pay_stub.cpp fillPayStub): the same
    // lines as the printed stub — gross, the taxes every check has, net,
    // and a deduction or tax only when this check or the year has one.
    QVariantList stub;
    const auto line = [&](QVariantList &into, const QString &label, const Money &now, const Money &before, bool always) {
        if (always || !now.isZero() || !before.isZero()) {
            into << QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("currentText"), money.text(now.minor())},
                                {QStringLiteral("ytdText"), money.text((now + before).minor())}};
        }
    };
    const QString afterTaxLabel = PayrollRecords(*c).employee(r->employeeId).deductions.afterTaxLabel;
    line(stub, tr("Gross pay"), t.gross, y.gross, true);
    line(stub, tr("Health insurance (before tax)"), r->section125, y.section125, false);
    line(stub, tr("Retirement plan (before tax)"), r->retirement, y.retirement, false);
    line(stub, tr("Federal income tax"), t.federalIncomeTax, y.federalIncomeTax, true);
    line(stub, tr("Social security"), t.socialSecurity, y.socialSecurity, true);
    line(stub, tr("Medicare"), t.medicare, y.medicare, true);
    line(stub, tr("Additional Medicare"), t.additionalMedicare, y.additionalMedicare, false);
    line(stub, tr("Colorado income tax"), t.coloradoIncomeTax, y.coloradoIncomeTax, true);
    line(stub, tr("Colorado FAMLI"), t.famliEmployee, y.famliEmployee, true);
    line(stub, tr("Local OPT"), t.optEmployee, y.optEmployee, false);
    line(stub, afterTaxLabel.isEmpty() ? tr("Deduction after tax") : afterTaxLabel, r->afterTax, y.afterTax, false);
    line(stub, tr("Net pay"), t.netPay, y.net, true);
    QVariantList employer;
    const Money none(0, money.currency);
    line(employer, tr("Social security"), t.socialSecurityEmployer, none, true);
    line(employer, tr("Medicare"), t.medicareEmployer, none, true);
    line(employer, tr("Federal unemployment (FUTA)"), t.futa, none, true);
    line(employer, tr("Colorado unemployment (UI)"), t.coloradoUi, none, true);
    line(employer, tr("Colorado FAMLI"), t.famliEmployer, none, false);
    line(employer, tr("Local OPT"), t.optEmployer, none, false);
    QVariantList notes;
    for (const QJsonValue &n : r->details.value(QStringLiteral("notes")).toArray()) {
        notes << n.toString();
    }
    return {{QStringLiteral("id"), r->docId},
            {QStringLiteral("employeeId"), r->employeeId},
            {QStringLiteral("employeeName"), r->employeeName},
            {QStringLiteral("payDate"), Format::isoDate(r->payDate)},
            {QStringLiteral("periodStart"), Format::isoDate(r->periodStart)},
            {QStringLiteral("periodEnd"), Format::isoDate(r->periodEnd)},
            {QStringLiteral("number"), r->number},
            // Queued for Checks to Print (M68): it gets its number there.
            {QStringLiteral("toPrint"), Documents(*c).load(r->docId).value_or(BusinessDocument{}).toPrint},
            {QStringLiteral("hours"), r->hours},
            {QStringLiteral("payText"), money.plain(r->regular.minor())},
            {QStringLiteral("bonusText"), r->supplemental.isZero() ? QString() : money.plain(r->supplemental.minor())},
            {QStringLiteral("voided"), r->voided},
            {QStringLiteral("replacesDocId"), r->replacesDocId},
            {QStringLiteral("netText"), money.text(t.netPay.minor())},
            {QStringLiteral("stub"), stub},
            {QStringLiteral("employer"), employer},
            {QStringLiteral("federalWorksheet"), worksheet(r->details.value(QStringLiteral("federal")).toArray())},
            {QStringLiteral("coloradoWorksheet"), worksheet(r->details.value(QStringLiteral("colorado")).toArray())},
            {QStringLiteral("notes"), notes}};
}

QVariantMap PayrollService::voidPaycheck(qint64 id, const QString &reason)
{
    return file() != nullptr ? Format::result(PayrollLedger(*file()).voidPaycheck(id, reason))
                             : Format::failure(tr("No company is open in this window."));
}

QVariantMap PayrollService::adjustPaycheck(qint64 id, const QVariantMap &corrected)
{
    CompanyFile *c = file();
    const std::optional<PaycheckRecord> old = c != nullptr ? PayrollLedger(*c).paycheck(id) : std::nullopt;
    if (!old) {
        return Format::failure(tr("No such paycheck."));
    }
    const MoneyFormat money = m_books->moneyFormat();
    QVariantMap run{{QStringLiteral("payDate"), corrected.value(QStringLiteral("payDate"), Format::isoDate(old->payDate))},
                    {QStringLiteral("periodStart"), Format::isoDate(old->periodStart)},
                    {QStringLiteral("periodEnd"), Format::isoDate(old->periodEnd)},
                    {QStringLiteral("bankAccountId"), old->bankAccountId}};
    QVariantMap row{{QStringLiteral("employeeId"), old->employeeId},
                    {QStringLiteral("hours"), corrected.value(QStringLiteral("hours"), old->hours)},
                    {QStringLiteral("pay"), corrected.value(QStringLiteral("pay"), money.plain(old->regular.minor()))},
                    {QStringLiteral("bonus"), corrected.value(QStringLiteral("bonus"), money.plain(old->supplemental.minor()))},
                    {QStringLiteral("checkNumber"), old->number}};
    PaycheckRequest request;
    QString error;
    if (!requestFrom(row, run, &request, &error)) {
        return Format::failure(error);
    }
    const QString reason = corrected.value(QStringLiteral("reason")).toString().trimmed();
    return Format::result(PayrollLedger(*c).replacePaycheck(id, request, reason.isEmpty() ? tr("Adjusted") : reason));
}

QVariantList PayrollService::liabilities(const QString &fromIso, const QString &toIso, bool openOnly) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = m_books->moneyFormat();
    const QDate today = m_books->todayDate();
    for (const LiabilityDue &d : PayrollLiabilities(*c).liabilities(Format::date(fromIso), Format::date(toIso), openOnly)) {
        out << QVariantMap{{QStringLiteral("key"), d.tax + QLatin1Char('|') + Format::isoDate(d.start)},
                           {QStringLiteral("tax"), d.tax},
                           {QStringLiteral("label"), d.label},
                           {QStringLiteral("agency"), d.agency},
                           {QStringLiteral("period"), d.period},
                           {QStringLiteral("start"), Format::isoDate(d.start)},
                           {QStringLiteral("end"), Format::isoDate(d.end)},
                           {QStringLiteral("due"), Format::isoDate(d.due)},
                           {QStringLiteral("dueText"), d.due.isValid() ? Format::isoDate(d.due) : tr("as billed")},
                           {QStringLiteral("accruedText"), money.text(d.accrued.minor())},
                           {QStringLiteral("paidText"), money.text(d.paid.minor())},
                           {QStringLiteral("balance"), d.balance.minor()},
                           {QStringLiteral("balanceText"), money.text(d.balance.minor())},
                           {QStringLiteral("balancePlain"), money.plain(d.balance.minor())},
                           {QStringLiteral("notes"), d.notes.join(QLatin1Char(' '))},
                           {QStringLiteral("overdue"), d.due.isValid() && d.due < today && d.balance.isPositive()}};
    }
    return out;
}

QVariantMap PayrollService::payLiability(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    const MoneyFormat money = m_books->moneyFormat();
    LiabilityPaymentRequest r;
    r.tax = map.value(QStringLiteral("tax")).toString();
    r.start = Format::date(map.value(QStringLiteral("start")));
    r.end = Format::date(map.value(QStringLiteral("end")));
    r.bankAccountId = Format::id(map.value(QStringLiteral("bankAccountId")));
    r.date = Format::date(map.value(QStringLiteral("date")));
    r.reference = map.value(QStringLiteral("reference")).toString();
    const QString amount = map.value(QStringLiteral("amount")).toString().trimmed();
    if (!amount.isEmpty()) {
        QString error;
        const std::optional<qint64> v = money.parse(amount, &error);
        if (!v) {
            return Format::failure(error);
        }
        r.amount = money.money(*v);
    }
    return Format::result(PayrollLiabilities(*c).pay(r));
}

QVariantList PayrollService::liabilityPayments(int year) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = m_books->moneyFormat();
    for (const LiabilityPaymentRecord &p : PayrollLiabilities(*c).payments(QDate(year, 1, 1), QDate(year + 1, 3, 31), true)) {
        out << QVariantMap{{QStringLiteral("id"), p.docId},
                           {QStringLiteral("label"), PayrollLiabilities::groupLabel(p.tax)},
                           {QStringLiteral("period"), Format::isoDate(p.start) + QStringLiteral(" – ") + Format::isoDate(p.end)},
                           {QStringLiteral("date"), Format::isoDate(p.date)},
                           {QStringLiteral("amountText"), money.text(p.amount.minor())},
                           {QStringLiteral("reference"), p.reference},
                           {QStringLiteral("voided"), p.voided}};
    }
    return out;
}

QVariantMap PayrollService::voidLiabilityPayment(qint64 id, const QString &reason)
{
    return file() != nullptr ? Format::result(PayrollLiabilities(*file()).voidPayment(id, reason))
                             : Format::failure(tr("No company is open in this window."));
}

QVariantMap PayrollService::exportEfw2(int year, const QUrl &fileUrl)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    const Efw2File built = buildEfw2(*c, year);
    if (!built.ok) {
        return Format::failure(built.error);
    }
    QString error;
    if (!writeEfw2(*c, year, path, &error)) {
        return Format::failure(error);
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("path"), path);
    out.insert(QStringLiteral("employees"), built.employees);
    out.insert(QStringLiteral("warnings"), built.warnings);
    return out;
}

QVariantMap PayrollService::exportFormForFiling(const QString &id, const QString &toIso, const QUrl &fileUrl)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return Format::failure(tr("No company is open in this window."));
    }
    static const QStringList filing{QStringLiteral("form_w2"), QStringLiteral("form_w3"), QStringLiteral("ui_wage_report"),
                                    QStringLiteral("famli_wage_report")};
    const QDate to = Format::date(toIso).isValid() ? Format::date(toIso) : m_books->todayDate();
    const std::optional<ReportTable> table = filing.contains(id) ? PayrollReports(*c).build(id, QDate(to.year(), 1, 1), to, true)
                                                                 : std::nullopt;
    if (!table) {
        return Format::failure(tr("Only the W-2, W-3 and wage reports are saved for filing."));
    }
    QString error;
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    if (!ReportPdf::writeReportPdf(*c, *table, path, &error)) {
        return Format::failure(error);
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("path"), path);
    return out;
}

} // namespace QindaOffice::Apps::Books
