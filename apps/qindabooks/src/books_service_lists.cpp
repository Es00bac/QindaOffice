// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService: the name lists — chart of accounts, customers and vendors,
// products and services (terms, classes, tax: books_service_tax.cpp).
#include "books_service.h"

#include "accounts.h"
#include "company_file.h"
#include "lists.h"
#include "purchase_orders.h"
#include "purchases.h"
#include "sales.h"

#include <QHash>

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

namespace {

// Item types as the QML API names them: service | product | other.
QString itemTypeKey(ItemKind kind)
{
    switch (kind) {
    case ItemKind::Service: return QStringLiteral("service");
    case ItemKind::NonInventory: return QStringLiteral("product");
    case ItemKind::OtherCharge: return QStringLiteral("other");
    }
    return QStringLiteral("service");
}

QString itemTypeLabel(ItemKind kind)
{
    switch (kind) {
    case ItemKind::Service: return QObject::tr("Service");
    case ItemKind::NonInventory: return QObject::tr("Product");
    case ItemKind::OtherCharge: return QObject::tr("Other charge");
    }
    return QString();
}

ItemKind itemKindFrom(const QString &key)
{
    if (key == QLatin1String("product") || key == QLatin1String("nonInventory")) {
        return ItemKind::NonInventory;
    }
    return key == QLatin1String("other") || key == QLatin1String("otherCharge") ? ItemKind::OtherCharge
                                                                               : ItemKind::Service;
}

QVariantMap partyMap(const Party &p, qint64 balance, const MoneyFormat &money)
{
    return {{QStringLiteral("id"), p.id},
            {QStringLiteral("kind"), p.kind == PartyKind::Vendor ? QStringLiteral("vendor") : QStringLiteral("customer")},
            {QStringLiteral("name"), p.displayName},
            {QStringLiteral("company"), p.companyName},
            {QStringLiteral("contact"), p.contactName},
            {QStringLiteral("email"), p.email},
            {QStringLiteral("phone"), p.phone},
            {QStringLiteral("address"), p.billingAddress},
            {QStringLiteral("billingAddress"), p.billingAddress},
            {QStringLiteral("shippingAddress"), p.shippingAddress},
            {QStringLiteral("taxId"), p.taxId},
            {QStringLiteral("termsId"), p.termsId},
            {QStringLiteral("is1099"), p.is1099},
            {QStringLiteral("defaultAccountId"), p.defaultAccountId},
            {QStringLiteral("taxRateId"), p.taxRateId},
            {QStringLiteral("notes"), p.notes},
            {QStringLiteral("active"), p.active},
            {QStringLiteral("balance"), balance},
            {QStringLiteral("balanceText"), money.text(balance)}};
}

QString classKey(AccountClass c)
{
    switch (c) {
    case AccountClass::Asset: return QStringLiteral("asset");
    case AccountClass::Liability: return QStringLiteral("liability");
    case AccountClass::Equity: return QStringLiteral("equity");
    case AccountClass::Income: return QStringLiteral("income");
    case AccountClass::Expense: return QStringLiteral("expense");
    }
    return QString();
}

QVariantMap accountBrief(const Account &a)
{
    return {{QStringLiteral("id"), a.id},
            {QStringLiteral("name"), a.fullName},
            {QStringLiteral("number"), a.number},
            {QStringLiteral("type"), accountTypeInfo(a.type).key}};
}

} // namespace

QVariantList BooksService::accounts(bool includeInactive) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    ChartOfAccounts chart(*c);
    for (const Account &a : chart.accounts(includeInactive)) {
        const AccountTypeInfo info = accountTypeInfo(a.type);
        const qint64 balance = chart.balance(a.id).minor();
        out << QVariantMap{{QStringLiteral("id"), a.id},
                           {QStringLiteral("number"), a.number},
                           {QStringLiteral("name"), a.name},
                           {QStringLiteral("fullName"), a.fullName},
                           {QStringLiteral("depth"), a.depth},
                           {QStringLiteral("parentId"), a.parentId},
                           {QStringLiteral("type"), info.key},
                           {QStringLiteral("typeLabel"), info.label},
                           {QStringLiteral("accountClass"), classKey(info.accountClass)},
                           {QStringLiteral("section"), reportSectionLabel(info.section)},
                           {QStringLiteral("balance"), balance},
                           {QStringLiteral("balanceText"), money.text(balance)},
                           {QStringLiteral("active"), a.active},
                           {QStringLiteral("systemRole"), a.systemRole},
                           {QStringLiteral("description"), a.description},
                           {QStringLiteral("hasPostings"), chart.hasPostings(a.id)}};
    }
    return out;
}

QVariantList BooksService::accountTypes() const
{
    QVariantList out;
    for (AccountType type : allAccountTypes()) {
        const AccountTypeInfo info = accountTypeInfo(type);
        out << QVariantMap{{QStringLiteral("id"), info.key},
                           {QStringLiteral("key"), info.key},
                           {QStringLiteral("label"), info.label},
                           {QStringLiteral("accountClass"), classKey(info.accountClass)},
                           {QStringLiteral("section"), reportSectionLabel(info.section)}};
    }
    return out;
}

QVariantMap BooksService::saveAccount(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    ChartOfAccounts chart(*c);
    const qint64 id = Format::id(map.value(QStringLiteral("id")));
    Account a;
    if (id != 0) {
        const std::optional<Account> existing = chart.account(id);
        if (!existing) {
            return Format::failure(tr("That account no longer exists."));
        }
        a = *existing;
    }
    if (map.contains(QStringLiteral("type"))) {
        const std::optional<AccountType> type = accountTypeFromKey(map.value(QStringLiteral("type")).toString());
        if (!type) {
            return Format::failure(tr("Choose an account type."));
        }
        a.type = *type;
    }
    if (map.contains(QStringLiteral("number"))) {
        a.number = map.value(QStringLiteral("number")).toString().trimmed();
    }
    if (map.contains(QStringLiteral("name"))) {
        a.name = map.value(QStringLiteral("name")).toString().trimmed();
    }
    if (map.contains(QStringLiteral("description"))) {
        a.description = map.value(QStringLiteral("description")).toString();
    }
    if (map.contains(QStringLiteral("parentId"))) {
        a.parentId = Format::id(map.value(QStringLiteral("parentId")));
    }
    Result saved = id == 0 ? chart.create(a) : chart.update(a);
    if (!saved.ok) {
        return Format::result(saved);
    }
    const qint64 savedId = id == 0 ? saved.id : id;
    // An opening balance on a new balance-sheet account (bank, loan, …).
    const QVariant opening = map.value(QStringLiteral("openingBalance"));
    if (id == 0 && !opening.toString().trimmed().isEmpty()) {
        QString error;
        const std::optional<qint64> minor = moneyFormat().parse(opening, &error);
        if (!minor) {
            return Format::failure(error);
        }
        QDate date = Format::date(map.value(QStringLiteral("openingDate")));
        if (!date.isValid()) {
            date = todayDate();
        }
        if (*minor != 0) {
            const Result ob = chart.setOpeningBalance(savedId, c->money(*minor), date);
            if (!ob.ok) {
                QVariantMap out = Format::result(ob);
                out.insert(QStringLiteral("id"), savedId); // the account itself exists
                return out;
            }
        }
    }
    return Format::success(savedId);
}

QVariantMap BooksService::setAccountActive(qint64 id, bool active)
{
    return file() != nullptr ? Format::result(ChartOfAccounts(*file()).setActive(id, active)) : noCompany();
}

QVariantList BooksService::paymentAccounts() const
{
    QVariantList out;
    if (CompanyFile *c = file()) {
        ChartOfAccounts chart(*c);
        const qint64 undeposited = chart.idForRole(SystemRole::UndepositedFunds);
        for (const Account &a : chart.accounts()) {
            if (a.type == AccountType::Bank || a.type == AccountType::CreditCard || a.id == undeposited) {
                out << accountBrief(a);
            }
        }
    }
    return out;
}

QVariantList BooksService::expenseAccounts() const
{
    QVariantList out;
    if (CompanyFile *c = file()) {
        for (const Account &a : ChartOfAccounts(*c).accounts()) {
            if (a.type == AccountType::Expense || a.type == AccountType::CostOfGoodsSold
                || a.type == AccountType::OtherExpense) {
                out << accountBrief(a);
            }
        }
    }
    return out;
}

QVariantList BooksService::incomeAccounts() const
{
    QVariantList out;
    if (CompanyFile *c = file()) {
        for (const Account &a : ChartOfAccounts(*c).accounts()) {
            if (a.type == AccountType::Income || a.type == AccountType::OtherIncome) {
                out << accountBrief(a);
            }
        }
    }
    return out;
}

// ---- parties -------------------------------------------------------------------

QVariantList BooksService::parties(const QString &kind, bool includeInactive) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const bool vendors = kind == QLatin1String("vendor");
    const QDate today = todayDate();
    // Open and overdue counts from ONE listing, not one query per party.
    QHash<qint64, int> open;
    QHash<qint64, int> overdue;
    const QList<BusinessDocument> docs = vendors ? Purchases(*c).unpaidBills() : Sales(*c).openInvoices();
    for (const BusinessDocument &d : docs) {
        ++open[d.partyId];
        if (d.dueDate.isValid() && d.dueDate < today) {
            ++overdue[d.partyId];
        }
    }
    // M68: each vendor's open purchase orders and what they still have to receive.
    QHash<qint64, int> orders;
    QHash<qint64, qint64> toReceive;
    if (vendors) {
        PurchaseOrders purchaseOrders(*c);
        for (const BusinessDocument &po : Documents(*c).list({{DocKind::PurchaseOrder}, 0, {}, {}, false, false})) {
            const DocumentStatus status = purchaseOrders.status(po);
            if (status == DocumentStatus::Open || status == DocumentStatus::PartiallyReceived) {
                ++orders[po.partyId];
                toReceive[po.partyId] += purchaseOrders.remainingAmount(po.id).minor();
            }
        }
    }
    const MoneyFormat money = moneyFormat();
    for (const Party &p : Lists(*c).parties(vendors ? PartyKind::Vendor : PartyKind::Customer, includeInactive)) {
        const qint64 balance =
            (vendors ? Purchases(*c).vendorBalance(p.id) : Sales(*c).customerBalance(p.id)).minor();
        QVariantMap row = partyMap(p, balance, money);
        row.insert(QStringLiteral("openCount"), open.value(p.id));
        row.insert(QStringLiteral("overdueCount"), overdue.value(p.id));
        if (vendors) {
            row.insert(QStringLiteral("openOrders"), orders.value(p.id));
            row.insert(QStringLiteral("toReceive"), toReceive.value(p.id));
            row.insert(QStringLiteral("toReceiveText"), money.text(toReceive.value(p.id)));
        }
        out << row;
    }
    return out;
}

QVariantMap BooksService::party(qint64 id) const
{
    CompanyFile *c = file();
    const std::optional<Party> p = c != nullptr ? Lists(*c).party(id) : std::nullopt;
    if (!p) {
        return {};
    }
    const bool vendor = p->kind == PartyKind::Vendor;
    const qint64 balance = (vendor ? Purchases(*c).vendorBalance(p->id) : Sales(*c).customerBalance(p->id)).minor();
    return partyMap(*p, balance, moneyFormat());
}

QVariantMap BooksService::saveParty(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    Lists lists(*c);
    const qint64 id = Format::id(map.value(QStringLiteral("id")));
    Party p;
    if (id != 0) {
        const std::optional<Party> existing = lists.party(id);
        if (!existing) {
            return Format::failure(tr("That name no longer exists."));
        }
        p = *existing;
    } else {
        p.kind = map.value(QStringLiteral("kind")).toString() == QLatin1String("vendor") ? PartyKind::Vendor
                                                                                         : PartyKind::Customer;
    }
    const auto text = [&map](const char *key, QString *field) {
        const QString k = QString::fromLatin1(key);
        if (map.contains(k)) {
            *field = map.value(k).toString();
        }
    };
    text("name", &p.displayName);
    p.displayName = p.displayName.trimmed();
    text("company", &p.companyName);
    text("contact", &p.contactName);
    text("email", &p.email);
    text("phone", &p.phone);
    text("address", &p.billingAddress);
    text("billingAddress", &p.billingAddress);
    text("shippingAddress", &p.shippingAddress);
    text("taxId", &p.taxId);
    text("notes", &p.notes);
    for (const auto &[key, field] : {std::pair{"termsId", &p.termsId}, std::pair{"defaultAccountId", &p.defaultAccountId},
                                     std::pair{"taxRateId", &p.taxRateId}}) {
        if (map.contains(QString::fromLatin1(key))) {
            *field = Format::id(map.value(QString::fromLatin1(key)));
        }
    }
    if (map.contains(QStringLiteral("is1099"))) {
        p.is1099 = map.value(QStringLiteral("is1099")).toBool();
    }
    const Result saved = id == 0 ? lists.createParty(p) : lists.updateParty(p);
    if (saved.ok && id != 0 && map.contains(QStringLiteral("active"))) {
        static_cast<void>(lists.setPartyActive(id, map.value(QStringLiteral("active")).toBool()));
    }
    return saved.ok ? Format::success(id == 0 ? saved.id : id) : Format::result(saved);
}

QVariantMap BooksService::setPartyActive(qint64 id, bool active)
{
    return file() != nullptr ? Format::result(Lists(*file()).setPartyActive(id, active)) : noCompany();
}

// ---- items ---------------------------------------------------------------------

QVariantList BooksService::items(bool includeInactive) const
{
    QVariantList out;
    CompanyFile *c = file();
    if (c == nullptr) {
        return out;
    }
    const MoneyFormat money = moneyFormat();
    for (const Item &i : Lists(*c).items(includeInactive)) {
        out << QVariantMap{{QStringLiteral("id"), i.id},
                           {QStringLiteral("name"), i.name},
                           {QStringLiteral("type"), itemTypeKey(i.kind)},
                           {QStringLiteral("typeLabel"), itemTypeLabel(i.kind)},
                           {QStringLiteral("description"), i.description},
                           {QStringLiteral("sku"), i.sku},
                           // `price`/`cost` are the plain text an editor shows
                           // ("95.00"); `…Minor` the integers.
                           {QStringLiteral("price"), money.plain(i.price.minor())},
                           {QStringLiteral("priceMinor"), i.price.minor()},
                           {QStringLiteral("priceText"), money.text(i.price.minor())},
                           {QStringLiteral("cost"), money.plain(i.cost.minor())},
                           {QStringLiteral("costMinor"), i.cost.minor()},
                           {QStringLiteral("costText"), money.text(i.cost.minor())},
                           {QStringLiteral("incomeAccountId"), i.incomeAccountId},
                           {QStringLiteral("expenseAccountId"), i.expenseAccountId},
                           {QStringLiteral("taxable"), i.taxable},
                           {QStringLiteral("active"), i.active}};
    }
    return out;
}

QVariantMap BooksService::saveItem(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    Lists lists(*c);
    const qint64 id = Format::id(map.value(QStringLiteral("id")));
    Item item;
    item.price = c->money(0);
    item.cost = c->money(0);
    if (id != 0) {
        const std::optional<Item> existing = lists.item(id);
        if (!existing) {
            return Format::failure(tr("That item no longer exists."));
        }
        item = *existing;
    }
    if (map.contains(QStringLiteral("name"))) {
        item.name = map.value(QStringLiteral("name")).toString().trimmed();
    }
    for (const char *key : {"kind", "type"}) {
        if (map.contains(QString::fromLatin1(key))) {
            item.kind = itemKindFrom(map.value(QString::fromLatin1(key)).toString());
        }
    }
    if (map.contains(QStringLiteral("description"))) {
        item.description = map.value(QStringLiteral("description")).toString();
    }
    if (map.contains(QStringLiteral("sku"))) {
        item.sku = map.value(QStringLiteral("sku")).toString().trimmed();
    }
    const MoneyFormat money = moneyFormat();
    for (const auto &[key, field] : {std::pair{"price", &item.price}, std::pair{"cost", &item.cost}}) {
        if (!map.contains(QString::fromLatin1(key))) {
            continue;
        }
        QString error;
        const std::optional<qint64> minor = money.parse(map.value(QString::fromLatin1(key)), &error);
        if (!minor) {
            return Format::failure(error);
        }
        *field = c->money(*minor);
    }
    if (map.contains(QStringLiteral("incomeAccountId"))) {
        item.incomeAccountId = Format::id(map.value(QStringLiteral("incomeAccountId")));
    }
    if (map.contains(QStringLiteral("expenseAccountId"))) {
        item.expenseAccountId = Format::id(map.value(QStringLiteral("expenseAccountId")));
    }
    if (map.contains(QStringLiteral("taxable"))) {
        item.taxable = map.value(QStringLiteral("taxable")).toBool();
    }
    const Result saved = id == 0 ? lists.createItem(item) : lists.updateItem(item);
    if (saved.ok && id != 0 && map.contains(QStringLiteral("active"))) {
        static_cast<void>(lists.setItemActive(id, map.value(QStringLiteral("active")).toBool()));
    }
    return saved.ok ? Format::success(id == 0 ? saved.id : id) : Format::result(saved);
}

} // namespace QindaOffice::Apps::Books
