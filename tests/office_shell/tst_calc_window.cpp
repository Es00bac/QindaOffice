// SPDX-License-Identifier: GPL-3.0-or-later
// UI smoke: QindaCalc's window must construct, show, edit a cell through
// the document, and repaint offscreen without crashing (M3 regression for
// the shell's document-host refactor).

#include "qindacalc/src/calc_window.h"
#include "qindacalc/src/spreadsheet_document.h"

#include <QApplication>
#include <QtTest>

using namespace QindaOffice::Apps::QindaCalc;

class CalcWindowTest : public QObject {
    Q_OBJECT
private slots:
    void constructAndEdit();
};

void CalcWindowTest::constructAndEdit()
{
    CalcWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    SpreadsheetDocument *doc = window.spreadsheetDocument();
    QVERIFY(doc != nullptr);
    doc->applyInput({0, 0}, QStringLiteral("41"));
    doc->applyInput({1, 0}, QStringLiteral("=A1*2"));
    QCOMPARE(toText(doc->workbook()->cellValue(0, 1, 0)), QStringLiteral("82"));
    QVERIFY(doc->isDirty());

    // Save and reload through the document interface.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/t.xlsx");
    QString error;
    QVERIFY2(doc->save(path, &error), qPrintable(error));
    SpreadsheetDocument reloaded;
    QVERIFY2(reloaded.load(path, &error), qPrintable(error));
    QCOMPARE(toText(reloaded.workbook()->cellValue(0, 1, 0)), QStringLiteral("82"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaCalc"));
    CalcWindowTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_window.moc"
