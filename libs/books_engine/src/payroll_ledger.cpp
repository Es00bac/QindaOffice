// SPDX-License-Identifier: GPL-3.0-or-later
// Paychecks: compute, post, void, adjust, list (payroll_ledger.h).
#include "payroll_ledger.h"

#include "accounts.h"
#include "company_file.h"
#include "documents.h"
#include "employees.h"
#include "sql_util.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSqlQuery>

namespace QindaOffice::Books {

using namespace Payroll;

namespace {

// The money columns of `paychecks`, in table order after `after_tax`, and
// where each lives in PaycheckTaxes.
struct MoneyColumn {
    const char *name;
    Money PaycheckTaxes::*field;
};
const MoneyColumn kTaxColumns[] = {
    {"federal_wages", &PaycheckTaxes::federalWages}, {"ss_wages", &PaycheckTaxes::socialSecurityWages},
    {"medicare_wages", &PaycheckTaxes::medicareWages}, {"futa_wages", &PaycheckTaxes::futaWages},
    {"co_wages", &PaycheckTaxes::coloradoWages}, {"ui_wages", &PaycheckTaxes::uiWages},
    {"famli_wages", &PaycheckTaxes::famliWages}, {"fit", &PaycheckTaxes::federalIncomeTax},
    {"ss", &PaycheckTaxes::socialSecurity}, {"medicare", &PaycheckTaxes::medicare},
    {"addl_medicare", &PaycheckTaxes::additionalMedicare}, {"co_wh", &PaycheckTaxes::coloradoIncomeTax},
    {"famli_ee", &PaycheckTaxes::famliEmployee}, {"opt_ee", &PaycheckTaxes::optEmployee},
    {"ss_er", &PaycheckTaxes::socialSecurityEmployer}, {"medicare_er", &PaycheckTaxes::medicareEmployer},
    {"futa", &PaycheckTaxes::futa}, {"co_ui", &PaycheckTaxes::coloradoUi},
    {"famli_er", &PaycheckTaxes::famliEmployer}, {"opt_er", &PaycheckTaxes::optEmployer},
    {"net", &PaycheckTaxes::netPay},
};

QJsonArray worksheetJson(const Worksheet &sheet)
{
    QJsonArray out;
    for (const WorksheetLine &l : sheet) {
        out.append(QJsonObject{{QStringLiteral("line"), l.line}, {QStringLiteral("text"), l.text},
                               {QStringLiteral("value"), l.valueText()}});
    }
    return out;
}

QString selectColumns()
{
    QString cols = QStringLiteral("p.doc_id, p.employee_id, p.pay_date, p.period_start, p.period_end, p.tax_year,"
                                  " p.frequency, p.hours, d.number, d.account_id, d.state, p.replaces_doc_id, p.opt_city,"
                                  " p.retirement_kind, p.gross, p.regular, p.supplemental, p.section125, p.retirement,"
                                  " p.shareholder_health, p.after_tax, p.details");
    for (const MoneyColumn &c : kTaxColumns) {
        cols += QStringLiteral(", p.") + QString::fromLatin1(c.name);
    }
    return cols;
}

PaycheckRecord readRecord(const QSqlQuery &q, const QString &cur)
{
    PaycheckRecord r;
    const auto money = [&](int i) { return Money(q.value(i).toLongLong(), cur); };
    r.docId = q.value(0).toLongLong();
    r.employeeId = q.value(1).toLongLong();
    r.payDate = Sql::fromIso(q.value(2));
    r.periodStart = Sql::fromIso(q.value(3));
    r.periodEnd = Sql::fromIso(q.value(4));
    r.taxYear = q.value(5).toInt();
    r.frequency = payFrequencyFromKey(q.value(6).toString()).value_or(PayFrequency::Biweekly);
    r.hours = q.value(7).toString();
    r.number = q.value(8).toString();
    r.bankAccountId = q.value(9).toLongLong();
    r.voided = q.value(10).toInt() == int(DocState::Void);
    r.replacesDocId = q.value(11).toLongLong();
    r.optCity = q.value(12).toString();
    r.retirementKind = PayDeductions::Retirement(q.value(13).toInt());
    r.taxes.gross = money(14);
    r.regular = money(15);
    r.supplemental = money(16);
    r.section125 = money(17);
    r.retirement = money(18);
    r.shareholderHealth = money(19);
    r.afterTax = money(20);
    r.details = QJsonDocument::fromJson(q.value(21).toString().toUtf8()).object();
    int i = 22;
    for (const MoneyColumn &c : kTaxColumns) {
        r.taxes.*c.field = money(i++);
    }
    r.taxes.preTaxDeductions = r.section125 + r.retirement;
    r.taxes.afterTaxDeductions = r.afterTax;
    r.taxes.employeeTaxes = r.taxes.federalIncomeTax + r.taxes.socialSecurity + r.taxes.medicare
        + r.taxes.additionalMedicare + r.taxes.coloradoIncomeTax + r.taxes.famliEmployee + r.taxes.optEmployee;
    r.taxes.employerTaxes = r.taxes.socialSecurityEmployer + r.taxes.medicareEmployer + r.taxes.futa
        + r.taxes.coloradoUi + r.taxes.famliEmployer + r.taxes.optEmployer;
    r.taxes.ok = true;
    return r;
}

} // namespace

PayrollLedger::PayrollLedger(CompanyFile &file)
    : m_file(file)
{
}

std::optional<PayrollTables> PayrollLedger::tables(int year, QString *error)
{
    return PayrollTables::forYear(year, error);
}

PayrollYearToDate PayrollLedger::yearToDate(qint64 employeeId, const QDate &payDate, qint64 beforeDocId) const
{
    PayrollYearToDate y;
    const QString cur = m_file.currency();
    const QString live = QStringLiteral(" FROM paychecks p JOIN documents d ON d.id = p.doc_id WHERE d.state <> %1"
                                        " AND p.employee_id = ? AND p.tax_year = ? AND (p.pay_date < ? OR (p.pay_date = ?"
                                        " AND (? = 0 OR p.doc_id < ?)))")
                             .arg(int(DocState::Void));
    const QString day = Sql::isoDate(payDate);
    QSqlQuery q(m_file.database());
    if (Sql::run(q,
                 QStringLiteral("SELECT SUM(ss_wages), SUM(medicare_wages), SUM(futa_wages), SUM(ui_wages),"
                                " SUM(famli_wages), SUM(supplemental), SUM(retirement), SUM(gross), SUM(federal_wages),"
                                " SUM(fit), SUM(ss), SUM(medicare), SUM(addl_medicare), SUM(co_wh), SUM(famli_ee),"
                                " SUM(opt_ee), SUM(section125), SUM(after_tax), SUM(net)")
                     + live,
                 {employeeId, payDate.year(), day, day, beforeDocId, beforeDocId})
        && q.next()) {
        const auto money = [&](int i) { return Money(q.value(i).toLongLong(), cur); };
        y.ytd.socialSecurityWages = money(0);
        y.ytd.medicareWages = money(1);
        y.ytd.futaWages = money(2);
        y.ytd.uiWages = money(3);
        y.ytd.famliWages = money(4);
        y.ytd.supplemental = money(5);
        y.ytd.retirement = money(6);
        y.gross = money(7);
        y.federalWages = money(8);
        y.federalIncomeTax = money(9);
        y.socialSecurity = money(10);
        y.medicare = money(11);
        y.additionalMedicare = money(12);
        y.coloradoIncomeTax = money(13);
        y.famliEmployee = money(14);
        y.optEmployee = money(15);
        y.section125 = money(16);
        y.retirement = money(6);
        y.afterTax = money(17);
        y.net = money(18);
    }
    q.finish();
    // The calendar month so far, for the local OPT.
    const QString monthStart = Sql::isoDate(QDate(payDate.year(), payDate.month(), 1));
    if (Sql::run(q,
                 QStringLiteral("SELECT SUM(gross), MAX(opt_ee + opt_er)") + live + QStringLiteral(" AND p.pay_date >= ?"),
                 {employeeId, payDate.year(), day, day, beforeDocId, beforeDocId, monthStart})
        && q.next()) {
        y.ytd.monthWages = Money(q.value(0).toLongLong(), cur);
        y.ytd.optChargedThisMonth = q.value(1).toLongLong() > 0;
    }
    return y;
}

EmployerToDate PayrollLedger::employerToDate(const QDate &payDate, qint64 beforeDocId) const
{
    EmployerToDate e;
    const QString cur = m_file.currency();
    const QString day = Sql::isoDate(payDate);
    const QString live = QStringLiteral(" FROM paychecks p JOIN documents d ON d.id = p.doc_id WHERE d.state <> %1"
                                        " AND p.pay_date >= ? AND (p.pay_date < ? OR (p.pay_date = ? AND (? = 0 OR p.doc_id < ?)))")
                             .arg(int(DocState::Void));
    QSqlQuery q(m_file.database());
    const QDate quarter(payDate.year(), (quarterOf(payDate) - 1) * 3 + 1, 1);
    if (Sql::run(q, QStringLiteral("SELECT SUM(ss_wages), SUM(ss_er), SUM(medicare_wages), SUM(medicare_er), SUM(ui_wages),"
                                   " SUM(co_ui)") + live,
                 {Sql::isoDate(quarter), day, day, beforeDocId, beforeDocId})
        && q.next()) {
        e.quarterSocialSecurityWages = Money(q.value(0).toLongLong(), cur);
        e.quarterSocialSecurityEmployer = Money(q.value(1).toLongLong(), cur);
        e.quarterMedicareWages = Money(q.value(2).toLongLong(), cur);
        e.quarterMedicareEmployer = Money(q.value(3).toLongLong(), cur);
        e.quarterUiWages = Money(q.value(4).toLongLong(), cur);
        e.quarterUi = Money(q.value(5).toLongLong(), cur);
    }
    q.finish();
    if (Sql::run(q, QStringLiteral("SELECT SUM(futa_wages), SUM(futa)") + live,
                 {Sql::isoDate(QDate(payDate.year(), 1, 1)), day, day, beforeDocId, beforeDocId})
        && q.next()) {
        e.yearFutaWages = Money(q.value(0).toLongLong(), cur);
        e.yearFuta = Money(q.value(1).toLongLong(), cur);
    }
    return e;
}

PaycheckRecord PayrollLedger::compute(const PaycheckRequest &request, qint64 beforeDocId, QString *error) const
{
    const auto failed = [error](const QString &why) {
        if (error != nullptr) {
            *error = why;
        }
        return PaycheckRecord{};
    };
    if (m_file.currency() != kCurrency) {
        return failed(QStringLiteral("US payroll needs a company kept in US dollars."));
    }
    PayrollRecords records(m_file);
    const EmployeePayroll e = records.employee(request.employeeId);
    if (!e.exists) {
        return failed(QStringLiteral("Set up this employee's payroll first (Payroll ▸ Employees)."));
    }
    if (!request.payDate.isValid()) {
        return failed(QStringLiteral("A paycheck needs a pay date."));
    }
    QString why;
    const std::optional<PayrollTables> tables = PayrollTables::forYear(request.payDate.year(), &why);
    if (!tables) {
        return failed(why);
    }
    const PayrollSetup setup = records.setup();
    // AGENT-GUARD (D-081): a sole proprietor or a partner is not the
    // business's employee (Pub 15 section 2, "Sole proprietor" and
    // "Partners"; Rev. Rul. 69-184): their pay is an owner draw. Only an
    // S corporation — or an owner still "not sure" — pays an owner wages.
    if (e.ownerEmployee && (setup.entity == EntityTax::ScheduleC || setup.entity == EntityTax::Partnership)) {
        return failed(QStringLiteral("%1 owns this %2 LLC, so is not its employee: pay an owner with an owner draw "
                                     "(Payroll ▸ Pay Owner), not a paycheck.")
                          .arg(e.fullName(), setup.entity == EntityTax::ScheduleC ? QStringLiteral("Schedule C")
                                                                                 : QStringLiteral("partnership")));
    }
    PaycheckRecord r;
    r.employeeId = e.employeeId;
    r.employeeName = e.fullName();
    r.payDate = request.payDate;
    r.periodStart = request.periodStart;
    r.periodEnd = request.periodEnd;
    r.taxYear = request.payDate.year();
    r.frequency = e.frequency;
    r.hours = e.payType == EmployeePayroll::PayType::Hourly ? request.hours.value_or(e.defaultHours).toString() : QString();
    r.number = request.toPrint ? QString() : request.checkNumber.trimmed();
    r.bankAccountId = request.bankAccountId != 0 ? request.bankAccountId : setup.payrollBankAccountId;
    r.regular = request.regular ? *request.regular : records.grossPay(e, request.hours);
    r.supplemental = request.bonus;
    const PayDeductions &d = e.deductions;
    r.section125 = d.section125;
    r.retirementKind = d.retirement;
    if (d.retirement != PayDeductions::Retirement::None) {
        r.retirement = d.retirementAmount.isPositive() ? d.retirementAmount
                                                       : (r.regular + r.supplemental).multiplied(d.retirementPercent);
    }
    r.shareholderHealth = e.shareholder2Percent ? d.shareholderHealth : m_file.money(0);
    r.afterTax = d.afterTax;
    r.optCity = e.optCity == QLatin1String("none") ? QString() : (e.optCity.isEmpty() ? setup.optCity : e.optCity);

    PayInput in;
    in.payDate = request.payDate;
    in.frequency = e.frequency;
    in.regular = r.regular;
    in.supplemental = r.supplemental;
    in.section125 = r.section125;
    in.retirement = r.retirement;
    in.simpleIra = d.retirement == PayDeductions::Retirement::SimpleIra;
    in.shareholderHealth = r.shareholderHealth;
    in.afterTax = r.afterTax;
    in.ytd = yearToDate(e.employeeId, request.payDate, beforeDocId).ytd;
    in.employerToDate = employerToDate(request.payDate, beforeDocId);
    EmployerSettings employer;
    if (!setup.uiRate.isEmpty()) {
        employer.uiRate = setup.uiRate;
    } else if (setup.uiHeavyConstruction) {
        employer.uiRate = tables->colorado().uiHeavyConstructionRate;
    }
    employer.famliEmployeeShare = setup.famliEmployeeShare;
    employer.famliEmployerShare = setup.famliEmployerShare;
    employer.roundFederalWithholding = setup.roundFederalWithholding;
    EmployeeTaxSetup tax;
    tax.w4 = e.w4;
    tax.w4OnFile = e.w4OnFile;
    tax.dr0004 = e.dr0004;
    tax.coloradoWithholding = e.coloradoWithholding;
    tax.optCity = r.optCity;
    r.taxes = computePaycheck(*tables, employer, tax, in);
    if (!r.taxes.ok) {
        return failed(r.taxes.error);
    }
    if (!r.taxes.gross.isPositive()) {
        return failed(QStringLiteral("A paycheck needs gross pay above zero."));
    }
    r.details = QJsonObject{{QStringLiteral("federal"), worksheetJson(r.taxes.federalWorksheet)},
                            {QStringLiteral("colorado"), worksheetJson(r.taxes.coloradoWorksheet)},
                            {QStringLiteral("notes"), QJsonArray::fromStringList(r.taxes.notes)},
                            {QStringLiteral("employee"), r.employeeName}};
    if (error != nullptr) {
        error->clear();
    }
    return r;
}

PaycheckRecord PayrollLedger::preview(const PaycheckRequest &request, QString *error) const
{
    return compute(request, 0, error);
}

Result PayrollLedger::post(PaycheckRecord &r, const PaycheckRequest &request, const PostOptions &options)
{
    PayrollRecords records(m_file);
    PayrollSetup setup = records.setup();
    for (const PayrollAccountSpec &spec : payrollAccountRoles()) {
        if (!ChartOfAccounts(m_file).account(setup.account(spec.role))) {
            if (const Result made = records.ensurePayrollAccounts(); !made.ok) {
                return made;
            }
            setup = records.setup();
            break;
        }
    }
    const std::optional<Account> bank = ChartOfAccounts(m_file).account(r.bankAccountId);
    if (!bank || bank->type != AccountType::Bank) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("choose the bank account the paycheck is drawn on"));
    }
    const EmployeePayroll e = records.employee(r.employeeId);
    BusinessDocument doc;
    doc.kind = DocKind::Paycheck;
    doc.state = DocState::Open;
    doc.number = r.number;
    doc.date = r.payDate;
    doc.accountId = r.bankAccountId;
    doc.memo = request.memo.isEmpty() ? QStringLiteral("Paycheck: %1").arg(r.employeeName) : request.memo;
    doc.total = r.taxes.netPay;
    doc.subtotal = r.taxes.gross;
    doc.toPrint = request.toPrint;
    Documents documents(m_file);
    if (const Result w = documents.write(doc); !w.ok) {
        return w;
    }
    r.docId = doc.id;
    QStringList names{QStringLiteral("doc_id"), QStringLiteral("employee_id"), QStringLiteral("pay_date"),
                      QStringLiteral("period_start"), QStringLiteral("period_end"), QStringLiteral("tax_year"),
                      QStringLiteral("frequency"), QStringLiteral("hours"), QStringLiteral("gross"),
                      QStringLiteral("regular"), QStringLiteral("supplemental"), QStringLiteral("section125"),
                      QStringLiteral("retirement"), QStringLiteral("retirement_kind"), QStringLiteral("shareholder_health"),
                      QStringLiteral("after_tax"), QStringLiteral("opt_city"), QStringLiteral("replaces_doc_id"),
                      QStringLiteral("details")};
    QVariantList binds{doc.id, r.employeeId, Sql::isoDate(r.payDate), Sql::isoDate(r.periodStart),
                       Sql::isoDate(r.periodEnd), r.taxYear, payFrequencyKey(r.frequency), r.hours,
                       r.taxes.gross.minor(), r.regular.minor(), r.supplemental.minor(), r.section125.minor(),
                       r.retirement.minor(), int(r.retirementKind), r.shareholderHealth.minor(), r.afterTax.minor(),
                       r.optCity, Sql::idOrNull(r.replacesDocId),
                       QString::fromUtf8(QJsonDocument(r.details).toJson(QJsonDocument::Compact))};
    for (const MoneyColumn &c : kTaxColumns) {
        names << QString::fromLatin1(c.name);
        binds << (r.taxes.*c.field).minor();
    }
    QString error;
    if (!Sql::exec(m_file.database(),
                   QStringLiteral("INSERT INTO paychecks(%1) VALUES(%2)")
                       .arg(names.join(QLatin1Char(',')), QStringList(names.size(), QStringLiteral("?")).join(QLatin1Char(','))),
                   binds, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    const PaycheckTaxes &t = r.taxes;
    const QString memo = doc.memo;
    QList<PostingLine> lines;
    const auto add = [&](const QString &role, qint64 debit, qint64 credit) {
        if (debit != 0 || credit != 0) {
            lines << PostingLine{setup.account(role), debit, credit, 0, 0, 0, 0, 0, memo};
        }
    };
    add(e.ownerEmployee ? PayrollRole::OfficerWages : PayrollRole::Wages, t.gross.minor(), 0);
    add(PayrollRole::EmployerTaxes, t.employerTaxes.minor(), 0);
    add(PayrollRole::FederalWithholding, 0, t.federalIncomeTax.minor());
    add(PayrollRole::SocialSecurity, 0, (t.socialSecurity + t.socialSecurityEmployer).minor());
    add(PayrollRole::Medicare, 0, (t.medicare + t.medicareEmployer + t.additionalMedicare).minor());
    add(PayrollRole::Futa, 0, t.futa.minor());
    add(PayrollRole::ColoradoWithholding, 0, t.coloradoIncomeTax.minor());
    add(PayrollRole::ColoradoUi, 0, t.coloradoUi.minor());
    add(PayrollRole::Famli, 0, (t.famliEmployee + t.famliEmployer).minor());
    add(PayrollRole::LocalOpt, 0, (t.optEmployee + t.optEmployer).minor());
    add(PayrollRole::Section125, 0, r.section125.minor());
    add(PayrollRole::Retirement, 0, r.retirement.minor());
    add(PayrollRole::AfterTax, 0, r.afterTax.minor());
    if (t.netPay.isPositive()) {
        lines << PostingLine{r.bankAccountId, 0, t.netPay.minor(), 0, 0, 0, 0, 0, memo};
    }
    const Result posted = Ledger(m_file).postLines(doc.id, DocKind::Paycheck, r.payDate, r.number, memo, lines, options);
    if (!posted.ok) {
        return posted;
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("paycheck"), doc.id, {},
                 QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("employee"), r.employeeName},
                                                             {QStringLiteral("payDate"), Sql::isoDate(r.payDate)},
                                                             {QStringLiteral("gross"), t.gross.toPlainString()},
                                                             {QStringLiteral("net"), t.netPay.toPlainString()}})
                                       .toJson(QJsonDocument::Compact)));
    return Result::success(doc.id);
}

Result PayrollLedger::createPaycheck(const PaycheckRequest &request, const PostOptions &options)
{
    QList<qint64> ids;
    const Result r = createPaychecks({request}, &ids, options);
    return r.ok ? Result::success(ids.value(0)) : r;
}

Result PayrollLedger::createPaychecks(const QList<PaycheckRequest> &requests, QList<qint64> *docIds,
                                      const PostOptions &options)
{
    if (requests.isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("choose who to pay"));
    }
    SqlTransaction txn(m_file);
    QList<qint64> ids;
    for (const PaycheckRequest &request : requests) {
        QString error;
        PaycheckRecord record = compute(request, 0, &error);
        if (!error.isEmpty()) {
            return Result::failure(ErrorCode::Invalid, error);
        }
        if (const Result posted = post(record, request, options); !posted.ok) {
            return posted;
        }
        ids << record.docId;
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    if (docIds != nullptr) {
        *docIds = ids;
    }
    return Result::success(ids.value(0));
}

Result PayrollLedger::voidPaycheck(qint64 docId, const QString &reason, const PostOptions &options)
{
    const std::optional<BusinessDocument> doc = Documents(m_file).load(docId);
    if (!doc || doc->kind != DocKind::Paycheck) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such paycheck"));
    }
    return Ledger(m_file).voidDocument(docId, reason, options);
}

Result PayrollLedger::replacePaycheck(qint64 docId, const PaycheckRequest &corrected, const QString &reason,
                                      const PostOptions &options)
{
    const std::optional<PaycheckRecord> old = paycheck(docId);
    if (!old || old->voided) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("only a live paycheck can be adjusted"));
    }
    SqlTransaction txn(m_file);
    if (const Result voided = Ledger(m_file).voidDocument(docId, reason, options); !voided.ok) {
        return voided;
    }
    QString error;
    PaycheckRecord record = compute(corrected, 0, &error);
    if (!error.isEmpty()) {
        return Result::failure(ErrorCode::Invalid, error);
    }
    record.replacesDocId = docId;
    if (const Result posted = post(record, corrected, options); !posted.ok) {
        return posted;
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(record.docId);
}

std::optional<PaycheckRecord> PayrollLedger::paycheck(qint64 docId) const
{
    const QList<PaycheckRecord> rows = [&] {
        QList<PaycheckRecord> out;
        QSqlQuery q(m_file.database());
        if (Sql::run(q, QStringLiteral("SELECT ") + selectColumns()
                            + QStringLiteral(" FROM paychecks p JOIN documents d ON d.id = p.doc_id WHERE p.doc_id = ?"),
                     {docId})) {
            while (q.next()) {
                out << readRecord(q, m_file.currency());
            }
        }
        return out;
    }();
    if (rows.isEmpty()) {
        return std::nullopt;
    }
    PaycheckRecord r = rows.first();
    r.employeeName = PayrollRecords(m_file).employee(r.employeeId).fullName();
    return r;
}

QList<PaycheckRecord> PayrollLedger::paychecks(const QDate &from, const QDate &to, qint64 employeeId,
                                               bool includeVoid) const
{
    QList<PaycheckRecord> out;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q,
                 QStringLiteral("SELECT ") + selectColumns()
                     + QStringLiteral(" FROM paychecks p JOIN documents d ON d.id = p.doc_id WHERE (? = '' OR p.pay_date >= ?)"
                                      " AND (? = '' OR p.pay_date <= ?) AND (? = 0 OR p.employee_id = ?)"
                                      " AND (? = 1 OR d.state <> ?) ORDER BY p.pay_date, p.doc_id"),
                 {Sql::isoDate(from), Sql::isoDate(from), Sql::isoDate(to), Sql::isoDate(to), employeeId, employeeId,
                  includeVoid ? 1 : 0, int(DocState::Void)});
        while (q.next()) {
            out << readRecord(q, m_file.currency());
        }
    }
    PayrollRecords records(m_file);
    QHash<qint64, QString> names;
    for (PaycheckRecord &r : out) {
        if (!names.contains(r.employeeId)) {
            names.insert(r.employeeId, records.employee(r.employeeId).fullName());
        }
        r.employeeName = names.value(r.employeeId);
    }
    return out;
}

} // namespace QindaOffice::Books
