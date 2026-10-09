// SPDX-License-Identifier: GPL-3.0-or-later
// A company's own payroll tax items (payroll_items.h).
#include "payroll_items.h"

#include "accounts.h"
#include "company_file.h"
#include "payroll_ledger.h"
#include "payroll_records.h"
#include "sql_util.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

namespace QindaOffice::Books {

using namespace Payroll;

namespace {

const QStringList kFrequencyKeys{QStringLiteral("monthly"), QStringLiteral("quarterly"), QStringLiteral("annually")};

const QString kColumns = QStringLiteral("id, name, side, kind, rate, wage_base, annual_limit, account_id, agency,"
                                        " frequency, due_day, active");

PayrollItem readItem(const QSqlQuery &q, const QString &cur)
{
    PayrollItem i;
    i.id = q.value(0).toLongLong();
    i.name = q.value(1).toString();
    i.side = customSideFromKey(q.value(2).toString()).value_or(CustomItem::Side::Employer);
    i.kind = customKindFromKey(q.value(3).toString()).value_or(CustomItem::Kind::Tax);
    i.rate = Decimal::parse(q.value(4).toString()).value_or(Decimal());
    if (!q.value(5).isNull()) {
        i.wageBase = Money(q.value(5).toLongLong(), cur);
    }
    if (!q.value(6).isNull()) {
        i.annualLimit = Money(q.value(6).toLongLong(), cur);
    }
    i.accountId = q.value(7).toLongLong();
    i.agency = q.value(8).toString();
    i.frequency = payrollItemFrequencyFromKey(q.value(9).toString()).value_or(PayrollItem::Frequency::Quarterly);
    i.dueDay = q.value(10).toInt();
    i.active = q.value(11).toInt() != 0;
    return i;
}

QString auditJson(const PayrollItem &i)
{
    return QString::fromUtf8(
        QJsonDocument(QJsonObject{{QStringLiteral("name"), i.name},
                                  {QStringLiteral("side"), customSideKey(i.side)},
                                  {QStringLiteral("kind"), customKindKey(i.kind)},
                                  {QStringLiteral("rate"), i.rate.toString()},
                                  {QStringLiteral("wageBase"), i.wageBase ? i.wageBase->toPlainString() : QString()},
                                  {QStringLiteral("annualLimit"), i.annualLimit ? i.annualLimit->toPlainString() : QString()},
                                  {QStringLiteral("account"), qint64(i.accountId)},
                                  {QStringLiteral("agency"), i.agency},
                                  {QStringLiteral("frequency"), payrollItemFrequencyKey(i.frequency)},
                                  {QStringLiteral("dueDay"), i.dueDay},
                                  {QStringLiteral("active"), i.active}})
            .toJson(QJsonDocument::Compact));
}

// "Payroll Liabilities" (a top-level current liability), found or made.
qint64 liabilitiesParent(CompanyFile &file)
{
    ChartOfAccounts chart(file);
    for (const Account &a : chart.accountsOfType(AccountType::OtherCurrentLiability, true)) {
        if (a.parentId == 0 && a.name.compare(QStringLiteral("Payroll Liabilities"), Qt::CaseInsensitive) == 0) {
            return a.id;
        }
    }
    Account a;
    a.name = QStringLiteral("Payroll Liabilities");
    a.type = AccountType::OtherCurrentLiability;
    const Result made = chart.create(a);
    return made.ok ? made.id : 0;
}

bool positive(const Decimal &d)
{
    return !d.isZero() && !d.isNegative();
}

} // namespace

CustomItem PayrollItem::computation() const
{
    return CustomItem{id, name, side, kind, rate, wageBase, annualLimit};
}

QString payrollItemFrequencyKey(PayrollItem::Frequency frequency)
{
    return kFrequencyKeys.value(int(frequency));
}

std::optional<PayrollItem::Frequency> payrollItemFrequencyFromKey(const QString &key)
{
    const qsizetype i = kFrequencyKeys.indexOf(key);
    return i < 0 ? std::nullopt : std::optional<PayrollItem::Frequency>(PayrollItem::Frequency(i));
}

PayrollItems::PayrollItems(CompanyFile &file)
    : m_file(file)
{
}

QList<PayrollItem> PayrollItems::items(bool includeInactive) const
{
    QList<PayrollItem> out;
    QSqlQuery q(m_file.database());
    if (Sql::run(q, QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM payroll_items WHERE (? = 1 OR active = 1)"
                                                                          " ORDER BY name COLLATE NOCASE"),
                 {includeInactive ? 1 : 0})) {
        while (q.next()) {
            out << readItem(q, m_file.currency());
        }
    }
    return out;
}

std::optional<PayrollItem> PayrollItems::item(qint64 id) const
{
    QSqlQuery q(m_file.database());
    if (Sql::run(q, QStringLiteral("SELECT ") + kColumns + QStringLiteral(" FROM payroll_items WHERE id = ?"), {id})
        && q.next()) {
        return readItem(q, m_file.currency());
    }
    return std::nullopt;
}

Result PayrollItems::saveItem(const PayrollItem &input)
{
    PayrollItem i = input;
    i.name = i.name.trimmed();
    i.agency = i.agency.trimmed();
    const auto invalid = [](const QString &why) { return Result::failure(ErrorCode::Invalid, why); };
    if (i.name.isEmpty()) {
        return invalid(QStringLiteral("a payroll tax item needs a name"));
    }
    if (i.kind == CustomItem::Kind::Withholding) {
        i.side = CustomItem::Side::Employee; // withheld from pay; each employee carries the rate
        i.rate = Decimal();
    } else if (!positive(i.rate) || Decimal::compare(i.rate, Decimal(1, 0)) > 0) {
        return invalid(QStringLiteral("a payroll tax item's rate is above 0% and at most 100%"));
    }
    if ((i.wageBase && !i.wageBase->isPositive()) || (i.annualLimit && !i.annualLimit->isPositive())) {
        return invalid(QStringLiteral("a wage base or an annual limit is above zero (leave it empty for none)"));
    }
    if (i.dueDay < 0 || i.dueDay > 31) {
        return invalid(QStringLiteral("the due day is 1 to 31, or 0 for the last day of the month"));
    }
    const std::optional<PayrollItem> before = i.id != 0 ? item(i.id) : std::nullopt;
    if (i.id != 0 && !before) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such payroll tax item"));
    }
    if (Sql::scalarInt(m_file.database(),
                       QStringLiteral("SELECT COUNT(*) FROM payroll_items WHERE name = ? COLLATE NOCASE AND id <> ?"),
                       {i.name, i.id})
        > 0) {
        return Result::failure(ErrorCode::Duplicate, QStringLiteral("another payroll tax item is named %1").arg(i.name));
    }
    SqlTransaction txn(m_file);
    ChartOfAccounts chart(m_file);
    if (i.accountId == 0) {
        i.accountId = before ? before->accountId : 0;
    }
    if (i.accountId == 0) {
        const qint64 parent = liabilitiesParent(m_file);
        for (const Account &a : chart.accountsOfType(AccountType::OtherCurrentLiability, true)) {
            if (a.parentId == parent && a.name.compare(i.name, Qt::CaseInsensitive) == 0) {
                i.accountId = a.id;
            }
        }
        if (i.accountId == 0) {
            Account a;
            a.name = i.name;
            a.type = AccountType::OtherCurrentLiability;
            a.parentId = parent;
            const Result made = chart.create(a);
            if (!made.ok) {
                return made;
            }
            i.accountId = made.id;
        }
    }
    const std::optional<Account> account = chart.account(i.accountId);
    if (!account || account->type != AccountType::OtherCurrentLiability) {
        return Result::failure(ErrorCode::UnknownAccount, QStringLiteral("a payroll tax item is owed through a current "
                                                                         "liability account"));
    }
    if (before && before->accountId != i.accountId
        && Sql::scalarInt(m_file.database(), QStringLiteral("SELECT COUNT(*) FROM paycheck_items WHERE item_id = ?"), {i.id})
            > 0) {
        return invalid(QStringLiteral("paychecks already owe %1 through its account; it cannot move").arg(i.name));
    }
    const QVariantList values{i.name, customSideKey(i.side), customKindKey(i.kind), i.rate.toString(),
                              i.wageBase ? QVariant(i.wageBase->minor()) : QVariant(),
                              i.annualLimit ? QVariant(i.annualLimit->minor()) : QVariant(), i.accountId, i.agency,
                              payrollItemFrequencyKey(i.frequency), i.dueDay, i.active ? 1 : 0};
    QString error;
    if (before) {
        if (!Sql::exec(m_file.database(),
                       QStringLiteral("UPDATE payroll_items SET name = ?, side = ?, kind = ?, rate = ?, wage_base = ?,"
                                      " annual_limit = ?, account_id = ?, agency = ?, frequency = ?, due_day = ?,"
                                      " active = ? WHERE id = ?"),
                       values + QVariantList{i.id}, &error)) {
            return Result::failure(ErrorCode::Storage, error);
        }
    } else {
        QSqlQuery q(m_file.database());
        if (!Sql::run(q,
                      QStringLiteral("INSERT INTO payroll_items(name, side, kind, rate, wage_base, annual_limit, account_id,"
                                     " agency, frequency, due_day, active, created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"),
                      values + QVariantList{CompanyFile::nowStamp()}, &error)) {
            return Result::failure(ErrorCode::Storage, error);
        }
        i.id = q.lastInsertId().toLongLong();
    }
    m_file.audit(before ? QStringLiteral("edit") : QStringLiteral("create"), QStringLiteral("payroll-item"), i.id,
                 before ? auditJson(*before) : QString(), auditJson(i));
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(i.id);
}

Result PayrollItems::setActive(qint64 id, bool active)
{
    std::optional<PayrollItem> i = item(id);
    if (!i) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("no such payroll tax item"));
    }
    i->active = active;
    return saveItem(*i);
}

QMap<qint64, PayrollItemToDate> PayrollItems::yearToDate(qint64 employeeId, const QDate &payDate, qint64 beforeDocId) const
{
    QMap<qint64, PayrollItemToDate> out;
    const QString cur = m_file.currency();
    const QString day = Sql::isoDate(payDate);
    QSqlQuery q(m_file.database());
    if (Sql::run(q,
                 QStringLiteral("SELECT pi.item_id, SUM(pi.wages), SUM(pi.amount) FROM paycheck_items pi"
                                " JOIN paychecks p ON p.doc_id = pi.doc_id JOIN documents d ON d.id = p.doc_id"
                                " WHERE d.state <> ? AND p.employee_id = ? AND p.tax_year = ? AND (p.pay_date < ?"
                                " OR (p.pay_date = ? AND (? = 0 OR p.doc_id < ?))) GROUP BY pi.item_id"),
                 {int(DocState::Void), employeeId, payDate.year(), day, day, beforeDocId, beforeDocId})) {
        while (q.next()) {
            out.insert(q.value(0).toLongLong(),
                       PayrollItemToDate{Money(q.value(1).toLongLong(), cur), Money(q.value(2).toLongLong(), cur)});
        }
    }
    return out;
}

QList<CustomItemInput> PayrollItems::inputsFor(const EmployeePayroll &employee, const QDate &payDate,
                                               qint64 beforeDocId) const
{
    QList<CustomItemInput> out;
    const QMap<qint64, PayrollItemToDate> ytd = yearToDate(employee.employeeId, payDate, beforeDocId);
    const Money zero = m_file.money(0);
    for (const PayrollItem &i : items()) {
        if (employee.exemptItems.contains(i.id)) {
            continue;
        }
        CustomItemInput in;
        in.item = i.computation();
        if (i.kind == CustomItem::Kind::Withholding) {
            const EmployeePayroll::CustomWithholding w = employee.withholding.value(i.id);
            if (!positive(w.percent) && !w.extra.isPositive()) {
                continue; // the employee carries no rate for it
            }
            in.employeeRate = w.percent;
            in.employeeExtra = w.extra;
        }
        const PayrollItemToDate before = ytd.value(i.id, PayrollItemToDate{zero, zero});
        in.ytdWages = before.wages;
        in.ytdAmount = before.amount;
        out << in;
    }
    return out;
}

Result PayrollItems::recordPaycheck(qint64 docId, const QList<CustomItemResult> &results)
{
    for (const CustomItemResult &c : results) {
        if (c.wages.isZero() && c.amount.isZero()) {
            continue;
        }
        QString error;
        if (!Sql::exec(m_file.database(),
                       QStringLiteral("INSERT INTO paycheck_items(doc_id, item_id, name, side, kind, wages, amount)"
                                      " VALUES(?, ?, ?, ?, ?, ?, ?)"),
                       {docId, c.id, c.name, customSideKey(c.side), customKindKey(c.kind), c.wages.minor(),
                        c.amount.minor()},
                       &error)) {
            return Result::failure(ErrorCode::Storage, error);
        }
    }
    return Result::success(docId);
}

void PayrollItems::attach(QList<PaycheckRecord> *records) const
{
    if (records == nullptr || records->isEmpty()) {
        return;
    }
    QHash<qint64, qsizetype> index;
    QStringList ids;
    for (qsizetype i = 0; i < records->size(); ++i) {
        index.insert(records->at(i).docId, i);
        ids << QString::number(records->at(i).docId);
    }
    const QString cur = m_file.currency();
    // Document ids are integers this function formats itself, never text a
    // user typed; batched so the statement stays small.
    for (qsizetype from = 0; from < ids.size(); from += 500) {
        QSqlQuery q(m_file.database());
        if (!Sql::run(q, QStringLiteral("SELECT doc_id, item_id, name, side, kind, wages, amount FROM paycheck_items"
                                        " WHERE doc_id IN (%1) ORDER BY doc_id, item_id")
                             .arg(ids.mid(from, 500).join(QLatin1Char(','))))) {
            return;
        }
        while (q.next()) {
            PaycheckRecord &r = (*records)[index.value(q.value(0).toLongLong())];
            CustomItemResult c;
            c.id = q.value(1).toLongLong();
            c.name = q.value(2).toString();
            c.side = customSideFromKey(q.value(3).toString()).value_or(CustomItem::Side::Employer);
            c.kind = customKindFromKey(q.value(4).toString()).value_or(CustomItem::Kind::Tax);
            c.wages = Money(q.value(5).toLongLong(), cur);
            c.amount = Money(q.value(6).toLongLong(), cur);
            if (c.side == CustomItem::Side::Employee) {
                r.taxes.customEmployee += c.amount;
                r.taxes.employeeTaxes += c.amount;
            } else {
                r.taxes.customEmployer += c.amount;
                r.taxes.employerTaxes += c.amount;
            }
            r.taxes.custom << c;
        }
    }
}

DuePeriod PayrollItems::periodFor(const PayrollItem &item, const QDate &payDate, const BusinessCalendar &calendar)
{
    DuePeriod p;
    const QLocale english(QLocale::English);
    switch (item.frequency) {
    case PayrollItem::Frequency::Monthly:
        p.start = QDate(payDate.year(), payDate.month(), 1);
        p.end = p.start.addMonths(1).addDays(-1);
        p.label = english.toString(p.start, QStringLiteral("MMMM yyyy"));
        break;
    case PayrollItem::Frequency::Quarterly:
        p.start = quarterStart(payDate.year(), quarterOf(payDate));
        p.end = quarterEnd(payDate.year(), quarterOf(payDate));
        p.label = quarterLabel(payDate.year(), quarterOf(payDate));
        break;
    case PayrollItem::Frequency::Annually:
        p.start = QDate(payDate.year(), 1, 1);
        p.end = QDate(payDate.year(), 12, 31);
        p.label = QString::number(payDate.year());
        break;
    }
    const QDate after = p.end.addDays(1);
    const int day = item.dueDay == 0 ? after.daysInMonth() : qMin(item.dueDay, after.daysInMonth());
    p.statutory = QDate(after.year(), after.month(), day);
    p.due = calendar.onOrAfter(p.statutory);
    return p;
}

} // namespace QindaOffice::Books
