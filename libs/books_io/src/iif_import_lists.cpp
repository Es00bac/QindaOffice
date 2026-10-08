// SPDX-License-Identifier: GPL-3.0-or-later
// IIF import, list half (iif_import.h): ACCNT, CUST, VEND, INVITEM and
// CLASS rows through the engine's validating, audited APIs — a name the
// company already has is left as it is.
#include "iif_import_p.h"

#include "accounts.h"
#include "bank_import.h"
#include "company_file.h"
#include "lists.h"

#include <QRegularExpression>

namespace QindaOffice::Books::IifInternal {

namespace {

QString lower(const QString &s)
{
    return s.trimmed().toLower();
}

bool yes(const QString &value)
{
    const QString v = value.trimmed().toUpper();
    return v == QLatin1String("Y") || v == QLatin1String("YES") || v == QLatin1String("TRUE") || v == QLatin1String("1");
}

// Address lines 1–5 of `prefix`, without a first line that only repeats the
// name or company (QuickBooks writes the addressee there; so do we).
QString address(const IifRecord &r, const QString &prefix, const QStringList &names)
{
    QStringList lines;
    for (int i = 1; i <= 5; ++i) {
        const QString line = r.value(prefix + QString::number(i));
        if (line.isEmpty()) {
            continue;
        }
        if (lines.isEmpty() && i == 1 && names.contains(line.toLower())) {
            continue;
        }
        lines << line;
    }
    return lines.join(QLatin1Char('\n'));
}

qint64 termsFor(Context &ctx, const QString &name)
{
    if (name.isEmpty()) {
        return 0;
    }
    Lists lists(ctx.file);
    if (const qint64 id = lists.findTerms(name)) {
        return id;
    }
    static const QRegularExpression net(QStringLiteral("^net\\s*(\\d{1,3})$"), QRegularExpression::CaseInsensitiveOption);
    if (const QRegularExpressionMatch m = net.match(name.trimmed()); m.hasMatch()) {
        const Result r = lists.createTerms({0, name.trimmed(), m.captured(1).toInt(), true});
        if (r.ok) {
            return r.id;
        }
    }
    ctx.report.note(QStringLiteral("Payment terms QindaBooks does not know (\"%1\") were left to the company default.").arg(name));
    return 0;
}

// Creates "A:B" (and "A") with `type` when missing; the id of the full name.
qint64 ensureAccount(Context &ctx, const QString &fullName, AccountType type, int *added)
{
    if (const qint64 id = ctx.accounts.value(lower(fullName))) {
        return id;
    }
    const qsizetype colon = fullName.lastIndexOf(QLatin1Char(':'));
    Account a;
    a.type = type;
    a.name = (colon < 0 ? fullName : fullName.mid(colon + 1)).trimmed();
    if (colon >= 0) {
        a.parentId = ensureAccount(ctx, fullName.left(colon), type, added);
        if (a.parentId == 0) {
            return 0;
        }
    }
    const Result r = ChartOfAccounts(ctx.file).create(a);
    if (!r.ok) {
        ctx.report.note(QStringLiteral("Account \"%1\" was not created: %2.").arg(fullName, r.error));
        return 0;
    }
    ctx.accounts.insert(lower(fullName), r.id);
    ++*added;
    return r.id;
}

void importAccount(Context &ctx, const IifRecord &r)
{
    const QString name = r.value(QStringLiteral("NAME"));
    if (name.isEmpty()) {
        return;
    }
    if (ctx.accounts.contains(lower(name))) {
        ++ctx.report.accounts.existing;
        return;
    }
    const QString keyword = r.value(QStringLiteral("ACCNTTYPE")).toUpper();
    const int type = IifImport::accountTypeFromIif(keyword);
    if (keyword == QLatin1String("NONPOSTING")) {
        ++ctx.report.accounts.skipped;
        ctx.report.note(QStringLiteral("Non-posting accounts (estimates, purchase orders) are not imported."));
        return;
    }
    // QuickBooks' A/R, A/P, Undeposited Funds, … are this company's own.
    if (const QString role = systemRoleFor(name, type, r.value(QStringLiteral("EXTRA"))); !role.isEmpty()) {
        if (const qint64 id = ctx.roleAccount(role)) {
            ctx.accounts.insert(lower(name), id);
            ++ctx.report.accounts.existing;
            return;
        }
    }
    if (type < 0) {
        ++ctx.report.accounts.skipped;
        ctx.report.note(QStringLiteral("Account \"%1\" has the unknown type \"%2\" and was not imported.").arg(name, keyword));
        return;
    }
    ChartOfAccounts chart(ctx.file);
    const qsizetype colon = name.lastIndexOf(QLatin1Char(':'));
    Account a;
    a.type = AccountType(type);
    a.name = (colon < 0 ? name : name.mid(colon + 1)).trimmed();
    a.description = r.value(QStringLiteral("DESC"));
    a.number = r.value(QStringLiteral("ACCNUM"));
    if (colon >= 0) {
        a.parentId = ensureAccount(ctx, name.left(colon), a.type, &ctx.report.accounts.added);
    }
    if (!a.number.isEmpty() && chart.find(a.number) != 0) {
        ctx.report.note(QStringLiteral("Account numbers this company already uses were left off the imported accounts (%1).")
                            .arg(a.number));
        a.number.clear();
    }
    const Result created = chart.create(a);
    if (!created.ok) {
        ++ctx.report.accounts.skipped;
        ctx.report.note(QStringLiteral("line %1: account \"%2\" was not imported: %3.").arg(r.line).arg(name, created.error));
        return;
    }
    ctx.accounts.insert(lower(name), created.id);
    ++ctx.report.accounts.added;
    if (yes(r.value(QStringLiteral("HIDDEN")))) {
        ctx.hiddenAccounts << created.id;
    }
    if (const std::optional<qint64> ob = BankImport::parseAmount(r.value(QStringLiteral("OBAMOUNT")), ctx.exponent);
        ob && *ob != 0) {
        ctx.report.note(QStringLiteral("Opening balances (OBAMOUNT) are not imported: balances come from the "
                                       "transactions, or from an opening journal entry."));
    }
}

void importParty(Context &ctx, const IifRecord &r, PartyKind kind)
{
    const QString name = r.value(QStringLiteral("NAME"));
    if (name.isEmpty()) {
        return;
    }
    const bool customer = kind == PartyKind::Customer;
    IifListCount &count = customer ? ctx.report.customers : ctx.report.vendors;
    QHash<QString, qint64> &known = customer ? ctx.customers : ctx.vendors;
    if (known.contains(lower(name))) {
        ++count.existing;
        return;
    }
    Party p;
    p.kind = kind;
    p.displayName = name;
    p.companyName = r.value(QStringLiteral("COMPANYNAME"));
    p.contactName = r.value(QStringLiteral("CONT1"));
    if (p.contactName.isEmpty()) {
        p.contactName = QStringList{r.value(QStringLiteral("FIRSTNAME")), r.value(QStringLiteral("LASTNAME"))}
                            .join(QLatin1Char(' '))
                            .trimmed();
    }
    p.email = r.value(QStringLiteral("EMAIL"));
    p.phone = r.value(QStringLiteral("PHONE1"));
    const QStringList names{name.toLower(), p.companyName.toLower()};
    p.billingAddress = address(r, customer ? QStringLiteral("BADDR") : QStringLiteral("ADDR"), names);
    if (customer) {
        p.shippingAddress = address(r, QStringLiteral("SADDR"), names);
    } else {
        p.taxId = r.value(QStringLiteral("TAXID"));
        p.is1099 = yes(r.value(QStringLiteral("1099")));
    }
    p.termsId = termsFor(ctx, r.value(QStringLiteral("TERMS")));
    p.notes = r.value(QStringLiteral("NOTE"));
    p.active = !yes(r.value(QStringLiteral("HIDDEN")));
    const Result created = Lists(ctx.file).createParty(p);
    if (!created.ok) {
        ++count.skipped;
        ctx.report.note(QStringLiteral("line %1: \"%2\" was not imported: %3.").arg(r.line).arg(name, created.error));
        return;
    }
    known.insert(lower(name), created.id);
    ++count.added;
}

void importItem(Context &ctx, const IifRecord &r)
{
    const QString name = r.value(QStringLiteral("NAME"));
    if (name.isEmpty()) {
        return;
    }
    if (ctx.items.contains(lower(name))) {
        ++ctx.report.items.existing;
        return;
    }
    // Intuit's INVITEMTYPE keywords. NONINV is what QindaBooks wrote for
    // a non-inventory part before M63; PART is QuickBooks' own spelling.
    const QString type = r.value(QStringLiteral("INVITEMTYPE")).toUpper();
    Item item;
    item.name = name;
    if (type == QLatin1String("SERV")) {
        item.kind = ItemKind::Service;
    } else if (type == QLatin1String("PART") || type == QLatin1String("NONINV")) {
        item.kind = ItemKind::NonInventory;
    } else if (type == QLatin1String("INVENTORY") || type == QLatin1String("ASSEMBLY") || type == QLatin1String("INVASSEMBLY")) {
        item.kind = ItemKind::NonInventory;
        ctx.report.note(QStringLiteral("Inventory items were imported as non-inventory products: QindaBooks does not "
                                       "track quantities on hand."));
    } else if (type == QLatin1String("OTHC")) {
        item.kind = ItemKind::OtherCharge;
    } else {
        ++ctx.report.items.skipped;
        ctx.report.note(QStringLiteral("Discount, subtotal, group, payment and sales-tax items are not imported; "
                                       "their amounts arrive through the transactions' accounts."));
        return;
    }
    item.description = r.value(QStringLiteral("DESC"));
    if (item.description.isEmpty()) {
        item.description = r.value(QStringLiteral("PURCHASEDESC"));
    }
    item.incomeAccountId = ctx.account(r.value(QStringLiteral("ACCNT")));
    for (const char *field : {"COGSACCNT", "EXPACCNT", "EXPENSEACCNT"}) {
        if (item.expenseAccountId == 0) {
            item.expenseAccountId = ctx.account(r.value(QString::fromLatin1(field)));
        }
    }
    const std::optional<qint64> price = BankImport::parseAmount(r.value(QStringLiteral("PRICE")), ctx.exponent);
    const std::optional<qint64> cost = BankImport::parseAmount(r.value(QStringLiteral("COST")), ctx.exponent);
    item.price = ctx.file.money(price.value_or(0));
    item.cost = ctx.file.money(cost.value_or(0));
    item.taxable = yes(r.value(QStringLiteral("TAXABLE")));
    item.active = !yes(r.value(QStringLiteral("HIDDEN")));
    const Result created = Lists(ctx.file).createItem(item);
    if (!created.ok) {
        ++ctx.report.items.skipped;
        ctx.report.note(QStringLiteral("line %1: item \"%2\" was not imported: %3.").arg(r.line).arg(name, created.error));
        return;
    }
    ctx.items.insert(lower(name), created.id);
    ++ctx.report.items.added;
}

} // namespace

QString systemRoleFor(const QString &name, int accountType, const QString &extra)
{
    const QString n = lower(name);
    const QString x = extra.trimmed().toUpper();
    // AGENT-NOTE: only QuickBooks' own A/R and A/P (by name) become the
    // system accounts; a second A/R account stays an account of its own,
    // and what posts to it imports as journal entries — exact either way.
    const bool anyType = accountType < 0;
    if ((anyType || accountType == int(AccountType::AccountsReceivable))
        && (n == QLatin1String("accounts receivable") || n == QLatin1String("a/r"))) {
        return SystemRole::AccountsReceivable;
    }
    if ((anyType || accountType == int(AccountType::AccountsPayable))
        && (n == QLatin1String("accounts payable") || n == QLatin1String("a/p"))) {
        return SystemRole::AccountsPayable;
    }
    if (n == QLatin1String("undeposited funds") || x.startsWith(QLatin1String("UNDEP"))) {
        return SystemRole::UndepositedFunds;
    }
    if (n == QLatin1String("opening bal equity") || n == QLatin1String("opening balance equity")
        || x.startsWith(QLatin1String("OPENBAL"))) {
        return SystemRole::OpeningBalanceEquity;
    }
    if (n == QLatin1String("retained earnings") || x.startsWith(QLatin1String("RETEARN"))) {
        return SystemRole::RetainedEarnings;
    }
    if (n == QLatin1String("sales tax payable") || x == QLatin1String("SALESTAX")) {
        return SystemRole::SalesTaxPayable;
    }
    return QString();
}

Context::Context(CompanyFile &f, const IifImportOptions &o, IifImportReport &r)
    : file(f)
    , options(o)
    , report(r)
    , exponent(currencyInfo(f.currency()).exponent)
{
    for (const Account &a : ChartOfAccounts(file).accounts(true)) {
        accounts.insert(lower(a.fullName), a.id);
    }
    Lists lists(file);
    for (const Party &p : lists.parties(PartyKind::Customer, true)) {
        customers.insert(lower(p.displayName), p.id);
    }
    for (const Party &p : lists.parties(PartyKind::Vendor, true)) {
        vendors.insert(lower(p.displayName), p.id);
    }
    for (const Item &i : lists.items(true)) {
        items.insert(lower(i.name), i.id);
    }
    for (const ClassTag &c : lists.classes(true)) {
        classes.insert(lower(c.name), c.id);
    }
}

qint64 Context::roleAccount(const QString &role) const
{
    return ChartOfAccounts(file).idForRole(role);
}

qint64 Context::account(const QString &name) const
{
    if (name.trimmed().isEmpty()) {
        return 0;
    }
    if (const qint64 mapped = options.accountMap.value(name.trimmed())) {
        return mapped;
    }
    if (const qint64 id = accounts.value(lower(name))) {
        return id;
    }
    const QString role = systemRoleFor(name, -1, {});
    return role.isEmpty() ? 0 : roleAccount(role);
}

qint64 Context::customer(const QString &name, bool create)
{
    if (name.trimmed().isEmpty()) {
        return 0;
    }
    if (const qint64 id = customers.value(lower(name))) {
        return id;
    }
    if (!create) {
        return 0;
    }
    Party p;
    p.kind = PartyKind::Customer;
    p.displayName = name.trimmed();
    const Result r = Lists(file).createParty(p);
    if (r.ok) {
        customers.insert(lower(name), r.id);
        ++report.customers.added;
    }
    return r.ok ? r.id : 0;
}

qint64 Context::vendor(const QString &name, bool create)
{
    if (name.trimmed().isEmpty()) {
        return 0;
    }
    if (const qint64 id = vendors.value(lower(name))) {
        return id;
    }
    if (!create) {
        return 0;
    }
    Party p;
    p.kind = PartyKind::Vendor;
    p.displayName = name.trimmed();
    const Result r = Lists(file).createParty(p);
    if (r.ok) {
        vendors.insert(lower(name), r.id);
        ++report.vendors.added;
    }
    return r.ok ? r.id : 0;
}

qint64 Context::anyParty(const QString &name) const
{
    const QString n = lower(name);
    return customers.contains(n) ? customers.value(n) : vendors.value(n);
}

qint64 Context::klass(const QString &name)
{
    if (name.trimmed().isEmpty()) {
        return 0;
    }
    if (const qint64 id = classes.value(lower(name))) {
        return id;
    }
    const Result r = Lists(file).createClass({0, name.trimmed(), 0, true});
    if (r.ok) {
        classes.insert(lower(name), r.id);
        ++report.classes.added;
    }
    return r.ok ? r.id : 0;
}

void importLists(Context &ctx, const QList<IifRecord> &rows)
{
    QHash<QString, int> other;
    // Accounts first: items and transactions refer to them.
    for (const IifRecord &r : rows) {
        if (r.type == QLatin1String("ACCNT")) {
            importAccount(ctx, r);
        }
    }
    for (const IifRecord &r : rows) {
        if (r.type == QLatin1String("CUST")) {
            importParty(ctx, r, PartyKind::Customer);
        } else if (r.type == QLatin1String("VEND")) {
            importParty(ctx, r, PartyKind::Vendor);
        } else if (r.type == QLatin1String("INVITEM")) {
            importItem(ctx, r);
        } else if (r.type == QLatin1String("CLASS")) {
            if (ctx.classes.contains(lower(r.value(QStringLiteral("NAME"))))) {
                ++ctx.report.classes.existing;
            } else {
                ctx.klass(r.value(QStringLiteral("NAME")));
            }
        } else if (r.type != QLatin1String("ACCNT")) {
            ++other[r.type];
        }
    }
    for (auto it = other.cbegin(); it != other.cend(); ++it) {
        ctx.report.note(QStringLiteral("%1 %2 row(s) were not imported (QindaBooks keeps no such list).")
                            .arg(it.value())
                            .arg(it.key()));
    }
}

void deactivateHidden(Context &ctx)
{
    ChartOfAccounts chart(ctx.file);
    for (qint64 id : std::as_const(ctx.hiddenAccounts)) {
        if (!chart.setActive(id, false).ok) {
            ctx.report.note(QStringLiteral("Hidden accounts that still hold a balance stay active."));
        }
    }
}

} // namespace QindaOffice::Books::IifInternal

namespace QindaOffice::Books {

int IifImport::accountTypeFromIif(const QString &keyword)
{
    static const QHash<QString, AccountType> types{
        {QStringLiteral("BANK"), AccountType::Bank},
        {QStringLiteral("AR"), AccountType::AccountsReceivable},
        {QStringLiteral("OCASSET"), AccountType::OtherCurrentAsset},
        {QStringLiteral("FIXASSET"), AccountType::FixedAsset},
        {QStringLiteral("OASSET"), AccountType::OtherAsset},
        {QStringLiteral("AP"), AccountType::AccountsPayable},
        {QStringLiteral("CCARD"), AccountType::CreditCard},
        {QStringLiteral("OCLIAB"), AccountType::OtherCurrentLiability},
        {QStringLiteral("LTLIAB"), AccountType::LongTermLiability},
        {QStringLiteral("EQUITY"), AccountType::Equity},
        {QStringLiteral("INC"), AccountType::Income},
        {QStringLiteral("EXINC"), AccountType::OtherIncome},
        {QStringLiteral("COGS"), AccountType::CostOfGoodsSold},
        {QStringLiteral("EXP"), AccountType::Expense},
        {QStringLiteral("EXEXP"), AccountType::OtherExpense},
    };
    const QString k = keyword.trimmed().toUpper();
    return types.contains(k) ? int(types.value(k)) : -1;
}

} // namespace QindaOffice::Books
