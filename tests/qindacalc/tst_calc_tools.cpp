// SPDX-License-Identifier: GPL-3.0-or-later
// The assistant's edits in QindaCalc (D-058): the snapshot lists filled
// cells with formulas and results, and set_cells / set_range / clear land
// as ONE undo step that recalculates.

#include "calc_test_support.h"

#include "assistant_controller.h"

#include <QJsonArray>
#include <QJsonDocument>

using namespace CalcTest;

class CalcToolsTest : public QObject {
    Q_OBJECT
private slots:
    void snapshotEditAndUndo();
};

void CalcToolsTest::snapshotEditAndUndo()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    scene.doc->applyInput({0, 0}, QStringLiteral("Month"));
    scene.doc->applyInput({0, 1}, QStringLiteral("Sales"));
    scene.doc->applyInput({5, 3}, QStringLiteral("stale"));
    auto *tools = scene.host.assistant()->documentTools();
    QVERIFY(tools != nullptr);
    const QString snapshot = tools->snapshot(60000);
    QVERIFY2(snapshot.contains(QStringLiteral("A1: Month | B1: Sales")), qPrintable(snapshot));
    QVERIFY(snapshot.contains(QStringLiteral("(active)")));

    const QJsonArray edits = QJsonDocument::fromJson(R"J([
        {"op":"set_range","start":"A2","rows":[["Jan",120],["Feb",95.5]]},
        {"op":"set_cells","cells":{"A4":"Total","B4":"=SUM(B2:B3)","ZZ":"bad"}},
        {"op":"clear","range":"D6"}])J").array();
    const int before = scene.doc->undoStack()->count();
    const auto outcome = tools->apply(edits);
    QCOMPARE(outcome.applied, 3);
    QCOMPARE(outcome.errors.size(), 1); // "ZZ" is not a cell
    QCOMPARE(scene.doc->undoStack()->count(), before + 1);
    QCOMPARE(scene.doc->displayTextAt({3, 1}), QStringLiteral("215.5"));
    QCOMPARE(scene.doc->inputTextAt({3, 1}), QStringLiteral("=SUM(B2:B3)"));
    QVERIFY(scene.doc->inputTextAt({5, 3}).isEmpty());
    QVERIFY(tools->snapshot(60000).contains(QStringLiteral("B4: =SUM(B2:B3) → 215.5")));

    tools->undo();
    QVERIFY(scene.doc->inputTextAt({3, 1}).isEmpty());
    QCOMPARE(scene.doc->inputTextAt({5, 3}), QStringLiteral("stale"));
}

CALC_TEST_MAIN(CalcToolsTest)

#include "tst_calc_tools.moc"
