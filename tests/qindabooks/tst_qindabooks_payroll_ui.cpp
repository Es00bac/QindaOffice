// SPDX-License-Identifier: GPL-3.0-or-later
// M71 (D-081) through the real window at 1280 and 520 px, over made-up
// companies: Set Up Owner Pay & Payroll (an S corporation paying
// Colorado's taxes), the employee payroll editor, Run Payroll's three
// steps, the paycheck with its stub and worksheet, Pay Payroll
// Liabilities, the Form 941 worksheet under Reports, Pay Owner turning
// into a distribution for an S corporation; a sole proprietorship's
// Estimated taxes planner, a draw with its tax set-aside, and the
// Overview's coming-due list; and a partnership outside Colorado with its
// own payroll tax items (the items dialog, an employee's user-entered
// state withholding, the preview's "Other taxes"). Each command opens its
// dialog; the dialog drives PayrollService. With QQO_GRAB_DIR every
// screen is saved for review. Identity numbers are made up and never
// printed by a failure.

#include "books_window_scene.h"

#include "qindabooks/src/payroll_service.h"

#include <QJSValue>

using namespace BooksAppTest;
using QindaOffice::Apps::Books::PayrollService;
using QindaOffice::Test::saveGrab;

namespace {

QString str(const char *text)
{
    return QString::fromUtf8(text);
}

QSize sizeFor(int width)
{
    return width == 1280 ? QSize(1280, 800) : QSize(520, 700);
}

void closePopup(QObject *popup)
{
    QMetaObject::invokeMethod(popup, "close");
}

// A QML function's JS object result as a map.
QVariantMap mapOf(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QJSValue>()) {
        return qvariant_cast<QJSValue>(value).toVariant().toMap();
    }
    return value.toMap();
}

bool succeeded(const QVariant &value)
{
    return mapOf(value).value(str("ok")).toBool();
}

QString errorOf(const QVariant &value)
{
    return mapOf(value).value(str("error")).toString();
}

void addWidths()
{
    QTest::addColumn<int>("width");
    QTest::newRow("1280") << 1280;
    QTest::newRow("520") << 520;
}

void type(WindowScene &scene, const char *name, const QString &text)
{
    QQuickItem *field = scene.item(str(name));
    QVERIFY2(field != nullptr, name);
    field->setProperty("text", text);
}

// A dialog scrolls its body (Tk.Scroll over a Flickable): bring `item`
// into view for the grab, a quarter of the way down the viewport.
void scrollIntoView(QQuickItem *item)
{
    for (QQuickItem *p = item != nullptr ? item->parentItem() : nullptr; p != nullptr; p = p->parentItem()) {
        if (!p->inherits("QQuickFlickable")) {
            continue;
        }
        auto *content = p->property("contentItem").value<QQuickItem *>();
        const qreal top = item->mapToItem(content, QPointF(0, 0)).y();
        const qreal maxY = qMax(0.0, p->property("contentHeight").toReal() - p->height());
        p->setProperty("contentY", qBound(0.0, top - p->height() / 4, maxY));
        return;
    }
}

} // namespace

class QindaBooksPayrollUiTest final : public QObject {
    Q_OBJECT

private slots:
    void setupEmployeeAndPayroll_data() { addWidths(); }
    void setupEmployeeAndPayroll()
    {
        QFETCH(int, width);
        WindowScene scene(sizeFor(width));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        PayrollService *payroll = scene.host.payroll();

        // Setup: how the business is taxed, the EIN, the city.
        QObject *setup = scene.openThrough(str("booksPayrollSetupAction"), str("payrollSetupDialog"));
        QVERIFY(setup != nullptr);
        QVERIFY(scene.item(str("payrollTablesNotice"))->isVisible());
        setup->setProperty("entity", str("sCorporation"));
        scene.item(str("payrollColoradoOn"))->setProperty("checked", true);
        type(scene, "payrollEin", str("00-0000000"));
        const QVariantList cities = setup->property("info").toMap().value(str("cities")).toList();
        int denver = -1;
        for (int i = 0; i < cities.size(); ++i) {
            denver = cities.at(i).toMap().value(str("key")).toString() == str("denver") ? i : denver;
        }
        QVERIFY(denver > 0);
        scene.item(str("payrollCity"))->setProperty("currentIndex", denver);
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_payroll_setup_%1").arg(width));
        const QVariant saved = WindowScene::call(setup, "save");
        QVERIFY2(succeeded(saved), qPrintable(errorOf(saved)));
        closePopup(setup);
        QCOMPARE(payroll->entity(), str("sCorporation"));
        QVERIFY(payroll->configured());

        // Employee Payroll opens the Employees tab; the editor adds Jane.
        QVERIFY(scene.host.actions()->trigger(str("booksPayrollEmployeesAction")));
        QCOMPARE(scene.host.currentView(), str("payroll"));
        QObject *view = scene.object(str("payrollView"));
        QVERIFY(view != nullptr);
        QCOMPARE(view->property("tab").toInt(), 3);
        QObject *editor = scene.object(str("employeePayrollDialog"));
        QVERIFY(editor != nullptr);
        WindowScene::call(editor, "openFor", 0);
        QTRY_VERIFY(editor->property("opened").toBool());
        type(scene, "payrollFirstName", str("Jane"));
        type(scene, "payrollLastName", str("Doe"));
        type(scene, "payrollSsn", str("900-00-0001"));
        type(scene, "payrollStreet", str("1 Elm Street"));
        type(scene, "payrollCityName", str("Denver"));
        type(scene, "payrollZip", str("80202"));
        type(scene, "payrollRate", str("78000"));
        scene.item(str("payrollOwnerEmployee"))->setProperty("checked", true);
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_payroll_employee_%1").arg(width));
        const QVariant employee = WindowScene::call(editor, "save");
        QVERIFY2(succeeded(employee), qPrintable(errorOf(employee)));
        closePopup(editor);
        QTRY_COMPARE(scene.item(str("payrollEmployees"))->property("count").toInt(), 1);
        QTest::qWait(150);
        saveGrab(scene.window, str("qindabooks_payroll_employees_%1").arg(width));

        // Run Payroll: the grid, the preview, the paychecks.
        QObject *run = scene.openThrough(str("booksRunPayrollAction"), str("runPayrollDialog"));
        QVERIFY(run != nullptr);
        QCOMPARE(run->property("rows").toList().size(), 1);
        QTest::qWait(150);
        saveGrab(scene.window, str("qindabooks_run_payroll_enter_%1").arg(width));
        const QVariant preview = WindowScene::call(run, "previewRun");
        QVERIFY2(succeeded(preview), qPrintable(errorOf(preview)));
        QCOMPARE(mapOf(preview).value(str("rows")).toList().first().toMap().value(str("netText")).toString(), str("$2,308.17"));
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_run_payroll_%1").arg(width));
        const QVariant created = WindowScene::call(run, "create");
        QVERIFY2(succeeded(created), qPrintable(errorOf(created)));
        QCOMPARE(run->property("created").toInt(), 1);
        QTest::qWait(150);
        saveGrab(scene.window, str("qindabooks_run_payroll_done_%1").arg(width));
        closePopup(run);
        // Printed later: the paycheck waits in the M68 queue.
        const qint64 bank = payroll->setup().value(str("payrollBankAccountId")).toLongLong();
        QCOMPARE(find(scene.host.books()->checkQueue(bank), str("payeeName"), str("Jane Doe")).value(str("kind")).toString(),
                 str("paycheck"));

        // The paycheck: stub, employer taxes, the Pub 15-T worksheet.
        const qint64 paycheck = payroll->paychecks(QString(), QString()).first().toMap().value(str("id")).toLongLong();
        WindowScene::call(view, "openPaycheck", paycheck);
        QObject *check = scene.object(str("paycheckDialog"));
        QTRY_VERIFY(check->property("opened").toBool());
        // Queued: it is numbered and printed from Checks to Print.
        QVERIFY(check->property("check").toMap().value(str("toPrint")).toBool());
        scene.item(str("paycheckFederalHeader"))->setProperty("collapsed", false);
        QTRY_VERIFY(scene.item(str("paycheckFederalWorksheet"))->isVisible());
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_paycheck_%1").arg(width));
        QCOMPARE(scene.item(str("paycheckStub"))->property("count").toInt(), 8); // gross, five taxes, OPT, net
        closePopup(check);

        // Pay Payroll Liabilities: the federal deposit for March first.
        QObject *pay = scene.openThrough(str("booksPayLiabilitiesAction"), str("payLiabilitiesDialog"));
        QVERIFY(pay != nullptr);
        const QVariantList dues = pay->property("dues").toList();
        QVERIFY(dues.size() >= 5);
        QCOMPARE(dues.first().toMap().value(str("tax")).toString(), str("federal941"));
        QCOMPARE(dues.first().toMap().value(str("due")).toString(), str("2026-04-15"));
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_pay_liabilities_%1").arg(width));
        const QVariant recorded = WindowScene::call(pay, "record");
        QVERIFY2(succeeded(recorded), qPrintable(errorOf(recorded)));
        QCOMPARE(pay->property("dues").toList().size(), dues.size() - 1);
        closePopup(pay);

        // Payroll Tax Forms opens the Form 941 worksheet in Reports.
        QVERIFY(scene.host.actions()->trigger(str("booksPayrollFormsAction")));
        QCOMPARE(scene.host.currentView(), str("reports"));
        QTRY_VERIFY(scene.item(str("reportRows")) != nullptr && scene.item(str("reportRows"))->property("count").toInt() > 10);
        QTest::qWait(250);
        saveGrab(scene.window, str("qindabooks_form941_%1").arg(width));

        // Pay Owner, for an S corporation, records a distribution.
        QObject *owner = scene.openThrough(str("booksPayOwnerAction"), str("payOwnerDialog"));
        QVERIFY(owner != nullptr);
        QCOMPARE(owner->property("kind").toString(), str("distribution"));
        QVERIFY(scene.item(str("payOwnerSwitched"))->isVisible());
        type(scene, "payOwnerName", str("Jane Doe"));
        type(scene, "payOwnerAmount", str("5000.00"));
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_pay_owner_distribution_%1").arg(width));
        const QVariant distribution = WindowScene::call(owner, "record");
        QVERIFY2(succeeded(distribution), qPrintable(errorOf(distribution)));
        closePopup(owner);
        QCOMPARE(payroll->ownerPayments(2026).size(), 1);

        // The Payroll ribbon tab, for the review grabs (the gate in
        // tst_qindabooks_ui checks every item fits at 1320 and 520 px).
        QQuickItem *ribbon = scene.item(str("officeRibbon"));
        QVERIFY(ribbon != nullptr);
        QVariant shown;
        QMetaObject::invokeMethod(ribbon, "activateTab", Q_RETURN_ARG(QVariant, shown), Q_ARG(QVariant, str("payroll")));
        QVERIFY(shown.toBool());
        QTest::qWait(150);
        saveGrab(scene.window, str("qindabooks_ribbon_payroll_%1").arg(width));
    }

    void scheduleCOwnerPayAndEstimates_data() { addWidths(); }
    void scheduleCOwnerPayAndEstimates()
    {
        QFETCH(int, width);
        WindowScene scene(sizeFor(width));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        PayrollService *payroll = scene.host.payroll();
        const qint64 savings = scene.account(str("1010"));
        QVERIFY(succeeded(payroll->saveSetup({{str("entity"), str("scheduleC")}, {str("setAsideAccountId"), savings},
                                              {str("colorado"), true},
                                              {str("payrollBankAccountId"), scene.account(str("1000"))}})));
        QVERIFY(succeeded(payroll->saveOwner({{str("name"), str("Pat Owner")}, {str("address"), str("9 Aspen Court\nGolden, CO 80401")}})));

        // The planner, from the owner's tax picture.
        QVERIFY(scene.host.actions()->trigger(str("booksEstimatedTaxesAction")));
        QObject *view = scene.object(str("payrollView"));
        QCOMPARE(view->property("tab").toInt(), 2);
        QObject *profile = scene.object(str("estimateProfileDialog"));
        QVERIFY(profile != nullptr);
        QVERIFY(QMetaObject::invokeMethod(scene.item(str("estimateProfile")), "clicked"));
        QTRY_VERIFY(profile->property("opened").toBool());
        type(scene, "estimateProjected", str("80000.00"));
        QTest::qWait(150);
        saveGrab(scene.window, str("qindabooks_tax_picture_%1").arg(width));
        const QVariant kept = WindowScene::call(profile, "save");
        QVERIFY2(succeeded(kept), qPrintable(errorOf(kept)));
        closePopup(profile);
        QObject *estimates = scene.object(str("payrollEstimates"));
        WindowScene::call(estimates, "refresh");
        const QVariantMap plan = estimates->property("plan").toMap();
        QVERIFY2(plan.value(str("ok")).toBool(), qPrintable(plan.value(str("error")).toString()));
        QCOMPARE(plan.value(str("federal")).toMap().value(str("totalTaxText")).toString(), str("$16,647.46"));
        QTRY_VERIFY(scene.item(str("estimateInstallments_federal")) != nullptr);
        QCOMPARE(scene.item(str("estimateInstallments_federal"))->property("count").toInt(), 4);
        QTest::qWait(250);
        saveGrab(scene.window, str("qindabooks_estimated_taxes_%1").arg(width));

        // Record installment 1 from the planner's table.
        QObject *payment = scene.object(str("estimatedPaymentDialog"));
        QVERIFY(QMetaObject::invokeMethod(payment, "openFor", Q_ARG(QVariant, str("federal")), Q_ARG(QVariant, 1),
                                          Q_ARG(QVariant, str("3745.68")), Q_ARG(QVariant, 2026)));
        QTRY_VERIFY(payment->property("opened").toBool());
        QTest::qWait(150);
        saveGrab(scene.window, str("qindabooks_estimated_payment_%1").arg(width));
        const QVariant paid = WindowScene::call(payment, "record");
        QVERIFY2(succeeded(paid), qPrintable(errorOf(paid)));
        closePopup(payment);

        // A draw, then the share to set aside for taxes.
        QObject *owner = scene.openThrough(str("booksPayOwnerAction"), str("payOwnerDialog"));
        QVERIFY(owner != nullptr);
        QCOMPARE(owner->property("kind").toString(), str("draw"));
        type(scene, "payOwnerAmount", str("1000.00"));
        const QVariant draw = WindowScene::call(owner, "record");
        QVERIFY2(succeeded(draw), qPrintable(errorOf(draw)));
        QCOMPARE(mapOf(draw).value(str("setAsideText")).toString(), str("$240.00"));
        QVERIFY(owner->property("offersSetAside").toBool());
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_pay_owner_%1").arg(width));
        QVERIFY(succeeded(WindowScene::call(owner, "moveSetAside")));
        closePopup(owner);
        QCOMPARE(find(scene.host.books()->accounts(), str("id"), savings).value(str("balance")).toLongLong(), 24000);

        // Overview: how the owner is paid and what falls due.
        WindowScene::call(view, "showTab", 0);
        QTRY_VERIFY(scene.item(str("payrollUpcoming"))->property("count").toInt() >= 1);
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_payroll_overview_%1").arg(width));
        WindowScene::call(view, "showTab", 1);
        QTRY_COMPARE(scene.item(str("ownerPayments"))->property("count").toInt(), 3); // estimate, draw, set-aside
        QTest::qWait(150);
        saveGrab(scene.window, str("qindabooks_owner_pay_%1").arg(width));
    }

    void otherStateItems_data() { addWidths(); }
    void otherStateItems()
    {
        QFETCH(int, width);
        WindowScene scene(sizeFor(width));
        QVERIFY2(scene.window != nullptr, qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        PayrollService *payroll = scene.host.payroll();
        QVERIFY(succeeded(payroll->saveSetup({{str("entity"), str("partnership")}, {str("colorado"), false},
                                              {str("stateCode"), str("TX")},
                                              {str("payrollBankAccountId"), scene.account(str("1000"))}})));
        // The setup for a business outside Colorado: its state, no Colorado section.
        QObject *setup = scene.openThrough(str("booksPayrollSetupAction"), str("payrollSetupDialog"));
        QVERIFY(setup != nullptr);
        QVERIFY(scene.item(str("payrollStateCode"))->isVisible());
        QVERIFY(!scene.item(str("payrollCan"))->isVisible());
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_payroll_setup_other_%1").arg(width));
        closePopup(setup);

        // The business's own payroll tax items.
        QObject *items = scene.openThrough(str("booksPayrollItemsAction"), str("payrollItemsDialog"));
        QVERIFY(items != nullptr);
        type(scene, "payrollItemName", str("Example state unemployment"));
        scene.item(str("payrollItemSide"))->setProperty("currentIndex", 1); // the employer
        type(scene, "payrollItemRate", str("2.7%"));
        type(scene, "payrollItemWageBase", str("10000"));
        type(scene, "payrollItemAgency", str("Example workforce agency"));
        QVariant saved = WindowScene::call(items, "save");
        QVERIFY2(succeeded(saved), qPrintable(errorOf(saved)));
        WindowScene::call(items, "pick", -1);
        type(scene, "payrollItemName", str("Example state income tax"));
        scene.item(str("payrollItemKind"))->setProperty("currentIndex", 1); // withholding, per employee
        type(scene, "payrollItemAgency", str("Example revenue department"));
        saved = WindowScene::call(items, "save");
        QVERIFY2(succeeded(saved), qPrintable(errorOf(saved)));
        QCOMPARE(payroll->payrollItems().size(), 2);
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_payroll_items_%1").arg(width));
        closePopup(items);

        // An employee carries the user-entered withholding: 4 % + $5.00.
        QObject *editor = scene.object(str("employeePayrollDialog"));
        WindowScene::call(editor, "openFor", 0);
        QTRY_VERIFY(editor->property("opened").toBool());
        type(scene, "payrollFirstName", str("Sam"));
        type(scene, "payrollLastName", str("Lee"));
        type(scene, "payrollRate", str("78000"));
        const QVariantList custom = editor->property("customItems").toList();
        QCOMPARE(custom.size(), 2);
        int sit = -1;
        for (int i = 0; i < custom.size(); ++i) {
            sit = custom.at(i).toMap().value(str("kind")).toString() == str("withholding") ? i : sit;
        }
        QVERIFY(sit >= 0);
        WindowScene::call(editor, "editItem", sit, str("percent"), str("4%"));
        WindowScene::call(editor, "editItem", sit, str("extra"), str("5.00"));
        QTRY_VERIFY(scene.item(str("payrollCustomPercent_%1").arg(sit)) != nullptr);
        // editItem is what typing calls; show the same text in the fields.
        scene.item(str("payrollCustomPercent_%1").arg(sit))->setProperty("text", str("4%"));
        scene.item(str("payrollCustomExtra_%1").arg(sit))->setProperty("text", str("5.00"));
        scrollIntoView(scene.item(str("payrollCustomItem_%1").arg(sit)));
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_payroll_employee_items_%1").arg(width));
        const QVariant employee = WindowScene::call(editor, "save");
        QVERIFY2(succeeded(employee), qPrintable(errorOf(employee)));
        closePopup(editor);

        // The preview: no Colorado columns, the business's own taxes as
        // "Other taxes": 4 % × 3,000 + 5 = 125.00; net 2,325.12.
        QObject *run = scene.openThrough(str("booksRunPayrollAction"), str("runPayrollDialog"));
        QVERIFY(run != nullptr);
        const QVariant preview = WindowScene::call(run, "previewRun");
        QVERIFY2(succeeded(preview), qPrintable(errorOf(preview)));
        const QVariantMap row = mapOf(preview).value(str("rows")).toList().first().toMap();
        QCOMPARE(row.value(str("otherText")).toString(), str("$125.00"));
        QCOMPARE(row.value(str("netText")).toString(), str("$2,325.12"));
        QTest::qWait(200);
        saveGrab(scene.window, str("qindabooks_run_payroll_items_%1").arg(width));
        closePopup(run);
    }
};

BOOKS_APP_TEST_MAIN(QindaBooksPayrollUiTest)

#include "tst_qindabooks_payroll_ui.moc"
