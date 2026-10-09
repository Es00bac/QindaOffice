// SPDX-License-Identifier: GPL-3.0-or-later
// Owner pay (M71, D-081; owner_pay.h): a Schedule C owner's draws — a
// printed check through the check queue or a transfer — the federal and
// Colorado estimated-tax payments posted as draws, the set-aside to a tax
// savings account, an S corporation's distributions and the draw it may
// not take, voids, and the planner's view of the payments.
#include "books_fixture.h"

#include "check_queue.h"
#include "documents.h"
#include "owner_pay.h"
#include "payroll_records.h"

using namespace BooksTest;

namespace {

struct OwnerCompany : TestCompany {
    qint64 bank = 0;
    qint64 savings = 0;
    qint64 owner = 0;

    explicit OwnerCompany(EntityTax entity = EntityTax::ScheduleC)
    {
        bank = account(QStringLiteral("1000"));
        Account s;
        s.name = QStringLiteral("Tax Savings");
        s.type = AccountType::Bank;
        savings = ChartOfAccounts(f()).create(s).id;
        PayrollRecords records(f());
        PayrollSetup setup = records.setup();
        setup.entity = entity;
        setup.payrollBankAccountId = bank;
        setup.setAsideAccountId = savings;
        records.saveSetup(setup);
        records.ensurePayrollAccounts();
        Party p;
        p.kind = PartyKind::Owner;
        p.displayName = QStringLiteral("Pat Owner");
        p.billingAddress = QStringLiteral("9 Aspen Court\nGolden, CO 80401");
        owner = Lists(f()).createParty(p).id;
    }
    qint64 draws() { return PayrollRecords(f()).setup().account(PayrollRole::OwnerDraws); }
    OwnerPaymentRequest request(OwnerPaymentKind kind, qint64 cents, const QDate &date)
    {
        OwnerPaymentRequest r;
        r.kind = kind;
        r.ownerId = owner;
        r.amount = usd(cents);
        r.date = date;
        return r;
    }
};

} // namespace

class TstBooksOwnerPay : public QObject {
    Q_OBJECT
private slots:
    void drawByCheckAndByTransfer()
    {
        OwnerCompany c;
        OwnerPay pay(c.f());
        OwnerPaymentRequest check = c.request(OwnerPaymentKind::Draw, 250000, QDate(2026, 10, 1));
        check.method = OwnerPaymentRequest::Method::Check;
        check.toPrint = true;
        const Result printed = pay.record(check);
        QVERIFY2(printed.ok, qPrintable(printed.error));
        const std::optional<BusinessDocument> doc = Documents(c.f()).load(printed.id);
        QCOMPARE(doc->kind, DocKind::Check);
        QCOMPARE(doc->partyId, c.owner);
        QCOMPARE(doc->lines.first().accountId, c.draws());
        QVERIFY(doc->toPrint);
        QCOMPARE(CheckQueue(c.f()).queue(c.bank).size(), 1); // M68's queue prints it
        const Result moved = pay.record(c.request(OwnerPaymentKind::Draw, 100000, QDate(2026, 10, 15)));
        QVERIFY2(moved.ok, qPrintable(moved.error));
        QCOMPARE(Documents(c.f()).load(moved.id)->kind, DocKind::Transfer);
        // Owner Draws is equity (credit-normal): draws show as −3,500.00.
        QCOMPARE(ChartOfAccounts(c.f()).balance(c.draws()).minor(), -350000);
        QCOMPARE(ChartOfAccounts(c.f()).balance(c.bank).minor(), -350000);
        QCOMPARE(pay.payments({}, {}, OwnerPaymentKind::Draw).size(), 2);
        QCOMPARE(pay.payments().first().ownerName, QStringLiteral("Pat Owner"));
        QVERIFY(c.f().integrityProblems().isEmpty());
    }

    // Estimated taxes are the owner's personal taxes: an electronic payment
    // debiting Owner Draws, with the tax, year and installment in the memo.
    void estimatedPaymentsPostAsDraws()
    {
        OwnerCompany c;
        OwnerPay pay(c.f());
        OwnerPaymentRequest federal = c.request(OwnerPaymentKind::FederalEstimate, 374568, QDate(2026, 9, 15));
        federal.taxYear = 2026;
        federal.installment = 3;
        federal.reference = QStringLiteral("EFTPS 4401");
        const Result r = pay.record(federal);
        QVERIFY2(r.ok, qPrintable(r.error));
        const BusinessDocument doc = *Documents(c.f()).load(r.id);
        QCOMPARE(doc.kind, DocKind::Expense);
        QCOMPARE(doc.lines.first().accountId, c.draws());
        QVERIFY(doc.memo.contains(QStringLiteral("Federal estimated tax 2026, payment 3 of 4")));
        OwnerPaymentRequest colorado = c.request(OwnerPaymentKind::ColoradoEstimate, 35900, QDate(2026, 9, 15));
        colorado.taxYear = 2026;
        colorado.installment = 3;
        QVERIFY(pay.record(colorado).ok);
        const QList<Payroll::EstimatedPayment> made = pay.estimatedPayments(2026);
        QCOMPARE(made.size(), 2);
        QCOMPARE(made.first().jurisdiction, Payroll::EstimatedPayment::Jurisdiction::Federal);
        QCOMPARE(made.first().amount, usd(374568));
        QVERIFY(pay.estimatedPayments(2025).isEmpty());
        OwnerPaymentRequest noYear = c.request(OwnerPaymentKind::FederalEstimate, 100, QDate(2026, 9, 15));
        QCOMPARE(pay.record(noYear).code, ErrorCode::Invalid);
        QCOMPARE(ChartOfAccounts(c.f()).balance(c.draws()).minor(), -(374568 + 35900));
    }

    void setAsideMovesMoneyToSavings()
    {
        OwnerCompany c;
        const Result r = OwnerPay(c.f()).record(c.request(OwnerPaymentKind::SetAside, 60000, QDate(2026, 10, 2)));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(ChartOfAccounts(c.f()).balance(c.savings).minor(), 60000);
        QCOMPARE(ChartOfAccounts(c.f()).balance(c.bank).minor(), -60000);
        QCOMPARE(ChartOfAccounts(c.f()).balance(c.draws()).minor(), 0); // still the business's money
    }

    // An S corporation pays its owner wages and distributions, never draws.
    void sCorporationDistributions()
    {
        OwnerCompany c(EntityTax::SCorporation);
        OwnerPay pay(c.f());
        QCOMPARE(pay.record(c.request(OwnerPaymentKind::Draw, 100000, QDate(2026, 10, 1))).code, ErrorCode::Invalid);
        const Result d = pay.record(c.request(OwnerPaymentKind::Distribution, 500000, QDate(2026, 10, 1)));
        QVERIFY2(d.ok, qPrintable(d.error));
        const qint64 distributions = PayrollRecords(c.f()).setup().account(PayrollRole::Distributions);
        QCOMPARE(ChartOfAccounts(c.f()).balance(distributions).minor(), -500000);
        QVERIFY(pay.voidPayment(d.id, QStringLiteral("Wrong amount")).ok);
        QCOMPARE(ChartOfAccounts(c.f()).balance(distributions).minor(), 0);
        QVERIFY(pay.payments().isEmpty());
        QCOMPARE(pay.payments({}, {}, {}, true).size(), 1);
    }

    void refusals()
    {
        OwnerCompany c;
        OwnerPay pay(c.f());
        QCOMPARE(pay.record(c.request(OwnerPaymentKind::Draw, 0, QDate(2026, 10, 1))).code, ErrorCode::Invalid);
        OwnerPaymentRequest toVendor = c.request(OwnerPaymentKind::Draw, 100, QDate(2026, 10, 1));
        toVendor.ownerId = c.vendor(QStringLiteral("Not An Owner"));
        QCOMPARE(pay.record(toVendor).code, ErrorCode::UnknownParty);
        QCOMPARE(pay.voidPayment(424242, QStringLiteral("x")).code, ErrorCode::NotFound);
        // A Schedule C owner takes draws, never shareholder distributions.
        QCOMPARE(pay.record(c.request(OwnerPaymentKind::Distribution, 100, QDate(2026, 10, 1))).code, ErrorCode::Invalid);
    }
};

QTEST_GUILESS_MAIN(TstBooksOwnerPay)
#include "tst_books_owner_pay.moc"
