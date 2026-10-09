// SPDX-License-Identifier: GPL-3.0-or-later
// A company's own payroll tax items (custom_items.h, D-081 amendment): a
// flat percentage of wages up to a wage base, an annual limit, the
// user-entered state withholding (a percentage of income tax wages and an
// extra amount), an exempt employee, half-up cents. Every expected value is
// worked by hand in the comment above it; the items are made up.
#include "custom_items.h"

#include <QTest>

using namespace QindaOffice::Books;
using namespace QindaOffice::Books::Payroll;

namespace {

Money m(const char *text)
{
    return parseUsd(QString::fromLatin1(text));
}

Decimal d(const char *text)
{
    return *Decimal::parse(QString::fromLatin1(text));
}

CustomItemInput tax(const char *rate, CustomItem::Side side, std::optional<Money> base = {}, std::optional<Money> limit = {})
{
    CustomItemInput in;
    in.item = CustomItem{7, QStringLiteral("Example state unemployment"), side, CustomItem::Kind::Tax, d(rate), base, limit};
    return in;
}

} // namespace

class TstPayrollCustom : public QObject {
    Q_OBJECT
private slots:
    // 2.7 % of $3,000 = 81.00; on gross less Section 125 ($3,000 here).
    void flatPercentage()
    {
        const CustomItemResult r = computeCustomItem(tax("0.027", CustomItem::Side::Employer), m("3000"), m("2700"));
        QCOMPARE(r.wages, m("3000"));
        QCOMPARE(r.amount, m("81"));
        QVERIFY(r.side == CustomItem::Side::Employer);
        QCOMPARE(worksheetAmount(r.worksheet, QStringLiteral("8")), m("81"));
    }

    // A $10,000 wage base with $9,000 already taxed this year: only $1,000
    // of a $3,000 check → 2.7 % × 1,000 = 27.00; past the base nothing.
    void wageBaseCrossedMidCheck()
    {
        CustomItemInput in = tax("0.027", CustomItem::Side::Employer, m("10000"));
        in.ytdWages = m("9000");
        CustomItemResult r = computeCustomItem(in, m("3000"), m("3000"));
        QCOMPARE(r.wages, m("1000"));
        QCOMPARE(r.amount, m("27"));
        in.ytdWages = m("10000");
        r = computeCustomItem(in, m("3000"), m("3000"));
        QCOMPARE(r.wages, m("0"));
        QCOMPARE(r.amount, m("0"));
    }

    // An annual limit of $50.00 with $45.00 taken: 1 % × 3,000 = 30.00 is
    // cut to the 5.00 left.
    void annualLimit()
    {
        CustomItemInput in = tax("0.01", CustomItem::Side::Employee, std::nullopt, m("50"));
        in.ytdAmount = m("45");
        QCOMPARE(computeCustomItem(in, m("3000"), m("3000")).amount, m("5"));
        in.ytdAmount = m("50");
        QCOMPARE(computeCustomItem(in, m("3000"), m("3000")).amount, m("0"));
    }

    // Withholding with no built-in table: the employee's 4 % of the income
    // tax wages ($2,700 after a $300 401(k) deferral) = 108.00, plus the
    // $5.00 extra they asked for = 113.00 — the item's own rate is unused.
    void userEnteredWithholding()
    {
        CustomItemInput in;
        in.item = CustomItem{9, QStringLiteral("Example state income tax"), CustomItem::Side::Employee,
                             CustomItem::Kind::Withholding, Decimal(), std::nullopt, std::nullopt};
        in.employeeRate = d("0.04");
        in.employeeExtra = m("5");
        const CustomItemResult r = computeCustomItem(in, m("3000"), m("2700"));
        QCOMPARE(r.wages, m("2700"));
        QCOMPARE(r.amount, m("113"));
        QVERIFY(r.kind == CustomItem::Kind::Withholding);
        // Only the extra amount: 5.00.
        in.employeeRate = Decimal();
        QCOMPARE(computeCustomItem(in, m("3000"), m("2700")).amount, m("5"));
    }

    // An exempt employee: nothing, and the worksheet says why.
    void exemptEmployee()
    {
        CustomItemInput in = tax("0.027", CustomItem::Side::Employer);
        in.exempt = true;
        const CustomItemResult r = computeCustomItem(in, m("3000"), m("3000"));
        QCOMPARE(r.amount, m("0"));
        QCOMPARE(r.wages, m("0"));
        QVERIFY(r.worksheet.first().text.contains(QStringLiteral("Not subject")));
    }

    // Half-up cents: 1.25 % × 333.33 = 4.166625 → 4.17; 0.5 % × 1.01 =
    // 0.00505 → 0.01.
    void halfUpCents()
    {
        QCOMPARE(computeCustomItem(tax("0.0125", CustomItem::Side::Employee), m("333.33"), m("333.33")).amount, m("4.17"));
        QCOMPARE(computeCustomItem(tax("0.005", CustomItem::Side::Employee), m("1.01"), m("1.01")).amount, m("0.01"));
    }

    void keys()
    {
        QCOMPARE(customSideKey(CustomItem::Side::Employee), QStringLiteral("employee"));
        QVERIFY(customSideFromKey(QStringLiteral("employer")) == CustomItem::Side::Employer);
        QCOMPARE(customKindKey(CustomItem::Kind::Withholding), QStringLiteral("withholding"));
        QVERIFY(!customKindFromKey(QStringLiteral("other")).has_value());
    }
};

QTEST_GUILESS_MAIN(TstPayrollCustom)
#include "tst_payroll_custom.moc"
