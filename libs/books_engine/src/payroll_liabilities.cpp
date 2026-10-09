// SPDX-License-Identifier: GPL-3.0-or-later
// Payroll liabilities by group and period; paying and voiding them
// (payroll_liabilities.h).
#include "payroll_liabilities.h"

#include "accounts.h"
#include "company_file.h"
#include "documents.h"
#include "payroll_items.h"
#include "payroll_ledger.h"
#include "payroll_records.h"
#include "sql_util.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

#include <algorithm>

namespace QindaOffice::Books {

using namespace Payroll;

namespace {

struct Share {
    QString tax;
    QString role;
    Money amount;
};

// What one paycheck owes, group by group (payroll_ledger.h's posting).
QList<Share> sharesOf(const PaycheckRecord &r)
{
    const PaycheckTaxes &t = r.taxes;
    QList<Share> out{{QStringLiteral("federal941"), PayrollRole::FederalWithholding, t.federalIncomeTax},
                     {QStringLiteral("federal941"), PayrollRole::SocialSecurity, t.socialSecurity + t.socialSecurityEmployer},
                     {QStringLiteral("federal941"), PayrollRole::Medicare, t.medicare + t.medicareEmployer + t.additionalMedicare},
                     {QStringLiteral("futa"), PayrollRole::Futa, t.futa},
                     {QStringLiteral("coWithholding"), PayrollRole::ColoradoWithholding, t.coloradoIncomeTax},
                     {QStringLiteral("coUi"), PayrollRole::ColoradoUi, t.coloradoUi},
                     {QStringLiteral("coFamli"), PayrollRole::Famli, t.famliEmployee + t.famliEmployer},
                     {QStringLiteral("section125"), PayrollRole::Section125, r.section125},
                     {QStringLiteral("retirement"), PayrollRole::Retirement, r.retirement},
                     {QStringLiteral("afterTax"), PayrollRole::AfterTax, r.afterTax}};
    if (!r.optCity.isEmpty()) {
        out << Share{QStringLiteral("opt:") + r.optCity, PayrollRole::LocalOpt, t.optEmployee + t.optEmployer};
    }
    // The company's own payroll tax items: one group each, its own account.
    for (const CustomItemResult &c : t.custom) {
        out << Share{QStringLiteral("custom:%1").arg(c.id), QStringLiteral("custom:%1").arg(c.id), c.amount};
    }
    return out;
}

// The payroll tax item a "custom:<id>" group key names.
qint64 customId(const QString &tax)
{
    return tax.startsWith(QLatin1String("custom:")) ? tax.mid(7).toLongLong() : 0;
}

QDate lastDay(int year, int month)
{
    const QDate first(year, month, 1);
    return first.addDays(first.daysInMonth() - 1);
}

DuePeriod optPeriod(const QDate &payDate, const QString &filing, const OccupationalPrivilegeTax *city,
                    const BusinessCalendar &calendar)
{
    DuePeriod p = filing == QLatin1String("monthly") ? monthlyReturnPeriod(payDate.year(), payDate.month(), calendar)
                                                    : quarterlyReturnPeriod(payDate.year(), quarterOf(payDate), calendar);
    if (city != nullptr && city->dueDay > 0) {
        const QDate after = p.end.addDays(1);
        p.statutory = QDate(after.year(), after.month(), qMin(city->dueDay, after.daysInMonth()));
        p.due = calendar.onOrAfter(p.statutory);
    }
    return p;
}

DuePeriod periodFor(const QString &tax, const QDate &payDate, const PayrollSetup &setup, const PayrollTables &tables,
                    const QMap<qint64, PayrollItem> &items)
{
    const BusinessCalendar federal(tables.federalHolidays());
    const BusinessCalendar colorado(tables.coloradoHolidays());
    if (const qint64 id = customId(tax); id != 0 && items.contains(id)) {
        // An agency QindaBooks knows nothing about: business days by the
        // federal calendar.
        return PayrollItems::periodFor(items.value(id), payDate, federal);
    }
    if (tax == QLatin1String("federal941")) {
        return federalDepositPeriod(payDate, setup.depositSchedule, federal);
    }
    if (tax == QLatin1String("futa")) {
        return quarterlyReturnPeriod(payDate.year(), quarterOf(payDate), federal);
    }
    if (tax == QLatin1String("coWithholding")) {
        return coloradoWithholdingPeriod(payDate, setup.coloradoFrequency, colorado);
    }
    if (tax == QLatin1String("coUi") || tax == QLatin1String("coFamli")) {
        return quarterlyReturnPeriod(payDate.year(), quarterOf(payDate), colorado);
    }
    if (tax.startsWith(QLatin1String("opt:"))) {
        return optPeriod(payDate, setup.optFiling, tables.opt(tax.mid(4)), colorado);
    }
    DuePeriod month; // deductions: by calendar month, paid as billed
    month.start = QDate(payDate.year(), payDate.month(), 1);
    month.end = lastDay(payDate.year(), payDate.month());
    month.label = QLocale(QLocale::English).toString(month.start, QStringLiteral("MMMM yyyy"));
    return month;
}

QString rowKey(const QString &tax, const QDate &start, const QDate &end)
{
    return tax + QLatin1Char('|') + Sql::isoDate(start) + QLatin1Char('|') + Sql::isoDate(end);
}

} // namespace

PayrollLiabilities::PayrollLiabilities(CompanyFile &file)
    : m_file(file)
{
}

QString PayrollLiabilities::groupLabel(const QString &tax)
{
    if (tax == QLatin1String("federal941")) return QStringLiteral("Federal income tax, social security and Medicare (Form 941)");
    if (tax == QLatin1String("futa")) return QStringLiteral("Federal unemployment tax (FUTA, Form 940)");
    if (tax == QLatin1String("coWithholding")) return QStringLiteral("Colorado wage withholding (DR 1094)");
    if (tax == QLatin1String("coUi")) return QStringLiteral("Colorado unemployment insurance premium");
    if (tax == QLatin1String("coFamli")) return QStringLiteral("Colorado FAMLI premium");
    if (tax == QLatin1String("section125")) return QStringLiteral("Health insurance deductions");
    if (tax == QLatin1String("retirement")) return QStringLiteral("Retirement plan deductions");
    if (tax == QLatin1String("afterTax")) return QStringLiteral("Other payroll deductions");
    if (tax.startsWith(QLatin1String("opt:"))) {
        QString city = tax.mid(4);
        QList<int> years = PayrollTables::availableYears();
        std::reverse(years.begin(), years.end());
        for (const int year : std::as_const(years)) {
            if (const std::optional<PayrollTables> t = PayrollTables::forYear(year); t && t->opt(city)) {
                city = t->opt(city)->city;
                break;
            }
        }
        return QStringLiteral("%1 Occupational Privilege Tax").arg(city);
    }
    return tax;
}

QString PayrollLiabilities::labelOf(const QString &tax) const
{
    if (const qint64 id = customId(tax); id != 0) {
        const std::optional<PayrollItem> item = PayrollItems(m_file).item(id);
        return item ? item->name : tax;
    }
    return groupLabel(tax);
}

QString PayrollLiabilities::groupAgency(const QString &tax)
{
    if (tax == QLatin1String("federal941") || tax == QLatin1String("futa")) return QStringLiteral("IRS — EFTPS");
    if (tax == QLatin1String("coWithholding")) return QStringLiteral("Colorado Department of Revenue — Revenue Online");
    if (tax == QLatin1String("coUi")) return QStringLiteral("CDLE — MyUI Employer+");
    if (tax == QLatin1String("coFamli")) return QStringLiteral("FAMLI Division — My FAMLI+ Employer");
    if (tax.startsWith(QLatin1String("opt:"))) return QStringLiteral("the city");
    return QStringLiteral("as the insurer or plan bills");
}

QList<LiabilityDue> PayrollLiabilities::liabilities(const QDate &from, const QDate &to, bool openOnly) const
{
    const PayrollSetup setup = PayrollRecords(m_file).setup();
    QMap<qint64, PayrollItem> items;
    for (const PayrollItem &item : PayrollItems(m_file).items(true)) {
        items.insert(item.id, item);
    }
    const QString cur = m_file.currency();
    QMap<QString, LiabilityDue> rows;
    QMap<QString, QList<std::pair<QDate, Money>>> federalByDay; // the $100,000 rule
    for (const PaycheckRecord &r : PayrollLedger(m_file).paychecks(from, to)) {
        const std::optional<PayrollTables> tables = PayrollTables::forYear(r.taxYear);
        if (!tables) {
            continue;
        }
        for (const Share &s : sharesOf(r)) {
            if (!s.amount.isPositive()) {
                continue;
            }
            const DuePeriod p = periodFor(s.tax, r.payDate, setup, *tables, items);
            const QString key = rowKey(s.tax, p.start, p.end);
            LiabilityDue &row = rows[key];
            const qint64 custom = customId(s.tax);
            if (row.tax.isEmpty()) {
                row = LiabilityDue{s.tax, custom != 0 ? items.value(custom).name : groupLabel(s.tax),
                                   custom != 0 ? items.value(custom).agency : groupAgency(s.tax), p.start, p.end, p.due,
                                   p.label, Money(0, cur), Money(0, cur), Money(0, cur), {}, {}};
            }
            auto part = std::find_if(row.parts.begin(), row.parts.end(), [&](const LiabilityPart &x) { return x.role == s.role; });
            if (part == row.parts.end()) {
                row.parts << LiabilityPart{s.role, custom != 0 ? items.value(custom).accountId : setup.account(s.role),
                                           Money(0, cur), Money(0, cur), Money(0, cur)};
                part = row.parts.end() - 1;
            }
            part->accrued += s.amount;
            row.accrued += s.amount;
            if (s.tax == QLatin1String("federal941")) {
                federalByDay[key] << std::pair{r.payDate, s.amount};
            }
        }
    }
    // What was paid, per group, period and account.
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT pp.tax, pp.period_start, pp.period_end, jl.account_id,"
                                   " SUM(jl.debit) - SUM(jl.credit) FROM payroll_payments pp"
                                   " JOIN documents d ON d.id = pp.doc_id JOIN journal_lines jl ON jl.txn_id = d.txn_id"
                                   " WHERE d.state <> ? AND jl.voided = 0"
                                   " GROUP BY pp.tax, pp.period_start, pp.period_end, jl.account_id"),
                 {int(DocState::Void)});
        while (q.next()) {
            const QString key = rowKey(q.value(0).toString(), Sql::fromIso(q.value(1)), Sql::fromIso(q.value(2)));
            if (!rows.contains(key)) {
                continue; // a period outside the range asked for
            }
            LiabilityDue &row = rows[key];
            for (LiabilityPart &part : row.parts) {
                if (part.accountId == q.value(3).toLongLong()) {
                    part.paid += Money(q.value(4).toLongLong(), cur);
                    row.paid += Money(q.value(4).toLongLong(), cur);
                }
            }
        }
    }
    QList<LiabilityDue> out;
    for (auto it = rows.begin(); it != rows.end(); ++it) {
        LiabilityDue &row = it.value();
        for (LiabilityPart &part : row.parts) {
            part.balance = part.accrued - part.paid;
        }
        row.balance = row.accrued - row.paid;
        out << row;
    }
    // The $100,000 next-day rule and the $2,500 quarter (Pub 15 §11), and
    // FUTA's $500 carry — each year by its own tables.
    std::sort(out.begin(), out.end(), [](const LiabilityDue &a, const LiabilityDue &b) { return a.start < b.start; });
    QMap<QString, Money> quarterTotals;
    for (const LiabilityDue &row : std::as_const(out)) {
        if (row.tax == QLatin1String("federal941")) {
            quarterTotals[quarterLabel(row.start.year(), quarterOf(row.start))] += row.accrued;
        }
    }
    QMap<int, QList<LiabilityDue *>> futaPending;
    QMap<int, Money> futaUndeposited;
    for (LiabilityDue &row : out) {
        const std::optional<PayrollTables> tables = PayrollTables::forYear(row.start.year());
        if (!tables) {
            continue;
        }
        const BusinessCalendar federal(tables->federalHolidays());
        if (row.tax == QLatin1String("federal941")) {
            QList<std::pair<QDate, Money>> days = federalByDay.value(rowKey(row.tax, row.start, row.end));
            std::sort(days.begin(), days.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
            Money running(0, cur);
            for (const auto &[day, amount] : days) {
                running += amount;
                if (running >= tables->deposits().nextDayThreshold) {
                    row.due = qMin(row.due, nextDayDepositDue(day, federal));
                    row.notes << QStringLiteral("%1 or more accumulated on %2: deposit by the next business day, and "
                                                "the business is a semiweekly depositor from then on.")
                                     .arg(usdText(tables->deposits().nextDayThreshold), Sql::isoDate(day));
                    break;
                }
            }
            const int q = quarterOf(row.start);
            if (row.notes.isEmpty()
                && quarterTotals.value(quarterLabel(row.start.year(), q)) < tables->deposits().quarterlyDeMinimis) {
                row.notes << QStringLiteral("The quarter's total is under %1: it may be paid with Form 941 (due %2) "
                                            "instead of depositing.")
                                 .arg(usdText(tables->deposits().quarterlyDeMinimis),
                                      Sql::isoDate(quarterlyReturnPeriod(row.start.year(), q, federal).due));
            }
        } else if (row.tax == QLatin1String("futa")) {
            // Deposit once the undeposited tax is more than $500; until
            // then it carries to the next quarter, the year's rest goes
            // with Form 940.
            const int year = row.start.year();
            futaPending[year] << &row;
            futaUndeposited[year] = futaUndeposited.value(year, Money(0, cur)) + row.balance;
            if (futaUndeposited[year] > tables->futa().depositThreshold) {
                for (LiabilityDue *carried : std::as_const(futaPending[year])) {
                    if (carried != &row) {
                        carried->due = row.due;
                        carried->notes << QStringLiteral("Carried to %1: deposit it with that quarter's FUTA.").arg(row.period);
                    }
                }
                futaPending[year].clear();
                futaUndeposited[year] = Money(0, cur);
            }
        }
    }
    for (auto it = futaPending.begin(); it != futaPending.end(); ++it) {
        const std::optional<PayrollTables> tables = PayrollTables::forYear(it.key());
        for (LiabilityDue *carried : std::as_const(it.value())) {
            const DuePeriod annual = annualReturnPeriod(it.key(), BusinessCalendar(tables ? tables->federalHolidays() : QList<QDate>()));
            carried->due = annual.due;
            carried->notes << QStringLiteral("%1 or less undeposited: pay it with Form 940 by %2, or deposit it.")
                                  .arg(tables ? usdText(tables->futa().depositThreshold) : QString(),
                                       Sql::isoDate(annual.due));
        }
    }
    if (openOnly) {
        out.erase(std::remove_if(out.begin(), out.end(), [](const LiabilityDue &r) { return r.balance.isZero(); }), out.end());
    }
    std::stable_sort(out.begin(), out.end(), [](const LiabilityDue &a, const LiabilityDue &b) {
        const QDate da = a.due.isValid() ? a.due : a.end;
        const QDate db = b.due.isValid() ? b.due : b.end;
        return da != db ? da < db : a.tax < b.tax;
    });
    return out;
}

std::optional<LiabilityDue> PayrollLiabilities::liability(const QString &tax, const QDate &start, const QDate &end) const
{
    for (const LiabilityDue &row : liabilities(QDate(start.year(), 1, 1), QDate(end.year(), 12, 31))) {
        if (row.tax == tax && row.start == start && row.end == end) {
            return row;
        }
    }
    return std::nullopt;
}

Result PayrollLiabilities::pay(const LiabilityPaymentRequest &request, const PostOptions &options)
{
    const std::optional<LiabilityDue> row = liability(request.tax, request.start, request.end);
    if (!row || row->accrued.isZero()) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("nothing is owed for that tax and period"));
    }
    const QString cur = m_file.currency();
    const Money amount = request.amount.value_or(row->balance);
    if (!amount.isPositive() || !request.date.isValid()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a payment needs a date and an amount above zero"));
    }
    const Money tolerance(100, cur);
    const Money diff = amount - row->balance;
    QList<Money> parts;
    Money adjustment(0, cur);
    if (diff.isZero() || (diff.abs() <= tolerance && row->balance.isPositive())) {
        for (const LiabilityPart &p : row->parts) {
            parts << p.balance;
        }
        adjustment = diff;
    } else if (amount < row->balance) {
        QList<qint64> weights;
        for (const LiabilityPart &p : row->parts) {
            weights << qMax<qint64>(0, p.balance.minor());
        }
        parts = amount.allocate(weights);
    } else {
        return Result::failure(ErrorCode::OverApplied, QStringLiteral("%1 is more than the %2 owed")
                                                           .arg(usdText(amount), usdText(row->balance)));
    }
    const PayrollSetup setup = PayrollRecords(m_file).setup();
    const qint64 bankId = request.bankAccountId != 0 ? request.bankAccountId : setup.payrollBankAccountId;
    const std::optional<Account> bank = ChartOfAccounts(m_file).account(bankId);
    if (!bank || bank->type != AccountType::Bank) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("choose the bank account the payment comes from"));
    }
    SqlTransaction txn(m_file);
    BusinessDocument doc;
    doc.kind = DocKind::PayrollLiabilityPayment;
    doc.state = DocState::Open;
    doc.date = request.date;
    doc.accountId = bankId;
    doc.reference = request.reference.trimmed();
    doc.memo = request.memo.isEmpty() ? QStringLiteral("%1, %2").arg(row->label, row->period) : request.memo;
    doc.total = amount;
    doc.subtotal = amount;
    if (const Result w = Documents(m_file).write(doc); !w.ok) {
        return w;
    }
    QList<PostingLine> lines;
    for (qsizetype i = 0; i < row->parts.size(); ++i) {
        if (parts.value(i).isPositive()) {
            lines << PostingLine{row->parts.at(i).accountId, parts.at(i).minor(), 0, 0, 0, 0, 0, 0, doc.memo};
        } else if (parts.value(i).isNegative()) {
            lines << PostingLine{row->parts.at(i).accountId, 0, -parts.at(i).minor(), 0, 0, 0, 0, 0, doc.memo};
        }
    }
    if (!adjustment.isZero()) {
        const QString note = QStringLiteral("Rounding difference: ") + doc.memo;
        lines << PostingLine{setup.account(PayrollRole::EmployerTaxes), qMax<qint64>(0, adjustment.minor()),
                             qMax<qint64>(0, -adjustment.minor()), 0, 0, 0, 0, 0, note};
    }
    lines << PostingLine{bankId, 0, amount.minor(), 0, 0, 0, 0, 0, doc.memo};
    const Result posted = Ledger(m_file).postLines(doc.id, doc.kind, doc.date, doc.reference, doc.memo, lines, options);
    if (!posted.ok) {
        return posted;
    }
    QString error;
    if (!Sql::exec(m_file.database(),
                   QStringLiteral("INSERT INTO payroll_payments(doc_id, tax, period_start, period_end, adjustment)"
                                  " VALUES(?, ?, ?, ?, ?)"),
                   {doc.id, request.tax, Sql::isoDate(request.start), Sql::isoDate(request.end), adjustment.minor()}, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("payroll-payment"), doc.id, {},
                 QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("tax"), request.tax},
                                                             {QStringLiteral("period"), row->period},
                                                             {QStringLiteral("amount"), amount.toPlainString()}})
                                       .toJson(QJsonDocument::Compact)));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(doc.id);
}

Result PayrollLiabilities::voidPayment(qint64 docId, const QString &reason, const PostOptions &options)
{
    const std::optional<BusinessDocument> doc = Documents(m_file).load(docId);
    if (!doc || doc->kind != DocKind::PayrollLiabilityPayment) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such payroll liability payment"));
    }
    return Ledger(m_file).voidDocument(docId, reason, options);
}

QList<LiabilityPaymentRecord> PayrollLiabilities::payments(const QDate &from, const QDate &to, bool includeVoid) const
{
    QList<LiabilityPaymentRecord> out;
    QSqlQuery q(m_file.database());
    Sql::run(q,
             QStringLiteral("SELECT pp.doc_id, pp.tax, pp.period_start, pp.period_end, d.date, d.total, pp.adjustment,"
                            " d.reference, d.state FROM payroll_payments pp JOIN documents d ON d.id = pp.doc_id"
                            " WHERE (? = '' OR d.date >= ?) AND (? = '' OR d.date <= ?) AND (? = 1 OR d.state <> ?)"
                            " ORDER BY d.date, pp.doc_id"),
             {Sql::isoDate(from), Sql::isoDate(from), Sql::isoDate(to), Sql::isoDate(to), includeVoid ? 1 : 0,
              int(DocState::Void)});
    while (q.next()) {
        out << LiabilityPaymentRecord{q.value(0).toLongLong(), q.value(1).toString(), Sql::fromIso(q.value(2)),
                                      Sql::fromIso(q.value(3)), Sql::fromIso(q.value(4)),
                                      Money(q.value(5).toLongLong(), m_file.currency()),
                                      Money(q.value(6).toLongLong(), m_file.currency()), q.value(7).toString(),
                                      q.value(8).toInt() == int(DocState::Void)};
    }
    return out;
}

} // namespace QindaOffice::Books
