// SPDX-License-Identifier: GPL-3.0-or-later
// M89 (D-097) through the real QindaCalc window at 1280 and 520 px: the
// formula bar's Formula AutoComplete in QindaTK's TextField suggestions —
// typing "=SUMI" lists SUMIF and SUMIFS with their arguments, Return puts
// "SUMIFS(" in place of what was typed; a table's name and, after "[", its
// columns; a defined name; Escape closes the list before it cancels.
// QQO_GRAB_DIR saves the windows with the list open.

#include "calc_test_support.h"

#include "standard_actions.h"
#include "support/quick_dump.h"

#include "sheet.h"

using namespace CalcTest;
using namespace QindaOffice::Calc;
namespace ActionId = QindaOffice::Quick::ActionId;

class CalcCompletionUiTest : public QObject {
    Q_OBJECT

    static void type(Scene &scene, int row, int col, const QString &text)
    {
        scene.grid->setCursorCell(row, col);
        scene.host.commitFormulaBar(text);
    }
    // Types `text` into the focused formula bar, a key at a time.
    static void keys(Scene &scene, const QString &text)
    {
        for (const QChar c : text) {
            QTest::keyClick(scene.window, c.toLatin1());
        }
    }
    static QVariantList shown(QQuickItem *field) { return field->property("suggestions").toList(); }
    static QStringList labelsOf(QQuickItem *field)
    {
        QStringList out;
        for (const QVariant &v : shown(field)) {
            out.append(v.toMap().value(QStringLiteral("label")).toString());
        }
        return out;
    }

private slots:
    void formulaAutoComplete_data()
    {
        QTest::addColumn<int>("width");
        QTest::newRow("1280") << 1280;
        QTest::newRow("520") << 520;
    }
    void formulaAutoComplete()
    {
        QFETCH(int, width);
        Scene scene(QSize(width, 760));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        // A table of sales and a defined name.
        const QStringList rows{QStringLiteral("Region,Qty,Unit Price"), QStringLiteral("North,2,10"),
                               QStringLiteral("South,5,4")};
        for (int r = 0; r < rows.size(); ++r) {
            const QStringList cells = rows.at(r).split(QLatin1Char(','));
            for (int c = 0; c < cells.size(); ++c) {
                type(scene, r, c, cells.at(c));
            }
        }
        scene.grid->setCursorCell(1, 1);
        scene.host.actions()->trigger(QString::fromLatin1(ActionId::kCalcInsertTable));
        QCOMPARE(scene.doc->workbook()->sheetAt(0).tables().size(), 1);
        DefinedName taxRate;
        taxRate.name = QStringLiteral("TaxRate");
        taxRate.formula = QStringLiteral("Sheet1!$F$1");
        scene.doc->workbook()->setDefinedNames({taxRate});

        scene.grid->setCursorCell(5, 0);
        QQuickItem *field = scene.item(QStringLiteral("formulaField"));
        QVERIFY(field != nullptr);
        field->forceActiveFocus();
        QTRY_VERIFY(field->hasActiveFocus());
        keys(scene, QStringLiteral("=SUMI"));
        QTRY_VERIFY(labelsOf(field).contains(QStringLiteral("SUMIFS")));
        QVERIFY(labelsOf(field).contains(QStringLiteral("SUMIF")));
        QTRY_VERIFY(field->property("suggestionsOpen").toBool());
        QVariantMap sumifs;
        for (const QVariant &v : shown(field)) {
            if (v.toMap().value(QStringLiteral("label")) == QStringLiteral("SUMIFS")) sumifs = v.toMap();
        }
        QCOMPARE(sumifs.value(QStringLiteral("detail")).toString(),
                 QStringLiteral("SUMIFS(sum_range, criteria_range1, criteria1, [criteria_range2, criteria2], …)"));
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m89_functions_%1").arg(width));
        // Down to SUMIFS, Return: the name typed so far becomes "SUMIFS(".
        const int at = labelsOf(field).indexOf(QStringLiteral("SUMIFS"));
        for (int i = 0; i < at; ++i) {
            QTest::keyClick(scene.window, Qt::Key_Down);
        }
        QTest::keyClick(scene.window, Qt::Key_Return);
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("=SUMIFS("));
        QCOMPARE(field->property("cursorPosition").toInt(), 8);
        // A table, then its columns.
        keys(scene, QStringLiteral("Tab"));
        QTRY_VERIFY(labelsOf(field).contains(QStringLiteral("Table1")));
        QTest::keyClick(scene.window, Qt::Key_Escape); // closes the list only
        QTRY_VERIFY(!field->property("suggestionsOpen").toBool());
        QVERIFY(field->hasActiveFocus());
        QCOMPARE(field->property("text").toString(), QStringLiteral("=SUMIFS(Tab"));
        keys(scene, QStringLiteral("le1[Un"));
        QTRY_COMPARE(labelsOf(field), QStringList{QStringLiteral("Unit Price")});
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindacalc_m89_columns_%1").arg(width));
        QTest::keyClick(scene.window, Qt::Key_Tab);
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("=SUMIFS(Table1[Unit Price]"));
        // A defined name.
        keys(scene, QStringLiteral(",Tax"));
        QTRY_VERIFY(labelsOf(field).contains(QStringLiteral("TaxRate")));
        QTest::keyClick(scene.window, Qt::Key_Return);
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("=SUMIFS(Table1[Unit Price],TaxRate"));
        QTRY_VERIFY(!field->property("suggestionsOpen").toBool());
        QCOMPARE(shown(field).size(), 0);
        // Escape with no list cancels the edit, as before.
        QTest::keyClick(scene.window, Qt::Key_Escape);
        QTRY_VERIFY(!field->hasActiveFocus());
    }
};

CALC_TEST_MAIN(CalcCompletionUiTest)
#include "tst_calc_completion_ui.moc"
