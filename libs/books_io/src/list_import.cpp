// SPDX-License-Identifier: GPL-3.0-or-later
// CSV import of the chart of accounts, customers, vendors and items.
#include "ledger_export.h"

#include "accounts.h"
#include "bank_import.h"
#include "company_file.h"
#include "csv_util.h"
#include "lists.h"

#include <QHash>
#include <QRegularExpression>

namespace QindaOffice::Books {

namespace {

QString key(const QString &header)
{
    static const QRegularExpression junk(QStringLiteral("[^a-z0-9]+"));
    return header.toLower().remove(junk);
}

// Canonical field → accepted header keys (normalised).
const QHash<QString, QStringList> &synonyms()
{
    static const QHash<QString, QStringList> table{
        {QStringLiteral("name"), {QStringLiteral("name"), QStringLiteral("displayname"), QStringLiteral("customer"),
                                  QStringLiteral("vendor"), QStringLiteral("customername"), QStringLiteral("vendorname"),
                                  QStringLiteral("fullname"), QStringLiteral("account"), QStringLiteral("accountname"),
                                  QStringLiteral("item"), QStringLiteral("itemname"), QStringLiteral("productservice")}},
        {QStringLiteral("number"), {QStringLiteral("number"), QStringLiteral("accountnumber"), QStringLiteral("accnum"),
                                    QStringLiteral("acctnum"), QStringLiteral("no")}},
        {QStringLiteral("type"), {QStringLiteral("type"), QStringLiteral("accounttype"), QStringLiteral("accnttype"),
                                  QStringLiteral("itemtype")}},
        {QStringLiteral("description"), {QStringLiteral("description"), QStringLiteral("desc"), QStringLiteral("salesdescription")}},
        {QStringLiteral("active"), {QStringLiteral("active"), QStringLiteral("status")}},
        {QStringLiteral("company"), {QStringLiteral("company"), QStringLiteral("companyname")}},
        {QStringLiteral("contact"), {QStringLiteral("contact"), QStringLiteral("contactname")}},
        {QStringLiteral("email"), {QStringLiteral("email"), QStringLiteral("emailaddress")}},
        {QStringLiteral("phone"), {QStringLiteral("phone"), QStringLiteral("phonenumber"), QStringLiteral("telephone")}},
        {QStringLiteral("billing"), {QStringLiteral("billingaddress"), QStringLiteral("address"), QStringLiteral("billto"),
                                     QStringLiteral("billing")}},
        {QStringLiteral("shipping"), {QStringLiteral("shippingaddress"), QStringLiteral("shipto")}},
        {QStringLiteral("taxid"), {QStringLiteral("taxid"), QStringLiteral("ein"), QStringLiteral("vat"),
                                   QStringLiteral("taxnumber"), QStringLiteral("businessid")}},
        {QStringLiteral("terms"), {QStringLiteral("terms"), QStringLiteral("paymentterms")}},
        {QStringLiteral("notes"), {QStringLiteral("notes"), QStringLiteral("note"), QStringLiteral("memo")}},
        {QStringLiteral("1099"), {QStringLiteral("1099"), QStringLiteral("is1099"), QStringLiteral("track1099"),
                                  QStringLiteral("eligiblefor1099")}},
        {QStringLiteral("defaultaccount"), {QStringLiteral("defaultaccount"), QStringLiteral("defaultexpenseaccount")}},
        {QStringLiteral("sku"), {QStringLiteral("sku"), QStringLiteral("itemcode")}},
        {QStringLiteral("price"), {QStringLiteral("price"), QStringLiteral("salesprice"), QStringLiteral("rate"),
                                   QStringLiteral("salespricerate")}},
        {QStringLiteral("cost"), {QStringLiteral("cost"), QStringLiteral("purchasecost")}},
        {QStringLiteral("incomeaccount"), {QStringLiteral("incomeaccount")}},
        {QStringLiteral("expenseaccount"), {QStringLiteral("expenseaccount"), QStringLiteral("cogsaccount"),
                                            QStringLiteral("purchaseaccount")}},
        {QStringLiteral("taxable"), {QStringLiteral("taxable"), QStringLiteral("salestax")}},
    };
    return table;
}

bool truthy(const QString &value, bool fallback)
{
    const QString v = value.trimmed().toLower();
    if (v.isEmpty()) {
        return fallback;
    }
    return v == QLatin1String("yes") || v == QLatin1String("y") || v == QLatin1String("true") || v == QLatin1String("1")
        || v == QLatin1String("active") || v == QLatin1String("x");
}

std::optional<AccountType> parseAccountType(const QString &text)
{
    if (const std::optional<AccountType> direct = accountTypeFromKey(text)) {
        return direct;
    }
    const QString k = key(text);
    for (AccountType type : allAccountTypes()) {
        const AccountTypeInfo info = accountTypeInfo(type);
        const QString label = key(info.label.section(QLatin1Char('('), 0, 0));
        if (k == key(info.key) || k == label || k == label + QStringLiteral("s")
            || k == LedgerExport::iifAccountType(int(type)).toLower()) {
            return type;
        }
    }
    return std::nullopt;
}

} // namespace

ImportReport ListImport::importCsv(CompanyFile &file, ListKind kind, const QByteArray &csv)
{
    ImportReport report;
    const QString text = Csv::decode(csv);
    const QList<QStringList> rows = Csv::parse(text, Csv::detectDelimiter(text));
    if (rows.size() < 2) {
        report.errors << QStringLiteral("the file has no data rows");
        return report;
    }
    QHash<QString, int> column; // canonical field → index
    for (int i = 0; i < rows.first().size(); ++i) {
        const QString k = key(rows.first()[i]);
        for (auto it = synonyms().cbegin(); it != synonyms().cend(); ++it) {
            if (it.value().contains(k) && !column.contains(it.key())) {
                column.insert(it.key(), i);
            }
        }
    }
    if (!column.contains(QStringLiteral("name"))) {
        report.errors << QStringLiteral("no Name column");
        return report;
    }
    ChartOfAccounts chart(file);
    Lists lists(file);
    const int exponent = currencyInfo(file.currency()).exponent;
    for (int r = 1; r < rows.size(); ++r) {
        const QStringList &row = rows[r];
        auto get = [&](const QString &field) {
            const int c = column.value(field, -1);
            return c >= 0 && c < row.size() ? row[c].trimmed() : QString();
        };
        const QString name = get(QStringLiteral("name"));
        const QString where = QStringLiteral("row %1: ").arg(r + 1);
        if (name.isEmpty()) {
            continue;
        }
        Result created;
        switch (kind) {
        case ListKind::Accounts: {
            const std::optional<Account> existing = chart.account(chart.find(name));
            if (existing && existing->fullName.compare(name, Qt::CaseInsensitive) == 0) {
                ++report.skipped;
                continue;
            }
            const std::optional<AccountType> type = parseAccountType(get(QStringLiteral("type")));
            if (!type) {
                report.errors << where + QStringLiteral("unknown account type \"%1\"").arg(get(QStringLiteral("type")));
                continue;
            }
            Account a;
            a.type = *type;
            a.number = get(QStringLiteral("number"));
            a.description = get(QStringLiteral("description"));
            a.active = truthy(get(QStringLiteral("active")), true);
            const qsizetype colon = name.lastIndexOf(QLatin1Char(':'));
            a.name = colon < 0 ? name : name.mid(colon + 1).trimmed();
            if (colon >= 0) {
                a.parentId = chart.find(name.left(colon).trimmed());
                if (a.parentId == 0) {
                    report.errors << where + QStringLiteral("parent account \"%1\" not found").arg(name.left(colon));
                    continue;
                }
            }
            created = chart.create(a);
            break;
        }
        case ListKind::Customers:
        case ListKind::Vendors: {
            const PartyKind partyKind = kind == ListKind::Customers ? PartyKind::Customer : PartyKind::Vendor;
            if (lists.findParty(partyKind, name) != 0) {
                ++report.skipped;
                continue;
            }
            Party p;
            p.kind = partyKind;
            p.displayName = name;
            p.companyName = get(QStringLiteral("company"));
            p.contactName = get(QStringLiteral("contact"));
            p.email = get(QStringLiteral("email"));
            p.phone = get(QStringLiteral("phone"));
            p.billingAddress = get(QStringLiteral("billing"));
            p.shippingAddress = get(QStringLiteral("shipping"));
            p.taxId = get(QStringLiteral("taxid"));
            p.termsId = lists.findTerms(get(QStringLiteral("terms")));
            p.is1099 = truthy(get(QStringLiteral("1099")), false);
            p.defaultAccountId = chart.find(get(QStringLiteral("defaultaccount")));
            p.notes = get(QStringLiteral("notes"));
            p.active = truthy(get(QStringLiteral("active")), true);
            created = lists.createParty(p);
            break;
        }
        case ListKind::Items: {
            if (lists.findItem(name) != 0) {
                ++report.skipped;
                continue;
            }
            Item i;
            i.name = name;
            const QString type = key(get(QStringLiteral("type")));
            i.kind = type.startsWith(QLatin1String("non")) || type == QLatin1String("product")
                    || type == QLatin1String("part")
                ? ItemKind::NonInventory
                : (type.startsWith(QLatin1String("other")) || type == QLatin1String("othc")) ? ItemKind::OtherCharge
                                                                                              : ItemKind::Service;
            i.description = get(QStringLiteral("description"));
            i.sku = get(QStringLiteral("sku"));
            const QString price = get(QStringLiteral("price"));
            const QString cost = get(QStringLiteral("cost"));
            const std::optional<qint64> p = price.isEmpty() ? std::optional<qint64>(0) : BankImport::parseAmount(price, exponent);
            const std::optional<qint64> c = cost.isEmpty() ? std::optional<qint64>(0) : BankImport::parseAmount(cost, exponent);
            if (!p || !c) {
                report.errors << where + QStringLiteral("unreadable price or cost");
                continue;
            }
            i.price = file.money(*p);
            i.cost = file.money(*c);
            i.incomeAccountId = chart.find(get(QStringLiteral("incomeaccount")));
            i.expenseAccountId = chart.find(get(QStringLiteral("expenseaccount")));
            i.taxable = truthy(get(QStringLiteral("taxable")), false);
            i.active = truthy(get(QStringLiteral("active")), true);
            created = lists.createItem(i);
            break;
        }
        }
        if (created.ok) {
            ++report.added;
        } else {
            report.errors << where + created.error;
        }
    }
    return report;
}

} // namespace QindaOffice::Books
