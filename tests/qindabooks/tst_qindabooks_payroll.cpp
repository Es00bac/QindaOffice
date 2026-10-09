// SPDX-License-Identifier: GPL-3.0-or-later
// M71 (D-081): PayrollService's QML API over made-up company files. An
// S corporation paying Colorado's taxes: setup with the account numbers
// (EIN shown in full only in the edit field), an officer added to the M68
// Employees list with a masked SSN everywhere but the editor, Run Payroll's
// draft, preview and posting ($3,000.00 biweekly in Denver → net
// $2,308.17, the same check tst_books_payroll posts), the paycheck's stub,
// adjusting and voiding it, the federal deposit falling due and being
// paid, the payroll reports in the catalog and filing exports that refuse
// made-up numbers without echoing them. A sole proprietorship: the planner
// from a projected $80,000 profit (the figures tst_payroll_estimates
// derives), a draw with its set-aside, an estimated payment closing
// installment 1, a draw check in the M68 queue, due-date reminders posted
// once. Every business type's owner-pay rules, and a business outside
// Colorado with its own payroll tax items (D-081 amendment). Identity
// numbers are made up (EIN 00-0000000, SSN 900-00-0001) and never compared
// with QCOMPARE, so no failure prints one.

#include "books_app_test_support.h"

#include "qindabooks/src/payroll_service.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QSignalSpy>

using namespace BooksAppTest;
using QindaOffice::Apps::Books::PayrollService;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

QString errorOf(const QVariantMap &result)
{
    return result.value(str("error")).toString();
}

struct PayrollCompany : Company {
    PayrollService payroll{&service};
    qint64 bank = 0;
    qint64 jane = 0;

    // A made-up company; `colorado` chooses Colorado's built-in taxes.
    explicit PayrollCompany(const QString &entity = str("sCorporation"), bool colorado = true)
    {
        bank = service.bankAccounts().value(0).toMap().value(str("id")).toLongLong();
        const QVariantMap setup = payroll.saveSetup({{str("entity"), entity}, {str("ein"), str("00-0000000")},
                                                     {str("colorado"), colorado},
                                                     {str("stateCode"), colorado ? QString() : str("TX")},
                                                     {str("optCity"), colorado ? str("denver") : QString()},
                                                     {str("payrollBankAccountId"), bank}});
        QVERIFY2(ok(setup), qPrintable(errorOf(setup)));
    }
    // Jane Doe, $78,000 a year paid biweekly, an officer on salary.
    void addJane()
    {
        const QVariantMap saved = payroll.saveEmployee(
            {{str("firstName"), str("Jane")}, {str("lastName"), str("Doe")}, {str("ssn"), str("900-00-0001")},
             {str("street"), str("1 Elm Street")}, {str("city"), str("Denver")}, {str("zip"), str("80202")},
             {str("payType"), str("salary")}, {str("payRate"), str("78000")}, {str("frequency"), str("biweekly")},
             {str("w4Status"), str("singleOrMarriedSeparately")}, {str("ownerEmployee"), true}});
        QVERIFY2(ok(saved), qPrintable(errorOf(saved)));
        jane = idOf(saved);
    }
    QVariantMap run(const QString &payDate)
    {
        QVariantMap draft = payroll.payrollDraft(payDate);
        draft.insert(str("toPrint"), false);
        return draft;
    }
};

bool mentionsSsn(const QString &text)
{
    return text.contains(str("900-00-0001")) || text.contains(str("900000001"));
}

} // namespace

class QindaBooksPayrollTest final : public QObject {
    Q_OBJECT

private slots:
    void setupKeepsTheNumbersAndCreatesTheAccounts()
    {
        PayrollCompany c;
        QVERIFY(c.payroll.configured());
        QCOMPARE(c.payroll.entity(), str("sCorporation"));
        QCOMPARE(c.payroll.tablesYear(), 2026);
        const QVariantMap setup = c.payroll.setup();
        QVERIFY(setup.value(str("ein")).toString() == str("00-0000000")); // the edit field
        QCOMPARE(setup.value(str("einMasked")).toString(), str("••-•••0000"));
        QCOMPARE(setup.value(str("optCity")).toString(), str("denver"));
        QVERIFY(!find(c.service.accounts(), str("name"), str("Payroll Liabilities")).isEmpty());
        QVERIFY(!find(c.service.accounts(), str("name"), str("Officer Wages")).isEmpty());
        // CDLE's three parts, and a refused rate.
        QVERIFY(ok(c.payroll.saveSetup({{str("uiRate"), str("1.53%, 0.17%, 1.35%")}, {str("famliEmployerShare"), str("50")}})));
        QCOMPARE(c.payroll.setup().value(str("uiRateText")).toString(), str("3.05%"));
        QCOMPARE(c.payroll.setup().value(str("uiRateParts")).toList().size(), 3);
        QCOMPARE(c.payroll.setup().value(str("famliEmployerShare")).toString(), str("50%"));
        QVERIFY(!ok(c.payroll.saveSetup({{str("uiRate"), str("abc")}})));
        QVERIFY(!ok(c.payroll.saveSetup({{str("ein"), str("12-345")}})));
        const QVariantMap info = c.payroll.tablesInfo();
        QVERIFY(info.value(str("ok")).toBool());
        QCOMPARE(info.value(str("year")).toInt(), 2026);
        QCOMPARE(info.value(str("unconfirmed")).toList().size(), 4);
        QVERIFY(!find(info.value(str("cities")).toList(), str("key"), str("denver")).isEmpty());
        QVERIFY(info.value(str("sources")).toList().size() > 20);
    }

    void employeesExtendTheM68List()
    {
        PayrollCompany c;
        c.addJane();
        // One person, on M68's list (Time) and with payroll.
        QCOMPARE(find(c.service.employees(true), str("id"), c.jane).value(str("name")).toString(), str("Jane Doe"));
        const QVariantMap row = find(c.payroll.employees(), str("id"), c.jane);
        QVERIFY(row.value(str("hasPayroll")).toBool());
        QCOMPARE(row.value(str("ssnMasked")).toString(), str("•••-••-0001"));
        QCOMPARE(row.value(str("payText")).toString(), str("$78,000.00 a year"));
        QVERIFY(row.value(str("ownerEmployee")).toBool());
        QVERIFY(c.payroll.employee(c.jane).value(str("ssn")).toString() == str("900-00-0001")); // the edit field only
        // An M68 employee without payroll says so; a bad SSN is refused
        // without repeating it.
        const qint64 sam = idOf(c.service.saveEmployee({{str("name"), str("Sam Lee")}}));
        QVERIFY(!find(c.payroll.employees(), str("id"), sam).value(str("hasPayroll")).toBool());
        const QVariantMap bad = c.payroll.saveEmployee({{str("id"), sam}, {str("firstName"), str("Sam")}, {str("lastName"), str("Lee")},
                                                        {str("ssn"), str("900-00-00012")}});
        QVERIFY(!ok(bad));
        QVERIFY(!errorOf(bad).contains(str("900")));
        QVERIFY(!ok(c.payroll.saveEmployee({{str("firstName"), str("No")}, {str("lastName"), QString()}})));
    }

    void runPayrollPreviewsPostsAndAdjusts()
    {
        PayrollCompany c;
        c.addJane();
        QSignalSpy notices(&c.service, &BooksService::notify);
        const QVariantMap draft = c.run(str("2026-03-27"));
        QCOMPARE(draft.value(str("periodStart")).toString(), str("2026-03-14"));
        QCOMPARE(draft.value(str("periodEnd")).toString(), str("2026-03-27"));
        QCOMPARE(draft.value(str("bankAccountId")).toLongLong(), c.bank);
        QCOMPARE(draft.value(str("rows")).toList().size(), 1);
        const QVariantMap preview = c.payroll.previewPayroll(draft);
        QVERIFY2(preview.value(str("ok")).toBool(), qPrintable(errorOf(preview)));
        const QVariantMap row = preview.value(str("rows")).toList().first().toMap();
        QCOMPARE(row.value(str("grossText")).toString(), str("$3,000.00"));
        QCOMPARE(row.value(str("fitText")).toString(), str("$320.38"));
        QCOMPARE(row.value(str("ssText")).toString(), str("$186.00"));
        QCOMPARE(row.value(str("medicareText")).toString(), str("$43.50"));
        QCOMPARE(row.value(str("coloradoText")).toString(), str("$123.00"));
        QCOMPARE(row.value(str("famliText")).toString(), str("$13.20"));
        QCOMPARE(row.value(str("optText")).toString(), str("$5.75"));
        QCOMPARE(row.value(str("netText")).toString(), str("$2,308.17"));
        QCOMPARE(row.value(str("employerText")).toString(), str("$343.00"));
        QVERIFY(!row.value(str("federalWorksheet")).toList().isEmpty());
        QCOMPARE(preview.value(str("totals")).toMap().value(str("costText")).toString(), str("$3,343.00"));
        QVERIFY(c.payroll.paychecks(QString(), QString()).isEmpty()); // a preview posts nothing

        const QVariantMap ran = c.payroll.runPayroll(draft);
        QVERIFY2(ok(ran), qPrintable(errorOf(ran)));
        QCOMPARE(ran.value(str("ids")).toList().size(), 1);
        const qint64 id = ran.value(str("ids")).toList().first().toLongLong();
        QCOMPARE(c.service.document(id).value(str("kind")).toString(), str("paycheck"));
        const QVariantMap check = c.payroll.paycheck(id);
        QCOMPARE(check.value(str("netText")).toString(), str("$2,308.17"));
        const QVariantMap net = find(check.value(str("stub")).toList(), str("label"), str("Net pay"));
        QCOMPARE(net.value(str("currentText")).toString(), str("$2,308.17"));
        QCOMPARE(net.value(str("ytdText")).toString(), str("$2,308.17"));
        QCOMPARE(find(check.value(str("employer")).toList(), str("label"), str("Colorado unemployment (UI)"))
                     .value(str("currentText")).toString(), str("$91.50"));

        // The federal deposit for March falls due April 15 (monthly depositor).
        const QVariantMap federal = find(c.payroll.liabilities(QString(), QString(), true), str("tax"), str("federal941"));
        QCOMPARE(federal.value(str("due")).toString(), str("2026-04-15"));
        QCOMPARE(federal.value(str("balanceText")).toString(), str("$779.38"));
        const QVariantMap paid = c.payroll.payLiability({{str("tax"), str("federal941")}, {str("start"), federal.value(str("start"))},
                                                         {str("end"), federal.value(str("end"))}, {str("bankAccountId"), c.bank},
                                                         {str("date"), str("2026-04-10")}, {str("amount"), QString()},
                                                         {str("reference"), str("EFTPS 0001")}});
        QVERIFY2(ok(paid), qPrintable(errorOf(paid)));
        QVERIFY(find(c.payroll.liabilities(QString(), QString(), true), str("tax"), str("federal941")).isEmpty());
        QCOMPARE(c.payroll.liabilityPayments(2026).size(), 1);

        // Adjusting writes a corrected paycheck; voiding takes it out.
        const QVariantMap adjusted = c.payroll.adjustPaycheck(id, {{str("bonus"), str("500.00")}, {str("reason"), str("Spot bonus")}});
        QVERIFY2(ok(adjusted), qPrintable(errorOf(adjusted)));
        QVERIFY(c.payroll.paycheck(id).value(str("voided")).toBool());
        const QVariantMap replacement = c.payroll.paycheck(idOf(adjusted));
        QCOMPARE(replacement.value(str("replacesDocId")).toLongLong(), id);
        QCOMPARE(replacement.value(str("bonusText")).toString(), str("500.00"));
        QVERIFY(ok(c.payroll.voidPaycheck(idOf(adjusted), str("Wrong person"))));
        QCOMPARE(c.payroll.paychecks(QString(), QString()).size(), 2); // both kept, both void
        for (const QList<QVariant> &args : std::as_const(notices)) {
            QVERIFY(!mentionsSsn(args.value(1).toString()));
        }
    }

    void reportsAndFilingCopies()
    {
        PayrollCompany c;
        c.addJane();
        QVERIFY(ok(c.payroll.runPayroll(c.run(str("2026-03-27")))));
        const QVariantList catalog = c.service.reportCatalog();
        const QVariantMap form = find(catalog, str("id"), str("form_941"));
        QCOMPARE(form.value(str("group")).toString(), str("Payroll tax forms"));
        QCOMPARE(form.value(str("options")).toStringList(), (QStringList{str("from"), str("to")}));
        QCOMPARE(find(catalog, str("id"), str("payroll_liability_balances")).value(str("options")).toStringList(),
                 (QStringList{str("from"), str("to")}));
        const QVariantMap f941 = c.service.runReport(str("form_941"), {{str("from"), str("2026-01-01")}, {str("to"), str("2026-03-31")}});
        QVERIFY2(ok(f941), qPrintable(errorOf(f941)));
        QVERIFY(!f941.value(str("rows")).toList().isEmpty());
        // On screen the W-2 masks the SSN; the filing copy is a PDF.
        const QVariantMap w2 = c.service.runReport(str("form_w2"), {{str("from"), str("2026-01-01")}, {str("to"), str("2026-03-31")}});
        QVERIFY2(ok(w2), qPrintable(errorOf(w2)));
        const QString shown = QString::fromUtf8(QJsonDocument::fromVariant(w2).toJson());
        QVERIFY(!mentionsSsn(shown));
        QVERIFY(shown.contains(str("•••-••-0001")));
        const QVariantMap copy = c.payroll.exportFormForFiling(str("form_w2"), str("2026-03-31"), url(c.dir, str("w2.pdf")));
        QVERIFY2(ok(copy), qPrintable(errorOf(copy)));
        QVERIFY(QFileInfo(c.dir.filePath(str("w2.pdf"))).size() > 1000);
        QVERIFY(!ok(c.payroll.exportFormForFiling(str("form_941"), str("2026-03-31"), url(c.dir, str("941.pdf")))));
        // The SSA file refuses the made-up numbers — without repeating them.
        const QVariantMap efw2 = c.payroll.exportEfw2(2026, url(c.dir, str("W2REPORT")));
        QVERIFY(!ok(efw2));
        QVERIFY(!mentionsSsn(errorOf(efw2)));
        QVERIFY(!errorOf(efw2).contains(str("000000000")));
        QVERIFY(!QFile::exists(c.dir.filePath(str("W2REPORT"))));
    }

    void ownerPayAndEstimatedTaxes()
    {
        PayrollCompany c(str("scheduleC"));
        const qint64 savings = idOf(c.service.saveAccount({{str("type"), str("bank")}, {str("name"), str("Tax Savings")},
                                                          {str("number"), str("1060")}}));
        QVERIFY(savings > 0);
        QVERIFY(ok(c.payroll.saveSetup({{str("setAsideAccountId"), savings}})));
        const qint64 owner = idOf(c.payroll.saveOwner({{str("name"), str("Pat Owner")}, {str("address"), str("9 Aspen Court\nGolden, CO 80401")}}));
        QVERIFY(owner > 0);
        QCOMPARE(c.payroll.owners().size(), 1);
        QVERIFY(ok(c.payroll.saveEstimateProfile({{str("filingStatus"), str("single")}, {str("projectedProfit"), str("80000.00")}})));
        QVERIFY(!ok(c.payroll.saveEstimateProfile({{str("projectedProfit"), str("lots")}})));

        // tst_payroll_estimates' $80,000 Schedule C, single, no prior year.
        QVariantMap plan = c.payroll.estimate();
        QVERIFY2(plan.value(str("ok")).toBool(), qPrintable(errorOf(plan)));
        QVERIFY(!plan.value(str("annualized")).toBool());
        QCOMPARE(plan.value(str("annualProfitText")).toString(), str("$80,000.00"));
        QCOMPARE(plan.value(str("seTaxText")).toString(), str("$11,303.64"));
        QVariantMap federal = plan.value(str("federal")).toMap();
        QCOMPARE(federal.value(str("totalTaxText")).toString(), str("$16,647.46"));
        QCOMPARE(federal.value(str("requiredText")).toString(), str("$14,982.71"));
        QCOMPARE(federal.value(str("installments")).toList().first().toMap().value(str("amountText")).toString(), str("$3,745.68"));
        QCOMPARE(plan.value(str("coloradoTaxText")).toString(), str("$2,050.00"));
        QCOMPARE(plan.value(str("colorado")).toMap().value(str("requiredText")).toString(), str("$1,435.00"));
        QCOMPARE(plan.value(str("setAsidePercent")).toInt(), 24);
        QCOMPARE(c.payroll.setAsideFor(str("1000.00")).value(str("amountText")).toString(), str("$240.00"));

        // A draw by transfer, and the set-aside it suggests.
        const QVariantMap draw = c.payroll.payOwner({{str("kind"), str("draw")}, {str("ownerId"), owner}, {str("bankAccountId"), c.bank},
                                                     {str("amount"), str("1000.00")}, {str("date"), str("2026-03-31")},
                                                     {str("method"), str("transfer")}});
        QVERIFY2(ok(draw), qPrintable(errorOf(draw)));
        QCOMPARE(draw.value(str("setAsidePercent")).toInt(), 24);
        QCOMPARE(draw.value(str("setAsideText")).toString(), str("$240.00"));
        QVERIFY(ok(c.payroll.recordSetAside({{str("amount"), draw.value(str("setAsidePlain"))}, {str("date"), str("2026-03-31")},
                                              {str("bankAccountId"), c.bank}, {str("toAccountId"), savings}})));
        QCOMPARE(find(c.service.accounts(), str("id"), savings).value(str("balance")).toLongLong(), 24000);
        // A draw check waits in the M68 queue under the owner's name.
        QVERIFY(ok(c.payroll.payOwner({{str("kind"), str("draw")}, {str("ownerId"), owner}, {str("bankAccountId"), c.bank},
                                         {str("amount"), str("500.00")}, {str("date"), str("2026-03-31")},
                                         {str("method"), str("check")}, {str("toPrint"), true}})));
        QCOMPARE(find(c.service.checkQueue(c.bank), str("payeeName"), str("Pat Owner")).value(str("amountText")).toString(), str("$500.00"));
        // Installment 1 paid through EFTPS.
        QVERIFY(ok(c.payroll.recordEstimatedPayment({{str("jurisdiction"), str("federal")}, {str("taxYear"), 2026},
                                                      {str("installment"), 1}, {str("amount"), str("3745.68")},
                                                      {str("date"), str("2026-03-31")}, {str("bankAccountId"), c.bank},
                                                      {str("ownerId"), owner}, {str("reference"), str("EFTPS 4401")}})));
        plan = c.payroll.estimate();
        federal = plan.value(str("federal")).toMap();
        QCOMPARE(federal.value(str("installments")).toList().first().toMap().value(str("state")).toString(), str("paid"));
        QCOMPARE(federal.value(str("paidText")).toString(), str("$3,745.68"));
        const QVariantList payments = c.payroll.ownerPayments(2026);
        QCOMPARE(payments.size(), 4);
        QCOMPARE(find(payments, str("kind"), str("federalEstimate")).value(str("kindLabel")).toString(), str("Federal estimated tax"));

        // Due within 30 days: Colorado's first installment; reminded once.
        QVERIFY(c.payroll.upcoming(14).isEmpty());
        const QVariantList soon = c.payroll.upcoming(30);
        QCOMPARE(soon.size(), 1);
        QCOMPARE(soon.first().toMap().value(str("label")).toString(), str("Colorado estimated tax, payment 1"));
        QCOMPARE(soon.first().toMap().value(str("date")).toString(), str("2026-04-15"));
        QSignalSpy notices(&c.service, &BooksService::notify);
        c.service.setToday(QDate(2026, 4, 2));
        c.payroll.remindDueDates();
        c.payroll.remindDueDates();
        QCOMPARE(notices.count(), 1);
        QVERIFY(notices.first().value(1).toString().contains(str("Colorado estimated tax, payment 1")));
        // A Schedule C owner takes no shareholder distribution.
        QVERIFY(!ok(c.payroll.payOwner({{str("kind"), str("distribution")}, {str("ownerId"), owner}, {str("bankAccountId"), c.bank},
                                          {str("amount"), str("100.00")}, {str("date"), str("2026-04-02")}, {str("method"), str("transfer")}})));
    }

    void sCorporationOwnersArePaidWages()
    {
        PayrollCompany c;
        const qint64 owner = idOf(c.payroll.saveOwner({{str("name"), str("Pat Owner")}}));
        const QVariantMap plan = c.payroll.estimate();
        QVERIFY(!plan.value(str("ok")).toBool());
        QVERIFY(errorOf(plan).contains(str("S corporation")));
        QVERIFY(!ok(c.payroll.payOwner({{str("kind"), str("draw")}, {str("ownerId"), owner}, {str("bankAccountId"), c.bank},
                                          {str("amount"), str("100.00")}, {str("date"), str("2026-03-31")}, {str("method"), str("transfer")}})));
        const QVariantMap distribution = c.payroll.payOwner({{str("kind"), str("distribution")}, {str("ownerId"), owner},
                                                             {str("bankAccountId"), c.bank}, {str("amount"), str("5000.00")},
                                                             {str("date"), str("2026-03-31")}, {str("method"), str("transfer")}});
        QVERIFY2(ok(distribution), qPrintable(errorOf(distribution)));
        QVERIFY(!distribution.contains(str("setAside"))); // a distribution suggests no set-aside
        QVERIFY(ok(c.payroll.voidOwnerPayment(idOf(distribution), str("Wrong amount"))));
        QVERIFY(find(c.payroll.ownerPayments(2026), str("id"), idOf(distribution)).value(str("voided")).toBool());
        // A sole proprietor is not paid by paycheck.
        c.addJane();
        QVERIFY(ok(c.payroll.saveSetup({{str("entity"), str("scheduleC")}})));
        const QVariantMap preview = c.payroll.previewPayroll(c.run(str("2026-03-27")));
        QVERIFY(!preview.value(str("ok")).toBool());
        QVERIFY(preview.value(str("rows")).toList().first().toMap().value(str("error")).toString().contains(str("owner's draw")));
    }

    // Each business type pays its owners its own way (owner_pay.h).
    void businessTypesDecideOwnerPay()
    {
        const auto pay = [](PayrollCompany &c, qint64 owner, const char *kind) {
            return c.payroll.payOwner({{str("kind"), str(kind)}, {str("ownerId"), owner}, {str("bankAccountId"), c.bank},
                                       {str("amount"), str("100.00")}, {str("date"), str("2026-03-31")},
                                       {str("method"), str("transfer")}});
        };
        PayrollCompany corp(str("cCorporation"));
        const qint64 holder = idOf(corp.payroll.saveOwner({{str("name"), str("Shareholder One")}}));
        QVERIFY(ok(pay(corp, holder, "dividend")));
        QVERIFY(!ok(pay(corp, holder, "draw")));
        QVERIFY(!ok(pay(corp, holder, "distribution")));
        QVERIFY(errorOf(corp.payroll.estimate()).contains(str("C corporation")));
        QCOMPARE(corp.payroll.ownerPayments(2026).first().toMap().value(str("kindLabel")).toString(), str("Dividend"));
        PayrollCompany nonprofit(str("nonprofit"));
        const qint64 nobody = idOf(nonprofit.payroll.saveOwner({{str("name"), str("Board Member")}}));
        for (const char *kind : {"draw", "distribution", "dividend"}) {
            QVERIFY(errorOf(pay(nonprofit, nobody, kind)).contains(str("no owners")));
        }
        QVERIFY(errorOf(nonprofit.payroll.estimate()).contains(str("nonprofit")));
        PayrollCompany partnership(str("partnership"));
        const qint64 partner = idOf(partnership.payroll.saveOwner({{str("name"), str("Partner A")}}));
        QVERIFY(ok(pay(partnership, partner, "draw")));
        QVERIFY(!ok(pay(partnership, partner, "dividend")));
        QCOMPARE(partnership.payroll.entity(), str("partnership"));
    }

    // A business outside Colorado with its own items: an employer
    // unemployment tax 2.7 % up to $10,000 and state income tax withholding
    // at the employee's 4 % + $5.00. The $3,000 check: no Colorado tax;
    // withholding 4 % × 3,000 + 5 = 125.00 ("Other taxes"); net 3,000 −
    // 320.38 − 186.00 − 43.50 − 125.00 = 2,325.12; employer 186.00 + 43.50
    // + 18.00 + 81.00 = 328.50.
    void payrollTaxItemsForAnotherState()
    {
        PayrollCompany c(str("sCorporation"), false);
        QVERIFY(!c.payroll.colorado());
        const QVariantMap ui = c.payroll.savePayrollItem({{str("name"), str("Example state unemployment")},
                                                          {str("side"), str("employer")}, {str("kind"), str("tax")},
                                                          {str("rate"), str("2.7%")}, {str("wageBase"), str("10000")},
                                                          {str("agency"), str("Example workforce agency")},
                                                          {str("frequency"), str("quarterly")}});
        QVERIFY2(ok(ui), qPrintable(errorOf(ui)));
        const QVariantMap sit = c.payroll.savePayrollItem({{str("name"), str("Example state income tax")},
                                                           {str("kind"), str("withholding")},
                                                           {str("agency"), str("Example revenue department")},
                                                           {str("frequency"), str("monthly")}});
        QVERIFY2(ok(sit), qPrintable(errorOf(sit)));
        QVERIFY(!ok(c.payroll.savePayrollItem({{str("name"), str("Bad")}, {str("rate"), str("lots")}})));
        const QVariantList items = c.payroll.payrollItems();
        QCOMPARE(items.size(), 2);
        QCOMPARE(find(items, str("id"), idOf(ui)).value(str("rate")).toString(), str("2.7%"));
        QCOMPARE(find(items, str("id"), idOf(ui)).value(str("wageBase")).toString(), str("10000.00"));
        QCOMPARE(find(items, str("id"), idOf(ui)).value(str("wageBaseText")).toString(), str("$10,000.00"));
        c.addJane();
        QVariantMap jane = c.payroll.employee(c.jane);
        QVariantList rows = jane.value(str("customItems")).toList();
        QCOMPARE(rows.size(), 2);
        for (QVariant &v : rows) {
            QVariantMap row = v.toMap();
            if (row.value(str("id")).toLongLong() == idOf(sit)) {
                row.insert(str("percent"), str("4%"));
                row.insert(str("extra"), str("5.00"));
            }
            v = row;
        }
        jane.insert(str("customItems"), rows);
        jane.insert(str("id"), c.jane);
        QVERIFY2(ok(c.payroll.saveEmployee(jane)), qPrintable(errorOf(c.payroll.saveEmployee(jane))));
        const QVariantMap stored = find(c.payroll.employee(c.jane).value(str("customItems")).toList(), str("id"), idOf(sit));
        QCOMPARE(stored.value(str("percent")).toString(), str("4%"));
        QCOMPARE(stored.value(str("extra")).toString(), str("5.00"));
        const QVariantMap preview = c.payroll.previewPayroll(c.run(str("2026-03-27")));
        QVERIFY2(preview.value(str("ok")).toBool(), qPrintable(errorOf(preview)));
        QVERIFY(!preview.value(str("colorado")).toBool());
        QVERIFY(preview.value(str("hasCustom")).toBool());
        const QVariantMap row = preview.value(str("rows")).toList().first().toMap();
        QCOMPARE(row.value(str("coloradoText")).toString(), str("$0.00"));
        QCOMPARE(row.value(str("otherText")).toString(), str("$125.00"));
        QCOMPARE(row.value(str("netText")).toString(), str("$2,325.12"));
        QCOMPARE(row.value(str("employerText")).toString(), str("$328.50"));
        const QVariantMap ran = c.payroll.runPayroll(c.run(str("2026-03-27")));
        QVERIFY2(ok(ran), qPrintable(errorOf(ran)));
        const QVariantMap check = c.payroll.paycheck(ran.value(str("ids")).toList().first().toLongLong());
        QCOMPARE(find(check.value(str("stub")).toList(), str("label"), str("Example state income tax"))
                     .value(str("currentText")).toString(), str("$125.00"));
        QVERIFY(find(check.value(str("stub")).toList(), str("label"), str("Colorado income tax")).isEmpty());
        QCOMPARE(find(check.value(str("employer")).toList(), str("label"), str("Example state unemployment"))
                     .value(str("currentText")).toString(), str("$81.00"));
        QCOMPARE(check.value(str("customWorksheets")).toList().size(), 2);
        const QVariantMap owed = find(c.payroll.liabilities(QString(), QString(), true), str("label"),
                                      str("Example state unemployment"));
        QCOMPARE(owed.value(str("agency")).toString(), str("Example workforce agency"));
        QCOMPARE(owed.value(str("balanceText")).toString(), str("$81.00"));
        QCOMPARE(owed.value(str("due")).toString(), str("2026-04-30"));
        // Colorado's forms are not offered to this business.
        const QVariantList catalog = c.service.reportCatalog();
        QVERIFY(find(catalog, str("id"), str("dr_1094")).isEmpty());
        QVERIFY(find(catalog, str("id"), str("famli_wage_report")).isEmpty());
        QVERIFY(!find(catalog, str("id"), str("form_941")).isEmpty());
        QVERIFY(ok(c.payroll.setPayrollItemActive(idOf(ui), false)));
        QVERIFY(!find(c.payroll.payrollItems(), str("id"), idOf(ui)).value(str("active")).toBool());
    }
};

BOOKS_APP_TEST_MAIN(QindaBooksPayrollTest)

#include "tst_qindabooks_payroll.moc"
