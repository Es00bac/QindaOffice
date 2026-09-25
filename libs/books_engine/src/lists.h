// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The name lists every form picks from: customers and vendors (parties),
// products and services (items), classes/tags, payment terms.
//
// Inventory quantities are OUT OF SCOPE (docs/books.md): an item is a
// service, a non-inventory product or an other charge, with a sales price,
// a purchase cost and the accounts they post to. Nothing tracks stock on
// hand or computes COGS from quantities.

#include "books_types.h"
#include "money.h"

#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Books {

class CompanyFile;

struct Party {
    qint64 id = 0;
    PartyKind kind = PartyKind::Customer;
    QString displayName;
    QString companyName;
    QString contactName;
    QString email;
    QString phone;
    QString billingAddress;
    QString shippingAddress;
    QString taxId;
    qint64 termsId = 0;
    bool is1099 = false;          // vendors: report payments on a 1099
    qint64 defaultAccountId = 0;  // vendors: default expense account
    qint64 taxRateId = 0;         // customers: default sales tax rate
    QString notes;
    bool active = true;
};

enum class ItemKind { Service = 1, NonInventory = 2, OtherCharge = 3 };

struct Item {
    qint64 id = 0;
    QString name;
    ItemKind kind = ItemKind::Service;
    QString description;
    QString sku;
    Money price;                  // sales price per unit
    Money cost;                   // purchase cost per unit
    qint64 incomeAccountId = 0;
    qint64 expenseAccountId = 0;  // expense or COGS account for purchases
    bool taxable = false;
    bool active = true;
};

struct ClassTag {
    qint64 id = 0;
    QString name;
    qint64 parentId = 0;
    bool active = true;
};

struct Terms {
    qint64 id = 0;
    QString name;
    int dueDays = 0;
    bool active = true;
};

class Lists {
public:
    explicit Lists(CompanyFile &file);

    // ---- parties ----
    Result createParty(const Party &party);
    Result updateParty(const Party &party);
    // Parties are never deleted once referenced; deactivate instead.
    Result setPartyActive(qint64 id, bool active);
    [[nodiscard]] std::optional<Party> party(qint64 id) const;
    [[nodiscard]] QList<Party> parties(PartyKind kind, bool includeInactive = false) const;
    [[nodiscard]] qint64 findParty(PartyKind kind, const QString &displayName) const;

    // ---- items ----
    Result createItem(const Item &item);
    Result updateItem(const Item &item);
    Result setItemActive(qint64 id, bool active);
    [[nodiscard]] std::optional<Item> item(qint64 id) const;
    [[nodiscard]] QList<Item> items(bool includeInactive = false) const;
    [[nodiscard]] qint64 findItem(const QString &name) const;

    // ---- classes / tags ----
    Result createClass(const ClassTag &tag);
    [[nodiscard]] QList<ClassTag> classes(bool includeInactive = false) const;
    [[nodiscard]] qint64 findClass(const QString &name) const;

    // ---- terms ----
    Result createTerms(const Terms &terms);
    [[nodiscard]] QList<Terms> terms(bool includeInactive = false) const;
    [[nodiscard]] qint64 findTerms(const QString &name) const;

private:
    CompanyFile &m_file;
};

} // namespace QindaOffice::Books
