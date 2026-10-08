// SPDX-License-Identifier: GPL-3.0-or-later
// BooksService (M63, D-071): budget entry for the Budget vs. Actual report —
// an amount per income and expense account per month of a fiscal year
// (books_engine Budgets). The dialog edits text; every split and sum is
// done here, exactly, so QML never adds money.
//
// AGENT-CONTRACT (QML, BudgetDialog.qml): budget(fiscalYear) rows are
// {accountId, name, number, section ("income" | "expense"), months[12]
// (plain text, "" for zero), annual, annualText}; monthLabels[12] name the
// fiscal months. Budgets are positive amounts in the account's normal sign
// (an income budget of 1,000.00 is income of 1,000.00).
#include "books_service.h"

#include "accounts.h"
#include "company_file.h"
#include "reports.h"

namespace QindaOffice::Apps::Books {

using namespace ::QindaOffice::Books;

int BooksService::currentFiscalYear() const
{
    const QDate today = todayDate();
    return file() != nullptr ? file()->settings().fiscalYearStart(today).year() : today.year();
}

QVariantMap BooksService::budget(int fiscalYear) const
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    if (fiscalYear < 1900 || fiscalYear > 9999) {
        fiscalYear = currentFiscalYear();
    }
    const MoneyFormat money = moneyFormat();
    const QDate first(fiscalYear, c->settings().fiscalYearStartMonth, 1);
    QStringList labels;
    for (int i = 0; i < 12; ++i) {
        labels << money.locale.toString(first.addMonths(i), QStringLiteral("MMM yyyy"));
    }
    Budgets budgets(*c);
    QVariantList rows;
    for (const Account &a : ChartOfAccounts(*c).accounts(false)) {
        const AccountTypeInfo info = accountTypeInfo(a.type);
        if (info.isBalanceSheet()) {
            continue;
        }
        const QList<qint64> months = budgets.fiscalYearMonths(a.id, fiscalYear);
        QVariantList texts;
        qint64 annual = 0;
        for (qint64 m : months) {
            texts << (m == 0 ? QString() : money.plain(m));
            annual += m;
        }
        rows << QVariantMap{{QStringLiteral("accountId"), a.id},
                            {QStringLiteral("name"), a.fullName},
                            {QStringLiteral("number"), a.number},
                            {QStringLiteral("depth"), a.depth},
                            {QStringLiteral("section"), info.accountClass == AccountClass::Income ? QStringLiteral("income")
                                                                                                  : QStringLiteral("expense")},
                            {QStringLiteral("months"), texts},
                            {QStringLiteral("annual"), annual},
                            {QStringLiteral("annualText"), annual == 0 ? QString() : money.plain(annual)}};
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("fiscalYear"), fiscalYear);
    out.insert(QStringLiteral("monthLabels"), labels);
    out.insert(QStringLiteral("rows"), rows);
    out.insert(QStringLiteral("from"), Format::isoDate(first));
    out.insert(QStringLiteral("to"), Format::isoDate(first.addYears(1).addDays(-1)));
    return out;
}

QVariantMap BooksService::saveBudget(const QVariantMap &map)
{
    CompanyFile *c = file();
    if (c == nullptr) {
        return noCompany();
    }
    const int fiscalYear = map.value(QStringLiteral("fiscalYear")).toInt();
    if (fiscalYear < 1900 || fiscalYear > 9999) {
        return Format::failure(tr("Choose the fiscal year the budget is for."));
    }
    const MoneyFormat money = moneyFormat();
    // Parse everything first: one unreadable amount saves nothing.
    QList<QPair<qint64, QList<qint64>>> parsed;
    for (const QVariant &v : map.value(QStringLiteral("rows")).toList()) {
        const QVariantMap row = v.toMap();
        const qint64 account = Format::id(row.value(QStringLiteral("accountId")));
        const QVariantList texts = row.value(QStringLiteral("months")).toList();
        if (texts.size() != 12) {
            return Format::failure(tr("Each budget row needs twelve months."));
        }
        QList<qint64> months;
        for (int i = 0; i < 12; ++i) {
            QString error;
            const std::optional<qint64> minor = money.parse(texts.at(i), &error);
            if (!minor) {
                return Format::failure(tr("%1, month %2: %3").arg(nameOfAccount(account)).arg(i + 1).arg(error));
            }
            months << *minor;
        }
        parsed << qMakePair(account, months);
    }
    SqlTransaction txn(*c);
    Budgets budgets(*c);
    for (const auto &[account, months] : std::as_const(parsed)) {
        if (const Result r = budgets.setFiscalYearMonths(account, fiscalYear, months); !r.ok) {
            return Format::result(r);
        }
    }
    if (!txn.commit()) {
        return Format::failure(txn.error());
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("saved"), int(parsed.size()));
    return out;
}

QVariantMap BooksService::budgetSpread(const QString &annualText) const
{
    QString error;
    const std::optional<qint64> annual = moneyFormat().parse(annualText, &error);
    if (!annual) {
        return Format::failure(error);
    }
    const MoneyFormat money = moneyFormat();
    QVariantList months;
    // Largest remainder: the twelve parts add up to the year exactly.
    for (const Money &m : money.money(*annual).split(12)) {
        months << (m.minor() == 0 ? QString() : money.plain(m.minor()));
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("months"), months);
    out.insert(QStringLiteral("annualText"), *annual == 0 ? QString() : money.plain(*annual));
    return out;
}

QVariantMap BooksService::budgetTotal(const QVariantList &monthTexts) const
{
    const MoneyFormat money = moneyFormat();
    qint64 total = 0;
    for (const QVariant &v : monthTexts) {
        QString error;
        const std::optional<qint64> minor = money.parse(v, &error);
        if (!minor) {
            return Format::failure(error);
        }
        total += *minor;
    }
    QVariantMap out = Format::success();
    out.insert(QStringLiteral("total"), total);
    out.insert(QStringLiteral("totalText"), total == 0 ? QString() : money.plain(total));
    return out;
}

} // namespace QindaOffice::Apps::Books
