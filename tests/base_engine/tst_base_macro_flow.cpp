// SPDX-License-Identifier: GPL-3.0-or-later
#include "base_fixture.h"
#include "base_macro_flow.h"

using namespace BaseTest;

class BaseMacroFlowTest final : public QObject {
    Q_OBJECT
private slots:
    void draftValuesAndTypedTemporaryCurrency()
    {
        Db db;
        db.create(table("gifts", {field("amount", FieldType::Currency), field("name", FieldType::Text),
                                  field("approved", FieldType::YesNo)}));
        const QVariantMap record{{str("amount"), str("123.45")}, {str("name"), str("Ada")}, {str("approved"), false}};
        auto value = evaluateMacroValue(*db.file, str("gifts"), str("[amount] > 100"), record, {});
        QVERIFY2(value.ok, qPrintable(value.error));
        QVERIFY(value.value.toBool());
        value = evaluateMacroValue(*db.file, str("gifts"), str("[amount] * 2"), record, {}, str("amount"));
        QVERIFY2(value.ok, qPrintable(value.error));
        QCOMPARE(value.value.toLongLong(), qint64(24690));
        QCOMPARE(value.input.toString(), displayText(FieldType::Currency, qint64(24690), db.file->currency()));
        const QVariantMap variables{{str("Budget"), QVariantMap{{str("value"), qint64(15000)}, {str("type"), str("currency")}}}};
        value = evaluateMacroValue(*db.file, str("gifts"), str("[TempVars].[Budget] - [amount]"), record, variables, str("amount"));
        QVERIFY2(value.ok, qPrintable(value.error));
        QCOMPARE(value.value.toLongLong(), qint64(2655));
        value = evaluateMacroValue(*db.file, str("gifts"), str("True"), record, {}, str("approved"));
        QVERIFY2(value.ok, qPrintable(value.error));
        QCOMPARE(value.input, QVariant(true));
        QCOMPARE(db.file->db().scalar(str("SELECT Count(*) FROM gifts")).toInt(), 0); // read-only draft evaluation
    }

    void refusesUnknownFieldsInvalidInputAndSql()
    {
        Db db;
        db.create(table("gifts", {field("amount", FieldType::Currency)}));
        const QVariantMap record{{str("amount"), str("10.00")}};
        for (const QString &text : {str("[missing]"), str("[TempVars].[missing]"), str("1; DELETE FROM gifts"), str("Sum([amount])")})
            QVERIFY(!evaluateMacroValue(*db.file, str("gifts"), text, record, {}).ok);
        QVERIFY(!evaluateMacroValue(*db.file, str("gifts"), str("1"), {{str("amount"), str("bad amount")}}, {}).ok);
        QVERIFY(!evaluateMacroValue(*db.file, str("gifts"), str("1"), record, {}, str("missing")).ok);
        QVERIFY(db.file->tableExists(str("gifts")));
    }

    void branchesMustBalanceAndRoundTrip()
    {
        Db db;
        Macro macro;
        macro.steps = {{str("if"), {{str("condition"), str("[amount] > 100")}}},
                       {str("setValue"), {{str("field"), str("approved")}, {str("expression"), str("True")}}},
                       {str("else"), {}}, {str("cancelEvent"), {{str("message"), str("Too small")}}}, {str("endIf"), {}}};
        QVERIFY2(checkMacro(*db.file, macro).isEmpty(), qPrintable(checkMacro(*db.file, macro).join(str("; "))));
        QCOMPARE(Macro::fromMap(macro.toMap()).toMap(), macro.toMap());
        macro.steps.removeLast();
        QVERIFY(!checkMacroFlow(macro).isEmpty());
        macro.steps = {{str("else"), {}}, {str("endIf"), {}}};
        QCOMPARE(checkMacroFlow(macro).size(), 2);
        macro.steps = {{str("if"), {{str("condition"), str("True")}}}, {str("else"), {}}, {str("else"), {}}, {str("endIf"), {}}};
        QCOMPARE(checkMacroFlow(macro).size(), 1);
    }
};
QTEST_GUILESS_MAIN(BaseMacroFlowTest)
#include "tst_base_macro_flow.moc"
