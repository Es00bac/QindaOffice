// SPDX-License-Identifier: GPL-3.0-or-later
// QindaCalc's document and host: a workbook edits, saves and reloads
// through the document; undo back to the saved state clears dirty; the
// recovery payload round-trips the whole workbook; ods goes through the
// LibreOffice bridge; a fresh host is pristine so Open can reuse it.

#include "calc_test_support.h"

#include "libre_office_converter.h"
#include "standard_actions.h"

#include "sheet.h"


#include <QFileInfo>
#include <QUndoStack>

#include <poppler-qt6.h>

using namespace CalcTest;
using namespace QindaOffice::Apps::QindaCalc;
namespace ActionId = QindaOffice::Quick::ActionId;
namespace Calc = QindaOffice::Calc;

class CalcWindowTest final : public QObject {
    Q_OBJECT
private slots:
    void constructAndEdit()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        SpreadsheetDocument *doc = scene.doc;
        doc->applyInput({0, 0}, QStringLiteral("41"));
        doc->applyInput({1, 0}, QStringLiteral("=A1*2"));
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 1, 0)), QStringLiteral("82"));
        QVERIFY(doc->isDirty());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/t.xlsx");
        QString error;
        QVERIFY2(doc->save(path, &error), qPrintable(error));
        SpreadsheetDocument reloaded;
        QVERIFY2(reloaded.load(path, &error), qPrintable(error));
        QCOMPARE(Calc::toText(reloaded.workbook()->cellValue(0, 1, 0)), QStringLiteral("82"));
    }

    // Dirty tracks the undo stack's clean index: Ctrl+Z back to the saved
    // state must not ask "Save changes?" on close.
    void undoBackToTheSavedStateClearsDirty()
    {
        CalcHost host;
        SpreadsheetDocument *doc = host.spreadsheetDocument();
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/clean.xlsx");
        doc->applyInput({0, 0}, QStringLiteral("1"));
        QVERIFY(doc->save(path));
        QVERIFY(!doc->isDirty());
        doc->applyInput({0, 0}, QStringLiteral("2"));
        QVERIFY(doc->isDirty());
        doc->undoStack()->undo();
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 0, 0)), QStringLiteral("1"));
        QVERIFY(!doc->isDirty());
        // Redo has to rebuild here, where pushing the command did not: the
        // push already found the workbook holding its "after" state.
        doc->undoStack()->redo();
        QCOMPARE(Calc::toText(doc->workbook()->cellValue(0, 0, 0)), QStringLiteral("2"));
        QVERIFY(doc->isDirty());
    }

    void saveCommandsAreLive()
    {
        CalcHost host;
        for (const char *id : {ActionId::kFileSave, ActionId::kFileSaveAs}) {
            QVERIFY2(host.actions()->entry(QString::fromLatin1(id))->isEnabled(), id);
        }
        QCOMPARE(host.actions()->entry(QString::fromLatin1(ActionId::kFileSave))->shortcut(),
                 QKeySequence(QKeySequence::Save));
        QVERIFY(host.actions()->entry(QString::fromLatin1(ActionId::kFilePrint))->isEnabled());
    }

    void recoverySnapshotRoundTripsTheWholeWorkbook()
    {
        SpreadsheetDocument doc;
        doc.applyInput({0, 0}, QStringLiteral("10"));
        doc.insertSheet(QStringLiteral("Second"));
        doc.setActiveSheet(1);
        doc.applyInput({0, 0}, QStringLiteral("=Sheet1!A1*3"));
        StylePatch patch;
        patch.bold = true;
        patch.strikeout = true;
        patch.vAlign = 0;
        doc.restyleRange({{0, 0}, {0, 0}}, patch, QStringLiteral("Bold"));
        Calc::RangeRef total;
        total.sheet = QStringLiteral("Sheet1");
        total.range = {{0, 0}, {0, 0}};
        doc.defineName(QStringLiteral("TOTAL"), total);
        Calc::ChartSpec chart;
        chart.title = QStringLiteral("t");
        chart.rangeText = QStringLiteral("A1:A1");
        doc.addChart(chart);
        const QByteArray payload = doc.recoverySnapshot();
        SpreadsheetDocument restored;
        QString error;
        QVERIFY2(restored.loadRecovery(payload, &error), qPrintable(error));
        QCOMPARE(restored.workbook()->sheetCount(), 2);
        QCOMPARE(Calc::toText(restored.workbook()->cellValue(1, 0, 0)), QStringLiteral("30"));
        const Calc::CellStyle *restyledStyle =
            restored.workbook()->style(restored.workbook()->sheetAt(1).styleIdAt({0, 0}));
        QVERIFY(restyledStyle != nullptr);
        QVERIFY(restyledStyle->bold);
        QVERIFY(restyledStyle->strikeout);
        QCOMPARE(restyledStyle->vAlign, 0);
        QVERIFY(restored.workbook()->namedRange(QStringLiteral("TOTAL")).has_value());
        QCOMPARE(restored.workbook()->charts.size(), 1);
    }

    void odsGoesThroughTheLibreOfficeBridge()
    {
        if (!QindaOffice::Core::LibreOfficeConverter::probe()) {
            QSKIP("LibreOffice is not installed");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        SpreadsheetDocument doc;
        doc.applyInput({0, 0}, QStringLiteral("7"));
        const QString path = dir.path() + QStringLiteral("/bridge.ods");
        QString error;
        QVERIFY2(doc.save(path, &error), qPrintable(error));
        SpreadsheetDocument back;
        QVERIFY2(back.load(path, &error), qPrintable(error));
        QCOMPARE(Calc::toText(back.workbook()->cellValue(0, 0, 0)), QStringLiteral("7"));
    }

    // Floating pictures (M39): the Insert action lands one at the cursor as
    // a single undo step, the grid actually paints it, and the recovery
    // payload carries the bytes back.
    void imagesInsertPaintAndRecover()
    {
        Scene scene(QSize(1000, 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));

        QImage source(40, 30, QImage::Format_RGB32);
        source.fill(QColor(20, 200, 90)); // a green nothing else on the grid is
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString file = dir.path() + QStringLiteral("/logo.png");
        QVERIFY(source.save(file));

        scene.grid->setCursorCell(4, 2);
        QVERIFY(scene.doc->addImageFromFile(file, 4, 2));
        QCOMPARE(scene.doc->workbook()->images.size(), 1);
        const Calc::ImageSpec &spec = scene.doc->workbook()->images.at(0);
        QCOMPARE(spec.format, QStringLiteral("png"));
        QCOMPARE(spec.anchorRow, 4);
        QCOMPARE(spec.anchorCol, 2);
        QCOMPARE(spec.widthPx, 40);

        // It paints: the picture is anchored at the cursor cell's top-left,
        // so cursorItemRect locates it without duplicating the header and
        // scroll arithmetic. Sample well inside, clear of the 2px cursor
        // outline that the same cell draws.
        const QPointF origin = scene.grid->mapToScene(scene.grid->cursorItemRect().topLeft());
        const int x = int(origin.x()) + 12;
        const int y = int(origin.y()) + 10;
        QImage grab;
        QTRY_VERIFY_WITH_TIMEOUT((grab = scene.window->grabWindow(), !grab.isNull()), 5000);
        const QString grabDir = qEnvironmentVariable("QQO_GRAB_DIR");
        if (!grabDir.isEmpty()) {
            grab.save(grabDir + QStringLiteral("/qindacalc_image.png"));
        }
        const QColor pixel = grab.pixelColor(x, y);
        qInfo() << "image pixel" << pixel << "at" << x << y;
        QVERIFY2(pixel.green() > pixel.red() + 40 && pixel.green() > pixel.blue() + 40,
                 qPrintable(pixel.name()));

        // One undo step, and the bytes survive a recovery round trip.
        const QByteArray payload = scene.doc->recoverySnapshot();
        scene.doc->undoStack()->undo();
        QCOMPARE(scene.doc->workbook()->images.size(), 0);
        scene.doc->undoStack()->redo();
        QCOMPARE(scene.doc->workbook()->images.size(), 1);

        SpreadsheetDocument restored;
        QString error;
        QVERIFY2(restored.loadRecovery(payload, &error), qPrintable(error));
        QCOMPARE(restored.workbook()->images.size(), 1);
        QCOMPARE(*restored.workbook()->images.at(0).bytes,
                 *scene.doc->workbook()->images.at(0).bytes);
    }

    // A chart window paints actual series pixels, not a blank panel (the
    // custom ChartItem — D-036; the QtCharts ChartView path crashed under
    // QGuiApplication before this existed).
    void chartWindowsRender()
    {
        const QString path = qEnvironmentVariable("QQO_SAMPLE_XLSX");
        if (path.isEmpty()) {
            QSKIP("QQO_SAMPLE_XLSX not set");
        }
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host.loadPath(path));
        QTRY_VERIFY_WITH_TIMEOUT(!scene.host.isLoading(), 300000);
        QQuickWindow *chartWindow = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                for (QWindow *window : QGuiApplication::allWindows()) {
                    if (window->objectName() == QLatin1String("chartWindow_0")) {
                        chartWindow = qobject_cast<QQuickWindow *>(window);
                        return true;
                    }
                }
                return false;
            }(),
            10000);
        QVERIFY(chartWindow != nullptr);
        QImage grab;
        QTRY_VERIFY_WITH_TIMEOUT((grab = chartWindow->grabWindow(), !grab.isNull()), 5000);
        const QString grabDir = qEnvironmentVariable("QQO_GRAB_DIR");
        if (!grabDir.isEmpty()) {
            for (QWindow *window : QGuiApplication::allWindows()) {
                if (window->objectName().startsWith(QLatin1String("chartWindow_"))) {
                    qobject_cast<QQuickWindow *>(window)->grabWindow().save(
                        grabDir + QStringLiteral("/qindacalc_%1.png").arg(window->objectName()));
                }
            }
        }
        // A rendered plot has several distinct colours in its centre (bars/
        // lines, gridlines, panel); a blank panel has one.
        QSet<QRgb> colours;
        for (int y = grab.height() / 4; y < grab.height() * 3 / 4; y += 7) {
            for (int x = grab.width() / 4; x < grab.width() * 3 / 4; x += 7) {
                colours.insert(grab.pixel(x, y));
            }
        }
        QVERIFY2(colours.size() > 3, qPrintable(QStringLiteral("colours: %1").arg(colours.size())));
    }

    // Freeze panes are document state: the toggle lands on the sheet, undo
    // clears it, and the recovery payload carries it.
    void freezeToggleIsUndoableAndRecovered()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.doc->applyInput({2, 2}, QStringLiteral("1"));
        scene.grid->setCursorCell(2, 2);
        scene.host.actions()->entry(QString::fromLatin1("calcFreezePanesAction"))
            ->setChecked(true);
        QTRY_COMPARE(scene.doc->workbook()->sheetAt(0).freezeRows(), 2);
        QCOMPARE(scene.doc->workbook()->sheetAt(0).freezeCols(), 2);
        scene.doc->undoStack()->undo();
        QCOMPARE(scene.doc->workbook()->sheetAt(0).freezeRows(), 0);
        scene.doc->undoStack()->redo();
        QCOMPARE(scene.doc->workbook()->sheetAt(0).freezeRows(), 2);

        scene.doc->applyInput({0, 0}, QStringLiteral("x"));
        const QByteArray payload = scene.doc->recoverySnapshot();
        SpreadsheetDocument restored;
        QString error;
        QVERIFY2(restored.loadRecovery(payload, &error), qPrintable(error));
        QCOMPARE(restored.workbook()->sheetAt(0).freezeRows(), 2);
        QCOMPARE(restored.workbook()->sheetAt(0).freezeCols(), 2);
    }

    // Hidden sheets have no tab, and the strip's view indices map to the
    // right engine sheets.
    void hiddenSheetsLeaveTheTabStrip()
    {
        CalcHost host;
        SpreadsheetDocument *doc = host.spreadsheetDocument();
        doc->insertSheet(QStringLiteral("Second"));
        doc->insertSheet(QStringLiteral("Third"));
        QCOMPARE(host.sheetNames().size(), 3);
        host.setActiveSheet(0);
        doc->setSheetHidden(0, true); // hides "Sheet1"
        QCOMPARE(host.sheetNames(), (QStringList{QStringLiteral("Second"), QStringLiteral("Third")}));
        // Hiding the active sheet moved the active one to a visible sheet,
        // and activating view index 1 reaches engine sheet 2.
        QCOMPARE(doc->workbook()->sheetName(doc->activeSheet()), QStringLiteral("Second"));
        host.setActiveSheet(1);
        QCOMPARE(doc->activeSheet(), 2);
        QCOMPARE(host.activeSheet(), 1);
        // Unhide restores the tab at its position.
        doc->setSheetHidden(0, false);
        QCOMPARE(host.sheetNames().size(), 3);
    }

    // The print path honors the sheet's print area: rows outside it never
    // reach the PDF.
    void printHonorsThePrintArea()
    {
        CalcHost host;
        SpreadsheetDocument *doc = host.spreadsheetDocument();
        for (int r = 0; r < 20; ++r) {
            doc->applyInput({r, 0}, QString::number(r));
        }
        doc->workbook()->sheetAt(0).setPrintArea(Calc::CellRange{{0, 0}, {4, 0}});
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/area.pdf");
        QVERIFY(host.exportPdf(path));
        std::unique_ptr<Poppler::Document> pdf(Poppler::Document::load(path));
        QVERIFY(pdf != nullptr);
        QString allText;
        for (int page = 0; page < pdf->numPages(); ++page) {
            std::unique_ptr<Poppler::Page> pdfPage(pdf->page(page));
            allText += pdfPage->text(QRectF());
        }
        QVERIFY(allText.contains(QStringLiteral("4")));
        QVERIFY2(!allText.contains(QStringLiteral("15")), qPrintable(allText.left(400)));
    }

    // AGENT-CONTRACT (ADR-0110): a freshly created host is PRISTINE, which
    // lets File ▸ Open replace its blank workbook instead of opening a
    // second window and leaving the blank one behind.
    void freshHostIsPristineSoOpenCanReuseIt()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(!scene.doc->isDirty());
        QVERIFY(scene.host.isPristineUntitled());
        QCOMPARE(scene.host.sheetNames(), QStringList{QStringLiteral("Sheet1")});
    }
};

CALC_TEST_MAIN(CalcWindowTest)

#include "tst_calc_window.moc"
