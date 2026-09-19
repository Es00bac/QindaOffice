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
        QVERIFY(!doc->isDirty());
        doc->undoStack()->redo();
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

    // A real-world Excel workbook (absolute part names, no <dimension>,
    // tables, drawings, charts): set QQO_SAMPLE_XLSX to a path and the test
    // loads it, prints one line per sheet (cell count, error cells with up
    // to six samples) and requires every sheet to arrive. Error cells are
    // counted with isErr on the VALUE — a literal text cell like "#" is not
    // an error (the old startsWith('#') heuristic flagged one).
    void opensARealExcelWorkbook()
    {
        const QString path = qEnvironmentVariable("QQO_SAMPLE_XLSX");
        if (path.isEmpty()) {
            QSKIP("QQO_SAMPLE_XLSX not set");
        }
        SpreadsheetDocument doc;
        QString error;
        const bool ok = doc.load(path, &error);
        QVERIFY2(ok, qPrintable(error));
        int totalErrors = 0;
        for (qint32 s = 0; s < doc.workbook()->sheetCount(); ++s) {
            const Calc::Sheet &sheet = doc.workbook()->sheetAt(s);
            int errors = 0;
            QStringList samples;
            for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
                if (!Calc::isErr(it.value().value)) {
                    continue;
                }
                const Calc::CellAddr addr{qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)};
                ++errors;
                if (samples.size() < 6) {
                    samples.append(Calc::addrText(addr) + QLatin1Char('=') + doc.inputTextAt(addr).left(60)
                                   + QStringLiteral(" -> ") + doc.displayTextAt(s, addr));
                }
            }
            totalErrors += errors;
            qInfo() << "sheet" << s << doc.workbook()->sheetName(s) << "cells" << sheet.cells().size()
                    << "errors" << errors;
            for (const QString &sample : samples) {
                qInfo() << "   " << sample;
            }
        }
        QVERIFY(doc.workbook()->sheetCount() >= 2);
        // Not one error cell anywhere in the book. The last twelve were the
        // array-arithmetic gap closed in M38 (SUMPRODUCT over a powered
        // range); any error appearing here again is an import regression.
        QCOMPARE(totalErrors, 0);
    }

    // The same file through File ▸ Open's path (loadPath): the window must
    // retitle, the grid must actually paint, and the sheet strip must show
    // one tab per sheet — the TabStrip regression that hid every tab lived
    // exactly here (a QStringList model is not Array.isArray). The load is
    // asynchronous (worker thread), so completion is awaited, not slept.
    void windowResponsiveAfterSampleLoad()
    {
        const QString path = qEnvironmentVariable("QQO_SAMPLE_XLSX");
        if (path.isEmpty()) {
            QSKIP("QQO_SAMPLE_XLSX not set");
        }
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QElapsedTimer timer;
        timer.start();
        QVERIFY(scene.host.loadPath(path));
        QTRY_VERIFY_WITH_TIMEOUT(!scene.host.isLoading(), 300000);
        qInfo() << "load + paint" << timer.elapsed() << "ms";
        QVERIFY2(scene.host.windowTitle().contains(QFileInfo(path).fileName()),
                 qPrintable(scene.host.windowTitle()));
        QCOMPARE(scene.host.sheetNames().size(), 11);
        QQuickItem *strip = scene.item("sheetStrip");
        QVERIFY(strip != nullptr);
        QCOMPARE(strip->property("count").toInt(), scene.host.sheetNames().size());
        qInfo() << "title" << scene.host.windowTitle() << "status" << scene.host.statusInfo()
                << "A1" << scene.doc->displayTextAt(0, {0, 0}) << "B2" << scene.doc->displayTextAt(0, {1, 1});
        // Painted, not blank: the banner band (solid, above the glyphs) and
        // a far empty cell must differ (a dead paint gives one uniform color
        // everywhere). The adoption signals land one frame after isLoading
        // drops, so poll.
        QImage grab;
        QTRY_VERIFY_WITH_TIMEOUT(
            (grab = scene.window->grabWindow(),
             grab.pixelColor(400, 140) != grab.pixelColor(1200, 700)),
            10000);
        const QString grabDir = qEnvironmentVariable("QQO_GRAB_DIR");
        if (!grabDir.isEmpty()) {
            grab.save(grabDir + QStringLiteral("/qindacalc_northstar.png"));
        }
    }

    // Tabs outgrow narrow windows: every tab must be scrollable into the
    // strip's viewport, and switching sheets (keyboard, menu) must reveal
    // the newly active tab on its own.
    void allTabsReachableAtSmallWidths()
    {
        Scene scene(QSize(900, 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        for (int i = 1; i <= 12; ++i) {
            scene.doc->insertSheet(QStringLiteral("Very Long Sheet Name %1").arg(i));
        }
        QQuickItem *strip = scene.item("sheetStrip");
        QVERIFY(strip != nullptr);
        QTRY_COMPARE(strip->property("count").toInt(), 13);
        QQuickItem *flick = scene.item("sheetTabFlick");
        QVERIFY(flick != nullptr);
        QTRY_VERIFY(flick->property("overflows").toBool());
        for (int i = 0; i < 13; ++i) {
            scene.host.setActiveSheet(i);
            QQuickItem *tab = scene.item(QStringLiteral("tab_%1").arg(i));
            QVERIFY(tab != nullptr);
            QTRY_VERIFY(tab->mapToItem(flick, QPointF(0, 0)).x() >= -0.5
                        && tab->mapToItem(flick, QPointF(tab->width(), 0)).x()
                            <= flick->width() + 0.5);
        }
    }

    // Imported rules are live: the sample's list validations enforce on the
    // one edit path every UI edit takes, its conditional formats arrive with
    // their precedence and dxfs, and its data bars measure against the
    // range's values.
    void sampleRulesEnforceAndImport()
    {
        const QString path = qEnvironmentVariable("QQO_SAMPLE_XLSX");
        if (path.isEmpty()) {
            QSKIP("QQO_SAMPLE_XLSX not set");
        }
        SpreadsheetDocument doc;
        QString error;
        QVERIFY2(doc.load(path, &error), qPrintable(error));
        int condFormats = 0;
        int validations = 0;
        for (qint32 s = 0; s < doc.workbook()->sheetCount(); ++s) {
            condFormats += doc.workbook()->sheetAt(s).condFormats().size();
            validations += doc.workbook()->sheetAt(s).validations().size();
        }
        QCOMPARE(condFormats, 8);
        QCOMPARE(validations, 9);

        // Controls!B5 is a scenario dropdown (Downside/Base/Upside): a value
        // outside the list is refused with a reason; a listed one goes in.
        const qint32 controls = doc.workbook()->sheetIndex(QStringLiteral("Controls"));
        QVERIFY(controls >= 0);
        doc.setActiveSheet(controls);
        const QString before = Calc::toText(doc.workbook()->cellValue(controls, 4, 1));
        QSignalSpy rejected(&doc, &SpreadsheetDocument::inputRejected);
        doc.applyInput({4, 1}, QStringLiteral("Bogus"));
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(Calc::toText(doc.workbook()->cellValue(controls, 4, 1)), before);
        doc.applyInput({4, 1}, QStringLiteral("Upside"));
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(Calc::toText(doc.workbook()->cellValue(controls, 4, 1)), QStringLiteral("Upside"));

        // Monthly!C8:C43 carries a dataBar: the extent spans the column's
        // numbers, so C8 (the smallest) shows ~0 and a later cell more.
        const qint32 monthly = doc.workbook()->sheetIndex(QStringLiteral("Monthly"));
        QVERIFY(monthly >= 0);
        const Calc::CondFormat low = doc.workbook()->condFormatFor(monthly, {7, 2});
        QVERIFY(low.dataBar.has_value());
        QCOMPARE(low.dataBar->fraction, 0.0);
    }

    // The sample's six Excel tables import with names, ranges and style
    // info, and the banding paints: a stripe row differs from its unbanded
    // neighbour on screen.
    void sampleTablesImportAndBand()
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
        int tables = 0;
        QStringList names;
        for (qint32 s = 0; s < scene.doc->workbook()->sheetCount(); ++s) {
            tables += scene.doc->workbook()->sheetAt(s).tables().size();
            for (const Calc::TableSpec &table : scene.doc->workbook()->sheetAt(s).tables()) {
                names.append(table.name);
            }
        }
        QCOMPARE(tables, 6);
        QVERIFY(names.contains(QStringLiteral("OrderLedger")));
        QVERIFY(names.contains(QStringLiteral("MonthlyPerformance")));

        // Orders!OrderLedger spans A7:R5007: header at sheet row 7, body
        // from 8 — row 9 is banded, row 8 is not. Column C, mid-cell, away
        // from the cursor. rowTopForTest is 1-based; add the grid's scene
        // position for window coordinates.
        const qint32 orders = scene.doc->workbook()->sheetIndex(QStringLiteral("Orders"));
        QVERIFY(orders >= 0);
        scene.host.setActiveSheet(orders);
        const int yBase = int(scene.grid->mapToScene(QPointF(0, 0)).y());
        const int yHeader = yBase + scene.grid->rowTopForTest(7) + 6;
        const int yPlain = yBase + scene.grid->rowTopForTest(8) + 6;
        const int yBanded = yBase + scene.grid->rowTopForTest(9) + 6;
        QImage grab;
        QTRY_VERIFY_WITH_TIMEOUT((grab = scene.window->grabWindow(), !grab.isNull()), 5000);
        const QString grabDir = qEnvironmentVariable("QQO_GRAB_DIR");
        if (!grabDir.isEmpty()) {
            grab.save(grabDir + QStringLiteral("/qindacalc_orders.png"));
        }
        const int x = 200;
        const QColor header = grab.pixelColor(x, yHeader);
        const QColor plain = grab.pixelColor(x, yPlain);
        const QColor banded = grab.pixelColor(x, yBanded);
        qInfo() << "Orders table rows: header" << header << "plain" << plain << "banded" << banded;
        // The header paints dark (the workbook's own navy wins over the
        // table style, Excel's precedence), and a stripe row differs from
        // its unbanded neighbour (the Medium2 band, #D9E1F2, is off-white).
        QVERIFY(header.red() < 150 && header.green() < 150 && header.blue() < 150);
        QVERIFY(plain != banded);
        QVERIFY(banded.red() < 250 && banded.blue() > 200);
    }

    // The sample's four chart parts import as floating chart objects on the
    // sheets the file put them on, with their kinds and series intact.
    void sampleChartsImport()
    {
        const QString path = qEnvironmentVariable("QQO_SAMPLE_XLSX");
        if (path.isEmpty()) {
            QSKIP("QQO_SAMPLE_XLSX not set");
        }
        SpreadsheetDocument doc;
        QString error;
        QVERIFY2(doc.load(path, &error), qPrintable(error));
        QCOMPARE(doc.workbook()->charts.size(), 4);
        const qint32 dashboard = doc.workbook()->sheetIndex(QStringLiteral("Dashboard"));
        const qint32 forecast = doc.workbook()->sheetIndex(QStringLiteral("Forecast"));
        QVERIFY(dashboard >= 0 && forecast >= 0);
        int dashboardCharts = 0;
        int forecastCharts = 0;
        int seriesTotal = 0;
        for (const Calc::ChartSpec &chart : doc.workbook()->charts) {
            QVERIFY(!chart.series.isEmpty());
            QVERIFY(!chart.title.isEmpty());
            seriesTotal += chart.series.size();
            if (chart.sheet == dashboard) {
                ++dashboardCharts;
            } else if (chart.sheet == forecast) {
                ++forecastCharts;
            }
        }
        QCOMPARE(dashboardCharts, 2);
        QCOMPARE(forecastCharts, 2);
        QCOMPARE(seriesTotal, 6);
        // And the host exposes them for the chart windows.
        CalcHost host;
        host.spreadsheetDocument()->load(path, &error);
        QCOMPARE(host.charts().size(), 4);
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

    // Dashboard!A1:P2 is a merged banner with a solid navy fill (FF142B44)
    // and white bold text. The pixel is sampled off-glyph, away from the
    // cursor, so only the fill can satisfy it. (Red until M33 landed cell
    // fills; now the proof they round-trip.)
    void sampleDashboardKeepsItsDarkTitle()
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
        scene.host.setActiveSheet(1); // Dashboard
        // Move the cursor off the banner: the selection overlay would
        // otherwise be what the pixel sees. Wait for the click to land
        // (status bar follows the cursor) plus a frame, then grab once.
        QTest::mouseClick(scene.window, Qt::LeftButton, Qt::NoModifier, QPoint(700, 400));
        QTRY_VERIFY_WITH_TIMEOUT(!scene.host.statusInfo().startsWith(QLatin1String("A1")), 5000);
        QTest::qWait(150);
        const QImage grab = scene.window->grabWindow();
        const QColor a1 = grab.pixelColor(1000, 140);
        qInfo() << "Dashboard A1:P2 pixel" << a1;
        QVERIFY(a1.red() < 100 && a1.green() < 100 && a1.blue() < 100);
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
