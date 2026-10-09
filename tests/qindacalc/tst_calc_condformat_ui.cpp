// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087) through the real QindaCalc window: Home ▸ Conditional
// Formatting — Data Bars, Color Scales and Icon Sets from the gallery
// commands (painted: a bar, a scale's colour, an icon), New Rule for the
// other kinds through CondFormatRuleDialog (Top 10, a text rule, a date
// rule, a formula), the Rules Manager (move, delete — one undo step each —
// and the rules kept as Excel wrote them, listed read-only), Clear Rules
// from the selection, a Warning validation's question (No takes the value
// back), and the recovery payload carrying every new field. QQO_GRAB_DIR
// saves the dialogs and the painted grid at 1280 and 520.

#include "calc_m78_support.h"

#include "calc_condformat_maps.h"
#include "sheet.h"

#include <QUndoStack>

using namespace CalcTest;
using namespace CalcTest::M78;
using namespace QindaOffice::Calc;

class CalcCondFormatUiTest : public QObject {
    Q_OBJECT

    static const QList<CondFormatRule> &rules(Scene &scene) { return scene.doc->workbook()->sheetAt(0).condFormats(); }
    // The parsed formulas are derived from the formula text, and the
    // defaulted == compares those shared pointers by identity: compare
    // everything else.
    static QList<CondFormatRule> withoutAst(QList<CondFormatRule> list)
    {
        for (CondFormatRule &rule : list) {
            rule.ast.reset();
            rule.ast2.reset();
        }
        return list;
    }
    static void numbers(Scene &scene)
    {
        const QList<int> values{-20, 5, 10, 40, 80, 100};
        for (int r = 0; r < values.size(); ++r) {
            type(scene, r, 0, QString::number(values.at(r)));
            type(scene, r, 1, QString::number(values.at(r)));
            type(scene, r, 2, QString::number(values.at(r)));
        }
    }
    static void select(Scene &scene, int r0, int c0, int r1, int c1)
    {
        scene.grid->setCursorCell(r0, c0);
        scene.grid->setCursorCell(r1, c1, true);
    }

private slots:
    void galleryCommandsPaint()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        numbers(scene);
        select(scene, 0, 0, 5, 0);
        trigger(scene, ActionId::kCalcCondFormatDataBars);
        select(scene, 0, 1, 5, 1);
        trigger(scene, ActionId::kCalcCondFormatColorScale);
        select(scene, 0, 2, 5, 2);
        trigger(scene, ActionId::kCalcCondFormatIconSet);
        QCOMPARE(rules(scene).size(), 3);
        QCOMPARE(rules(scene).first().op, CondFormatRule::Op::IconSet); // the newest first
        const Workbook &wb = *scene.doc->workbook();
        QVERIFY(wb.condFormatFor(0, {5, 0}).dataBar.has_value());
        QVERIFY(wb.condFormatFor(0, {0, 0}).dataBar->negative);
        QCOMPARE(wb.condFormatFor(0, {5, 1}).background, std::optional<Rgb>(Rgb{0x63, 0xbe, 0x7b}));
        QVERIFY(wb.condFormatFor(0, {5, 2}).icon.has_value());
        QCOMPARE(wb.condFormatFor(0, {5, 2}).icon->index, 2);
        QCOMPARE(wb.condFormatFor(0, {0, 2}).icon->index, 0);
        grab(scene, QStringLiteral("qindacalc_m78_cond_format_gallery_1280"));
        // Clear Rules from Selected Cells: the bar's column only.
        select(scene, 0, 0, 5, 0);
        trigger(scene, ActionId::kCalcCondFormatClearSelection);
        QCOMPARE(rules(scene).size(), 2);
        scene.doc->undoStack()->undo();
        QCOMPARE(rules(scene).size(), 3);
    }

    void newRuleForEveryKind()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        numbers(scene);
        type(scene, 0, 3, QStringLiteral("apple pie"));
        type(scene, 1, 3, QStringLiteral("pear"));
        select(scene, 0, 0, 5, 0);
        trigger(scene, ActionId::kCalcCondFormatTop10);
        QVERIFY(dialogOpened(scene, "condFormatRuleDialog"));
        QObject *dialog = popup(scene, "condFormatRuleDialog");
        QCOMPARE(dialog->property("kind").toInt(), 2);
        grab(scene, QStringLiteral("qindacalc_m78_cond_format_rule_1280"));
        QMetaObject::invokeMethod(dialog, "set", Q_ARG(QVariant, QStringLiteral("rank")), Q_ARG(QVariant, 2));
        QVERIFY(answer(scene, "condFormatRuleDialog", true));
        QCOMPARE(rules(scene).first().op, CondFormatRule::Op::Top10);
        QCOMPARE(rules(scene).first().rank, 2);
        QVERIFY(scene.doc->workbook()->condFormatFor(0, {5, 0}).background.has_value());
        QVERIFY(!scene.doc->workbook()->condFormatFor(0, {3, 0}).background.has_value());
        // Text, a formula and a date rule through the same dialog's map.
        const QList<QVariantMap> maps{
            {{QStringLiteral("range"), QStringLiteral("D1:D2")}, {QStringLiteral("op"), int(CondFormatRule::Op::BeginsWith)},
             {QStringLiteral("text"), QStringLiteral("app")}, {QStringLiteral("bold"), true}},
            {{QStringLiteral("range"), QStringLiteral("B1:B6")}, {QStringLiteral("op"), int(CondFormatRule::Op::Expression)},
             {QStringLiteral("formula"), QStringLiteral("=B1>$A$4")}, {QStringLiteral("background"), QStringLiteral("#c6efce")}},
            {{QStringLiteral("range"), QStringLiteral("C1:C6")}, {QStringLiteral("op"), int(CondFormatRule::Op::TimePeriod)},
             {QStringLiteral("period"), 0}, {QStringLiteral("background"), QStringLiteral("#ffeb9c")}}};
        for (const QVariantMap &map : maps) {
            trigger(scene, ActionId::kCalcCondFormatNewRule);
            QVERIFY(dialogOpened(scene, "condFormatRuleDialog"));
            QVariantMap rule = dialog->property("rule").toMap();
            for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
                rule.insert(it.key(), it.value());
            }
            dialog->setProperty("rule", rule);
            QVERIFY(answer(scene, "condFormatRuleDialog", true));
        }
        QCOMPARE(rules(scene).size(), 4);
        QVERIFY(scene.doc->workbook()->condFormatFor(0, {0, 3}).bold.value_or(false));
        QVERIFY(!scene.doc->workbook()->condFormatFor(0, {1, 3}).bold.has_value());
        QVERIFY(scene.doc->workbook()->condFormatFor(0, {5, 1}).background.has_value()); // 100 > 40
        QVERIFY(!scene.doc->workbook()->condFormatFor(0, {1, 1}).background.has_value());
        // The Rules Manager: the newest first, moved down, deleted.
        trigger(scene, ActionId::kCalcCondFormat);
        QVERIFY(dialogOpened(scene, "condFormatDialog"));
        QObject *manager = popup(scene, "condFormatDialog");
        QTRY_VERIFY(scene.item(QStringLiteral("condFormatRule_3")) != nullptr);
        grab(scene, QStringLiteral("qindacalc_m78_cond_format_manager_1280"));
        const int steps = scene.doc->undoStack()->count();
        manager->setProperty("selected", 0);
        QMetaObject::invokeMethod(manager, "move", Q_ARG(QVariant, 1));
        QCOMPARE(rules(scene).at(1).op, CondFormatRule::Op::TimePeriod);
        QMetaObject::invokeMethod(manager, "removeSelected");
        QCOMPARE(rules(scene).size(), 3);
        QCOMPARE(scene.doc->undoStack()->count(), steps + 2);
        QVERIFY(answer(scene, "condFormatDialog", true));
        // The recovery payload carries every field.
        SpreadsheetDocument restored;
        QString error;
        QVERIFY2(restored.loadRecovery(scene.doc->recoverySnapshot(), &error), qPrintable(error));
        QCOMPARE(withoutAst(restored.workbook()->sheetAt(0).condFormats()), withoutAst(rules(scene)));
        QVERIFY(restored.workbook()->sheetAt(0).condFormats().first().ast != nullptr); // parsed again
    }

    void warningValidationAsks()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.grid->setCursorCell(0, 0);
        scene.host.setValidationRule({{QStringLiteral("kind"), 0}, {QStringLiteral("operand1"), QStringLiteral("1")},
                                      {QStringLiteral("operand2"), QStringLiteral("10")}, {QStringLiteral("style"), 1},
                                      {QStringLiteral("errorTitle"), QStringLiteral("Check")},
                                      {QStringLiteral("prompt"), QStringLiteral("1 to 10")}});
        type(scene, 0, 0, QStringLiteral("42"));
        QVERIFY(dialogOpened(scene, "validationQuestion"));
        QCOMPARE(cell(scene, 0, 0, 0), Value(42.0)); // in the cell while it asks
        grab(scene, QStringLiteral("qindacalc_m78_validation_warning_1280"));
        QVERIFY(answer(scene, "validationQuestion", false)); // No: taken back
        QVERIFY(isBlank(cell(scene, 0, 0, 0)));
        type(scene, 0, 0, QStringLiteral("42"));
        QVERIFY(dialogOpened(scene, "validationQuestion"));
        QVERIFY(answer(scene, "validationQuestion", true)); // Yes: kept
        QCOMPARE(cell(scene, 0, 0, 0), Value(42.0));
        trigger(scene, ActionId::kCalcValidation);
        QVERIFY(dialogOpened(scene, "validationDialog"));
        popup(scene, "validationDialog")->setProperty("tab", 2);
        grab(scene, QStringLiteral("qindacalc_m78_validation_dialog_1280"));
        QVERIFY(answer(scene, "validationDialog", false));
    }

    void dialogsSqueezedTo520()
    {
        Scene scene(QSize(520, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        numbers(scene);
        select(scene, 0, 0, 5, 0);
        trigger(scene, ActionId::kCalcCondFormatNewRule);
        QVERIFY(dialogOpened(scene, "condFormatRuleDialog"));
        QMetaObject::invokeMethod(popup(scene, "condFormatRuleDialog"), "setKind", Q_ARG(QVariant, 0));
        grab(scene, QStringLiteral("qindacalc_m78_cond_format_rule_520"));
        QVERIFY(answer(scene, "condFormatRuleDialog", true));
        trigger(scene, ActionId::kCalcCondFormat);
        QVERIFY(dialogOpened(scene, "condFormatDialog"));
        grab(scene, QStringLiteral("qindacalc_m78_cond_format_manager_520"));
        QVERIFY(answer(scene, "condFormatDialog", true));
    }
};

CALC_TEST_MAIN(CalcCondFormatUiTest)

#include "tst_calc_condformat_ui.moc"
