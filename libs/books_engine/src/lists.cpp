// SPDX-License-Identifier: GPL-3.0-or-later
#include "lists.h"

#include "company_file.h"
#include "sql_util.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>

namespace QindaOffice::Books {

namespace {

const QString kPartyColumns = QStringLiteral(
    "id, kind, display_name, company_name, contact_name, email, phone, billing_address, shipping_address,"
    " tax_id, terms_id, is_1099, default_account_id, tax_rate_id, notes, active");

Party readParty(const QSqlQuery &q)
{
    Party p;
    p.id = q.value(0).toLongLong();
    p.kind = PartyKind(q.value(1).toInt());
    p.displayName = q.value(2).toString();
    p.companyName = q.value(3).toString();
    p.contactName = q.value(4).toString();
    p.email = q.value(5).toString();
    p.phone = q.value(6).toString();
    p.billingAddress = q.value(7).toString();
    p.shippingAddress = q.value(8).toString();
    p.taxId = q.value(9).toString();
    p.termsId = q.value(10).toLongLong();
    p.is1099 = q.value(11).toBool();
    p.defaultAccountId = q.value(12).toLongLong();
    p.taxRateId = q.value(13).toLongLong();
    p.notes = q.value(14).toString();
    p.active = q.value(15).toBool();
    return p;
}

QVariantList partyValues(const Party &p)
{
    return {int(p.kind),         p.displayName.trimmed(),
            p.companyName,       p.contactName,
            p.email,             p.phone,
            p.billingAddress,    p.shippingAddress,
            p.taxId,             Sql::idOrNull(p.termsId),
            p.is1099 ? 1 : 0,    Sql::idOrNull(p.defaultAccountId),
            Sql::idOrNull(p.taxRateId), p.notes,
            p.active ? 1 : 0};
}

QString partyJson(const Party &p)
{
    return QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("name"), p.displayName},
                                                       {QStringLiteral("kind"), int(p.kind)},
                                                       {QStringLiteral("email"), p.email},
                                                       {QStringLiteral("active"), p.active}})
                                 .toJson(QJsonDocument::Compact));
}

const QString kItemColumns = QStringLiteral(
    "id, name, kind, description, sku, price, cost, income_account_id, expense_account_id, taxable, active");

Item readItem(const QSqlQuery &q, const QString &currency)
{
    Item i;
    i.id = q.value(0).toLongLong();
    i.name = q.value(1).toString();
    i.kind = ItemKind(q.value(2).toInt());
    i.description = q.value(3).toString();
    i.sku = q.value(4).toString();
    i.price = Money(q.value(5).toLongLong(), currency);
    i.cost = Money(q.value(6).toLongLong(), currency);
    i.incomeAccountId = q.value(7).toLongLong();
    i.expenseAccountId = q.value(8).toLongLong();
    i.taxable = q.value(9).toBool();
    i.active = q.value(10).toBool();
    return i;
}

} // namespace

Lists::Lists(CompanyFile &file)
    : m_file(file)
{
}

Result Lists::createParty(const Party &party)
{
    if (party.displayName.trimmed().isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a name is required"));
    }
    if (findParty(party.kind, party.displayName) != 0) {
        return Result::failure(ErrorCode::Duplicate,
                               QStringLiteral("%1 already exists").arg(party.displayName.trimmed()));
    }
    SqlTransaction txn(m_file);
    QString error;
    QVariantList values = partyValues(party);
    values << CompanyFile::nowStamp();
    const qint64 id = Sql::insert(
        m_file.database(),
        QStringLiteral("INSERT INTO parties(kind, display_name, company_name, contact_name, email, phone,"
                       " billing_address, shipping_address, tax_id, terms_id, is_1099, default_account_id,"
                       " tax_rate_id, notes, active, created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"),
        values, &error);
    if (id == 0) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("party"), id, {}, partyJson(party));
    txn.commit();
    return Result::success(id);
}

Result Lists::updateParty(const Party &party)
{
    const std::optional<Party> before = this->party(party.id);
    if (!before) {
        return Result::failure(ErrorCode::UnknownParty, QStringLiteral("unknown customer or vendor"));
    }
    if (party.displayName.trimmed().isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a name is required"));
    }
    const qint64 clash = findParty(party.kind, party.displayName);
    if (clash != 0 && clash != party.id) {
        return Result::failure(ErrorCode::Duplicate, QStringLiteral("%1 already exists").arg(party.displayName));
    }
    SqlTransaction txn(m_file);
    QVariantList values = partyValues(party);
    values << party.id;
    QString error;
    if (!Sql::exec(m_file.database(),
                   QStringLiteral("UPDATE parties SET kind = ?, display_name = ?, company_name = ?, contact_name = ?,"
                                  " email = ?, phone = ?, billing_address = ?, shipping_address = ?, tax_id = ?,"
                                  " terms_id = ?, is_1099 = ?, default_account_id = ?, tax_rate_id = ?, notes = ?,"
                                  " active = ? WHERE id = ?"),
                   values, &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(QStringLiteral("update"), QStringLiteral("party"), party.id, partyJson(*before), partyJson(party));
    txn.commit();
    return Result::success(party.id);
}

Result Lists::setPartyActive(qint64 id, bool active)
{
    std::optional<Party> p = party(id);
    if (!p) {
        return Result::failure(ErrorCode::UnknownParty, QStringLiteral("unknown customer or vendor"));
    }
    p->active = active;
    return updateParty(*p);
}

std::optional<Party> Lists::party(qint64 id) const
{
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT ") + kPartyColumns + QStringLiteral(" FROM parties WHERE id = ?"), {id});
    if (!q.next()) {
        return std::nullopt;
    }
    return readParty(q);
}

QList<Party> Lists::parties(PartyKind kind, bool includeInactive) const
{
    QList<Party> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT ") + kPartyColumns
                    + QStringLiteral(" FROM parties WHERE kind = ? AND (? OR active = 1)"
                                     " ORDER BY display_name COLLATE NOCASE"),
             {int(kind), includeInactive ? 1 : 0});
    while (q.next()) {
        out << readParty(q);
    }
    return out;
}

qint64 Lists::findParty(PartyKind kind, const QString &displayName) const
{
    return Sql::scalarInt(m_file.database(),
                          QStringLiteral("SELECT id FROM parties WHERE kind = ? AND display_name = ? COLLATE NOCASE"),
                          {int(kind), displayName.trimmed()});
}

Result Lists::createItem(const Item &item)
{
    if (item.name.trimmed().isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("an item needs a name"));
    }
    if (item.incomeAccountId == 0 && item.expenseAccountId == 0) {
        return Result::failure(ErrorCode::UnknownAccount,
                               QStringLiteral("an item needs an income or an expense account"));
    }
    if (findItem(item.name) != 0) {
        return Result::failure(ErrorCode::Duplicate, QStringLiteral("%1 already exists").arg(item.name));
    }
    SqlTransaction txn(m_file);
    QString error;
    const qint64 id = Sql::insert(
        m_file.database(),
        QStringLiteral("INSERT INTO items(name, kind, description, sku, price, cost, income_account_id,"
                       " expense_account_id, taxable, active) VALUES(?,?,?,?,?,?,?,?,?,?)"),
        {item.name.trimmed(), int(item.kind), item.description, item.sku, item.price.minor(), item.cost.minor(),
         Sql::idOrNull(item.incomeAccountId), Sql::idOrNull(item.expenseAccountId), item.taxable ? 1 : 0,
         item.active ? 1 : 0},
        &error);
    if (id == 0) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("item"), id, {}, item.name);
    txn.commit();
    return Result::success(id);
}

Result Lists::updateItem(const Item &item)
{
    if (!this->item(item.id)) {
        return Result::failure(ErrorCode::UnknownItem, QStringLiteral("unknown item"));
    }
    const qint64 clash = findItem(item.name);
    if (clash != 0 && clash != item.id) {
        return Result::failure(ErrorCode::Duplicate, QStringLiteral("%1 already exists").arg(item.name));
    }
    SqlTransaction txn(m_file);
    QString error;
    if (!Sql::exec(m_file.database(),
                   QStringLiteral("UPDATE items SET name = ?, kind = ?, description = ?, sku = ?, price = ?, cost = ?,"
                                  " income_account_id = ?, expense_account_id = ?, taxable = ?, active = ?"
                                  " WHERE id = ?"),
                   {item.name.trimmed(), int(item.kind), item.description, item.sku, item.price.minor(),
                    item.cost.minor(), Sql::idOrNull(item.incomeAccountId), Sql::idOrNull(item.expenseAccountId),
                    item.taxable ? 1 : 0, item.active ? 1 : 0, item.id},
                   &error)) {
        return Result::failure(ErrorCode::Storage, error);
    }
    m_file.audit(QStringLiteral("update"), QStringLiteral("item"), item.id, {}, item.name);
    txn.commit();
    return Result::success(item.id);
}

Result Lists::setItemActive(qint64 id, bool active)
{
    std::optional<Item> i = item(id);
    if (!i) {
        return Result::failure(ErrorCode::UnknownItem, QStringLiteral("unknown item"));
    }
    i->active = active;
    return updateItem(*i);
}

std::optional<Item> Lists::item(qint64 id) const
{
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT ") + kItemColumns + QStringLiteral(" FROM items WHERE id = ?"), {id});
    if (!q.next()) {
        return std::nullopt;
    }
    return readItem(q, m_file.currency());
}

QList<Item> Lists::items(bool includeInactive) const
{
    QList<Item> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT ") + kItemColumns
                    + QStringLiteral(" FROM items WHERE (? OR active = 1) ORDER BY name COLLATE NOCASE"),
             {includeInactive ? 1 : 0});
    while (q.next()) {
        out << readItem(q, m_file.currency());
    }
    return out;
}

qint64 Lists::findItem(const QString &name) const
{
    return Sql::scalarInt(m_file.database(), QStringLiteral("SELECT id FROM items WHERE name = ? COLLATE NOCASE"),
                          {name.trimmed()});
}

Result Lists::createClass(const ClassTag &tag)
{
    if (tag.name.trimmed().isEmpty()) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("a class needs a name"));
    }
    QString error;
    const qint64 id = Sql::insert(m_file.database(),
                                  QStringLiteral("INSERT INTO classes(name, parent_id, active) VALUES(?,?,?)"),
                                  {tag.name.trimmed(), Sql::idOrNull(tag.parentId), tag.active ? 1 : 0}, &error);
    if (id == 0) {
        return Result::failure(ErrorCode::Duplicate, error);
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("class"), id, {}, tag.name);
    return Result::success(id);
}

QList<ClassTag> Lists::classes(bool includeInactive) const
{
    QList<ClassTag> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT id, name, parent_id, active FROM classes WHERE (? OR active = 1) ORDER BY name"),
             {includeInactive ? 1 : 0});
    while (q.next()) {
        out << ClassTag{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toLongLong(), q.value(3).toBool()};
    }
    return out;
}

qint64 Lists::findClass(const QString &name) const
{
    return Sql::scalarInt(m_file.database(), QStringLiteral("SELECT id FROM classes WHERE name = ? COLLATE NOCASE"),
                          {name.trimmed()});
}

Result Lists::createTerms(const Terms &terms)
{
    if (terms.name.trimmed().isEmpty() || terms.dueDays < 0) {
        return Result::failure(ErrorCode::Invalid, QStringLiteral("terms need a name and due days ≥ 0"));
    }
    QString error;
    const qint64 id = Sql::insert(m_file.database(),
                                  QStringLiteral("INSERT INTO terms(name, due_days, active) VALUES(?,?,?)"),
                                  {terms.name.trimmed(), terms.dueDays, terms.active ? 1 : 0}, &error);
    if (id == 0) {
        return Result::failure(ErrorCode::Duplicate, error);
    }
    m_file.audit(QStringLiteral("create"), QStringLiteral("terms"), id, {}, terms.name);
    return Result::success(id);
}

QList<Terms> Lists::terms(bool includeInactive) const
{
    QList<Terms> out;
    QSqlQuery q(m_file.database());
    Sql::run(q, QStringLiteral("SELECT id, name, due_days, active FROM terms WHERE (? OR active = 1)"
                               " ORDER BY due_days, name"),
             {includeInactive ? 1 : 0});
    while (q.next()) {
        out << Terms{q.value(0).toLongLong(), q.value(1).toString(), q.value(2).toInt(), q.value(3).toBool()};
    }
    return out;
}

qint64 Lists::findTerms(const QString &name) const
{
    return Sql::scalarInt(m_file.database(), QStringLiteral("SELECT id FROM terms WHERE name = ? COLLATE NOCASE"),
                          {name.trimmed()});
}

} // namespace QindaOffice::Books
