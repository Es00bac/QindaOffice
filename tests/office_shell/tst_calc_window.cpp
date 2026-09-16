// SPDX-License-Identifier: GPL-3.0-or-later
// UI smoke: QindaCalc's window must construct, show, edit a cell through
// the document, and repaint offscreen without crashing (M3 regression for
// the shell's document-host refactor).

#include "qindacalc/src/calc_window.h"
#include "qindacalc/src/spreadsheet_document.h"

#include "standard_actions.h"
#include "libre_office_converter.h"

#include <QApplication>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUndoStack>
#include <QtTest>

using namespace QindaOffice::Apps::QindaCalc;
namespace Shell = QindaOffice::Shell;

class CalcWindowTest : public QObject {
    Q_OBJECT
private slots:
    void constructAndEdit();
    void undoBackToTheSavedStateClearsDirty();
    void saveCommandsAreLiveOnceTheEngineIsInstalled();
    void recoverySnapshotRoundTripsTheWholeWorkbook();
    void odsGoesThroughTheLibreOfficeBridge();
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

// Dirty tracks the undo stack's clean index. applySnapshot() used to latch
// setDirty(true), so Ctrl+Z all the way back to the saved state still asked
// "Save changes to …?" on close.
void CalcWindowTest::undoBackToTheSavedStateClearsDirty()
{
    CalcWindow window;
    SpreadsheetDocument *doc = window.spreadsheetDocument();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/clean.xlsx");
    QString error;
    QVERIFY2(doc->save(path, &error), qPrintable(error));
    QVERIFY(!doc->isDirty());

    doc->applyInput({0, 0}, QStringLiteral("7"));
    QVERIFY(doc->isDirty());
    doc->undoStack()->undo();
    QVERIFY(!doc->isDirty());
    doc->undoStack()->redo();
    QVERIFY(doc->isDirty());
}

// AGENTS.md rule 1 / plan 5.1: the app installs its engine through
// setDocumentHost(), and Save must be reachable from the menu, the toolbar
// and Ctrl+S — all three were permanently disabled.
void CalcWindowTest::saveCommandsAreLiveOnceTheEngineIsInstalled()
{
    CalcWindow window;
    QAction *save = window.action(QString::fromLatin1(Shell::ActionId::kFileSave));
    QAction *saveAs = window.action(QString::fromLatin1(Shell::ActionId::kFileSaveAs));
    QVERIFY(save != nullptr);
    QVERIFY(saveAs != nullptr);
    QVERIFY(save->isEnabled());
    QVERIFY(saveAs->isEnabled());
    QCOMPARE(save->shortcut(), QKeySequence(QKeySequence::Save));
}

// Crash recovery is only real if the payload carries everything the app
// models. Both engines used to return {} from recoverySnapshot(), so a
// QindaCalc crash lost every unsaved cell.
void CalcWindowTest::recoverySnapshotRoundTripsTheWholeWorkbook()
{
    SpreadsheetDocument doc;
    doc.applyInput({0, 0}, QStringLiteral("41"));
    doc.applyInput({1, 0}, QStringLiteral("=A1*2"));
    doc.applyInput({2, 0}, QStringLiteral("'007"));      // text that looks numeric
    doc.applyInput({3, 0}, QStringLiteral("=1/0"));      // an error value
    StylePatch patch;
    patch.bold = true;
    patch.numberFormat = QStringLiteral("#,##0.00");
    doc.restyleRange({{5, 5}, {5, 5}}, patch, QStringLiteral("Style")); // an EMPTY cell
    doc.insertSheet(QStringLiteral("Second"));
    doc.workbook()->defineName(QStringLiteral("TOTAL"),
                               CalcEng::RangeRef{QStringLiteral("Sheet1"),
                                                 {{0, 0}, {3, 0}}, true, true, true, true});
    CalcEng::ChartSpec chart;
    chart.title = QStringLiteral("Trend");
    chart.rangeText = QStringLiteral("A1:A4");
    doc.addChart(chart);

    const QByteArray payload = doc.recoverySnapshot();
    QVERIFY(!payload.isEmpty());

    SpreadsheetDocument restored;
    QString error;
    QVERIFY2(restored.loadRecovery(payload, &error), qPrintable(error));
    const CalcEng::Workbook *wb = restored.workbook();
    QCOMPARE(wb->sheetCount(), 2);
    QCOMPARE(wb->sheetName(1), QStringLiteral("Second"));
    // Formulas are re-parsed and recomputed, not restored as stale values.
    QCOMPARE(toText(wb->cellValue(0, 1, 0)), QStringLiteral("82"));
    // A text cell that looks like a number stays text.
    QVERIFY(CalcEng::isText(wb->cellValue(0, 2, 0)));
    QCOMPARE(toText(wb->cellValue(0, 2, 0)), QStringLiteral("007"));
    QVERIFY(CalcEng::isErr(wb->cellValue(0, 3, 0)));
    // A style set on an empty cell survives (cells() never held it).
    const CalcEng::CellStyle *style = wb->style(wb->sheetAt(0).styleIdAt({5, 5}));
    QVERIFY(style != nullptr);
    QVERIFY(style->bold);
    QCOMPARE(style->numberFormat, QStringLiteral("#,##0.00"));
    // Named ranges and charts are part of the document, so part of recovery.
    const auto name = wb->namedRange(QStringLiteral("TOTAL"));
    QVERIFY(name.has_value());
    QCOMPARE(name->range.bottomRight.row, 3);
    QCOMPARE(wb->charts.size(), 1);
    QCOMPARE(wb->charts.first().title, QStringLiteral("Trend"));
    QVERIFY(restored.isDirty()); // recovered content is unsaved by definition
}

// The .desktop file has always declared ods/xls, but load() refused anything
// that was not xlsx/csv — so opening an .ods from the file manager launched
// QindaCalc only to show "Unsupported format".
void CalcWindowTest::odsGoesThroughTheLibreOfficeBridge()
{
    QString diagnostic;
    if (!QindaOffice::Core::LibreOfficeConverter::probe(&diagnostic)) {
        QSKIP(qPrintable(QStringLiteral("LibreOffice unavailable: %1").arg(diagnostic)));
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/book.ods");
    QString error;
    SpreadsheetDocument doc;
    doc.applyInput({0, 0}, QStringLiteral("21"));
    doc.applyInput({1, 0}, QStringLiteral("=A1*2"));
    QVERIFY2(doc.save(path, &error), qPrintable(error));
    QVERIFY(QFileInfo(path).size() > 0);

    SpreadsheetDocument back;
    QVERIFY2(back.load(path, &error), qPrintable(error));
    QCOMPARE(toText(back.workbook()->cellValue(0, 1, 0)), QStringLiteral("42"));
    // The declared MIME list and the engine must agree (shell rule 2).
    QVERIFY(back.mimeTypes().contains(
        QStringLiteral("application/vnd.oasis.opendocument.spreadsheet")));
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
