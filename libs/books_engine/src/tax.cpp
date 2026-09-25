// SPDX-License-Identifier: GPL-3.0-or-later
#include "tax.h"

#include "accounts.h"
#include "company_file.h"
#include "journal.h"
#include "sql_util.h"

#include <QSqlQuery>

namespace QindaOffice::Books {

namespace {

// Σ of decimal fractions with a common scale (exact).
Decimal addDecimals(const Decimal &a, const Decimal &b)
{
    const int scale = qMax(a.scale(), b.scale());
    return Decimal(a.mantissa() * pow10i(scale - a.scale()) + b.mantissa() * pow10i(scale - b.scale()), scale)
        .normalized();
}

} // namespace

Decimal TaxRate::combinedRate() const
{
    Decimal sum(0, 0);
    for (const TaxComponent &c : components) {
        sum = addDecimals(sum, c.rate);
    }
    return sum;
}

SalesTax::SalesTax(CompanyFile &file)
    : m_file(file)
{
}

Result SalesTax::createAgency(const TaxAgency &agency)
{
    if (agency.name.trimmed().isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a tax agency needs a name"));
    }
    SqlTransaction txn(m_file);
    QString error;
    const qint64 id = Sql::insert(m_file.database(),
                                  QStringLiteral("INSERT INTO tax_agencies(name, registration_id, rounding, active)"
                                                 " VALUES(?,?,?,?)"),
                                  {agency.name.trimmed(), agency.registrationId, int(agency.rounding),
                                   agency.active ? 1 : 0},
                                  &error);
    if (id == 0) {
        return Result::failure(ErrorCode::Duplicate, error);
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("tax_agency"), id, {}, agency.name);
    txn.commit();
    return Result::success(id);
}

Result SalesTax::createComponent(const TaxComponent &component)
{
    if (component.name.trimmed().isEmpty() || component.rate.isNegative()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a tax component needs a name and a rate ≥ 0"));
    }
    if (!agency(component.agencyId)) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("unknown tax agency"));
    }
    SqlTransaction txn(m_file);
    QString error;
    const qint64 id = Sql::insert(
        m_file.database(),
        QStringLiteral("INSERT INTO tax_components(agency_id, name, rate, active) VALUES(?,?,?,?)"),
        {component.agencyId, component.name.trimmed(), component.rate.toString(), component.active ? 1 : 0}, &error);
    if (id == 0) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("tax_component"), id, {},
                 component.name + QLatin1Char(' ') + component.rate.toPercentString());
    txn.commit();
    return Result::success(id);
}

Result SalesTax::createRate(const QString &name, const QList<qint64> &componentIds)
{
    if (name.trimmed().isEmpty() || componentIds.isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a tax rate needs a name and components"));
    }
    SqlTransaction txn(m_file);
    QString error;
    const qint64 id = Sql::insert(m_file.database(), QStringLiteral("INSERT INTO tax_rates(name) VALUES(?)"),
                                  {name.trimmed()}, &error);
    if (id == 0) {
        return Result::failure(ErrorCode::Duplicate, error);
    }
    for (qint64 component : componentIds) {
        if (!Sql::exec(m_file.database(),
                       QStringLiteral("INSERT INTO tax_rate_components(rate_id, component_id) VALUES(?, ?)"),
                       {id, component}, &error)) {
            return Result::failure(ErrorCode::NotFound, error);
        }
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("tax_rate"), id, {}, name);
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(id);
}

Result SalesTax::createSimpleRate(qint64 agencyId, const QString &name, const Decimal &rate)
{
    SqlTransaction txn(m_file);
    const Result component = createComponent(TaxComponent{0, agencyId, name, rate, true});
    if (!component.ok) {
        return component;
    }
    const Result created = createRate(name, {component.id});
    if (!created.ok || !txn.commit()) {
        return created.ok ? Result::failure(ErrorCode::Storage, txn.error()) : created;
    }
    return created;
}

QList<TaxAgency> SalesTax::agencies() const
{
    QList<TaxAgency> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT id, name, registration_id, rounding, active FROM tax_agencies ORDER BY name"));
    while (q.next()) {
        out << TaxAgency{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toString(),
                         RoundingMode(q.value(3).toInt()), q.value(4).toBool()};
    }
    return out;
}

std::optional<TaxAgency> SalesTax::agency(qint64 id) const
{
    for (const TaxAgency &a : agencies()) {
        if (a.id == id) {
            return a;
        }
    }
    return std::nullopt;
}

std::optional<TaxRate> SalesTax::rate(qint64 id) const
{
    TaxRate rate;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT id, name, active FROM tax_rates WHERE id = ?"), {id});
        if (!q.next()) {
            return std::nullopt;
        }
        rate.id = q.value(0).toLongLong();
        rate.name = q.value(1).toString();
        rate.active = q.value(2).toBool();
    }
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT c.id, c.agency_id, c.name, c.rate, c.active FROM tax_rate_components rc"
                               " JOIN tax_components c ON c.id = rc.component_id WHERE rc.rate_id = ? ORDER BY c.id"),
             {id});
    while (q.next()) {
        rate.components << TaxComponent{q.value(0).toLongLong(), q.value(1).toLongLong(), q.value(2).toString(),
                                        Decimal::parse(q.value(3).toString()).value_or(Decimal()),
                                        q.value(4).toBool()};
    }
    return rate;
}

QList<TaxRate> SalesTax::rates() const
{
    QList<qint64> ids;
    {
        QSqlQuery q(m_file.database());
        Sql::run(q, QStringLiteral("SELECT id FROM tax_rates ORDER BY name"));
        while (q.next()) {
            ids << q.value(0).toLongLong();
        }
    }
    QList<TaxRate> out;
    for (qint64 id : ids) {
        if (const std::optional<TaxRate> r = rate(id)) {
            out << *r;
        }
    }
    return out;
}

QList<TaxShare> SalesTax::compute(qint64 rateId, const Money &taxableBase) const
{
    QList<TaxShare> shares;
    const std::optional<TaxRate> r = rate(rateId);
    if (!r) {
        return shares;
    }
    for (const TaxComponent &c : r->components) {
        const std::optional<TaxAgency> a = agency(c.agencyId);
        const RoundingMode mode = a ? a->rounding : RoundingMode::HalfUp;
        shares << TaxShare{c.id, c.agencyId, c.name, c.rate, taxableBase.multiplied(c.rate, mode)};
    }
    return shares;
}

Result SalesTax::recordPayment(qint64 agencyId, qint64 bankAccountId, const Money &amount, const QDate &date,
                               const QString &memo)
{
    if (!agency(agencyId)) {
        return Result::failure(ErrorCode::NotFound, QStringLiteral("unknown tax agency"));
    }
    if (!amount.isPositive() || !amount.sameCurrency(m_file.money(0))) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a tax payment needs a positive amount"));
    }
    const qint64 payable = ChartOfAccounts(m_file).idForRole(SystemRole::SalesTaxPayable);
    SqlTransaction txn(m_file);
    BusinessDocument doc;
    doc.kind = DocKind::SalesTaxPayment;
    doc.date = date;
    doc.memo = memo;
    doc.accountId = bankAccountId;
    doc.total = amount;
    doc.subtotal = amount;
    Documents documents(m_file);
    if (const Result w = documents.write(doc); !w.ok) {
        return w;
    }
    PostingLine debit{payable, amount.minor(), 0, 0, 0, 0, 0, agencyId, memo};
    PostingLine credit{bankAccountId, 0, amount.minor(), 0, 0, 0, 0, agencyId, memo};
    if (const Result p = Ledger(m_file).postLines(doc.id, doc.kind, date, {}, memo, {debit, credit}, {}); !p.ok) {
        return p;
    }
    if (!txn.commit()) {
        return Result::failure(ErrorCode::Storage, txn.error());
    }
    return Result::success(doc.id);
}

Money SalesTax::liability(qint64 agencyId, const QDate &asOf) const
{
    return m_file.money(Sql::scalarInt(
        m_file.database(),
        QStringLiteral("SELECT COALESCE(SUM(l.credit) - SUM(l.debit), 0) FROM journal_lines l"
                       " JOIN accounts a ON a.id = l.account_id AND a.system_role = 'sales_tax_payable'"
                       " WHERE l.voided = 0 AND l.tax_agency_id = ? AND (? = '' OR l.date <= ?)"),
        {agencyId, Sql::isoDate(asOf), Sql::isoDate(asOf)}));
}

} // namespace QindaOffice::Books
