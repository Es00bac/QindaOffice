// SPDX-License-Identifier: GPL-3.0-or-later
// The gates that need a REAL Excel workbook. Point QQO_SAMPLE_XLSX at one
// (the Northstar demo the parity work was driven by) and these open it
// through the app: every sheet reachable, no error cells, rules enforced,
// tables banded, charts imported, the dashboard painted as its author left
// it. Without the variable every slot skips, so the suite stays green on a
// machine that has no such file. Split from tst_calc_window.cpp, which
// carries the gates that build their own documents.

#include "calc_test_support.h"

#include "spreadsheet_document.h"
#include "spreadsheet_item.h"
#include "calc_host.h"

#include "libre_office_converter.h"
#include "standard_actions.h"

#include "sheet.h"

#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUndoStack>

#include <QtTest>

using namespace CalcTest;
using namespace QindaOffice::Apps::QindaCalc;
namespace ActionId = QindaOffice::Quick::ActionId;
namespace Calc = QindaOffice::Calc;

class CalcSampleTest : public QObject {
    Q_OBJECT
private slots:
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

    // The close-out gate for the parity work (M32-M39): the author's file,
    // saved by US and read back, is still the author's file. Everything the
    // eight milestones taught the bridge to carry is counted on both sides —
    // if any single one of them regresses, the numbers stop matching and
    // this says which.
    void sampleSurvivesOurOwnSaveAndReopen()
    {
        const QString path = qEnvironmentVariable("QQO_SAMPLE_XLSX");
        if (path.isEmpty()) {
            QSKIP("QQO_SAMPLE_XLSX not set");
        }
        struct Census {
            int sheets = 0;
            int visibleSheets = 0;
            int cells = 0;
            int errors = 0;
            int styled = 0;
            int condFormats = 0;
            int validations = 0;
            int comments = 0;
            int tables = 0;
            int filters = 0;
            int charts = 0;
            int series = 0;
            int images = 0;
            int frozen = 0;
            int printAreas = 0;
            int activeTab = 0;
            QString a1;
            double forecast = 0;
        };
        const auto census = [](const SpreadsheetDocument &doc) {
            Census c;
            const Calc::Workbook *wb = doc.workbook();
            c.sheets = wb->sheetCount();
            c.activeTab = wb->activeTab();
            c.charts = int(wb->charts.size());
            c.images = int(wb->images.size());
            for (const Calc::ChartSpec &chart : wb->charts) {
                c.series += int(chart.series.size());
            }
            for (qint32 s = 0; s < wb->sheetCount(); ++s) {
                const Calc::Sheet &sheet = wb->sheetAt(s);
                if (!sheet.isHidden()) {
                    ++c.visibleSheets;
                }
                c.cells += int(sheet.cells().size());
                for (auto it = sheet.cells().constBegin(); it != sheet.cells().constEnd(); ++it) {
                    if (Calc::isErr(it.value().value)) {
                        ++c.errors;
                    }
                }
                c.styled += int(sheet.styleIds().size());
                c.condFormats += int(sheet.condFormats().size());
                c.validations += int(sheet.validations().size());
                c.comments += int(sheet.comments().size());
                c.tables += int(sheet.tables().size());
                if (sheet.autoFilter().range.isValid()) {
                    ++c.filters;
                }
                if (sheet.freezeRows() > 0 || sheet.freezeCols() > 0) {
                    ++c.frozen;
                }
                if (sheet.printArea().has_value()) {
                    ++c.printAreas;
                }
            }
            c.a1 = doc.displayTextAt(0, {0, 0});
            const qint32 forecast = wb->sheetIndex(QStringLiteral("Forecast"));
            if (forecast >= 0) {
                // B26: SUMPRODUCT(values,(1+rate)^periods) — the M38 shape.
                const auto value = Calc::toNumber(wb->cellValue(forecast, 25, 1));
                c.forecast = value.value_or(0);
            }
            return c;
        };

        SpreadsheetDocument original;
        QString error;
        QVERIFY2(original.load(path, &error), qPrintable(error));
        const Census before = census(original);
        QCOMPARE(before.errors, 0);
        QVERIFY(before.forecast > 0);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString saved = dir.path() + QStringLiteral("/roundtrip.xlsx");
        QVERIFY2(original.save(saved, &error), qPrintable(error));

        SpreadsheetDocument reopened;
        QVERIFY2(reopened.load(saved, &error), qPrintable(error));
        const Census after = census(reopened);

        qInfo() << "sheets" << before.sheets << after.sheets << "cells" << before.cells
                << after.cells << "styled" << before.styled << after.styled << "cond"
                << before.condFormats << after.condFormats << "valid" << before.validations
                << after.validations << "comments" << before.comments << after.comments
                << "tables" << before.tables << after.tables << "charts" << before.charts
                << after.charts << "series" << before.series << after.series;

        QCOMPARE(after.sheets, before.sheets);
        QCOMPARE(after.visibleSheets, before.visibleSheets);
        QCOMPARE(after.cells, before.cells);
        QCOMPARE(after.errors, 0);
        QCOMPARE(after.styled, before.styled);
        QCOMPARE(after.condFormats, before.condFormats);
        QCOMPARE(after.validations, before.validations);
        QCOMPARE(after.comments, before.comments);
        QCOMPARE(after.tables, before.tables);
        QCOMPARE(after.filters, before.filters);
        QCOMPARE(after.charts, before.charts);
        QCOMPARE(after.series, before.series);
        QCOMPARE(after.images, before.images);
        QCOMPARE(after.frozen, before.frozen);
        QCOMPARE(after.printAreas, before.printAreas);
        QCOMPARE(after.activeTab, before.activeTab);
        QCOMPARE(after.a1, before.a1);
        // The recalculated forecast agrees to the cent.
        QVERIFY2(qAbs(after.forecast - before.forecast) < 0.01,
                 qPrintable(QStringLiteral("%1 vs %2").arg(before.forecast).arg(after.forecast)));
    }

    // …and LibreOffice reads what we wrote. Our own reader agreeing with
    // our own writer proves only that they share a bug; saving the sample
    // as ODS runs the package through headless LibreOffice, which has to
    // parse every part we generated, and loading it back brings it home
    // through a foreign implementation.
    void whatWeWriteOpensInLibreOffice()
    {
        const QString path = qEnvironmentVariable("QQO_SAMPLE_XLSX");
        if (path.isEmpty()) {
            QSKIP("QQO_SAMPLE_XLSX not set");
        }
        if (!QindaOffice::Core::LibreOfficeConverter::probe()) {
            QSKIP("LibreOffice is not installed");
        }
        SpreadsheetDocument doc;
        QString error;
        QVERIFY2(doc.load(path, &error), qPrintable(error));
        const qint32 sheets = doc.workbook()->sheetCount();
        const QString a1 = doc.displayTextAt(0, {0, 0});

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString ods = dir.path() + QStringLiteral("/ours.ods");
        QVERIFY2(doc.save(ods, &error), qPrintable(error));
        QVERIFY(QFileInfo(ods).size() > 0);

        SpreadsheetDocument back;
        QVERIFY2(back.load(ods, &error), qPrintable(error));
        QCOMPARE(back.workbook()->sheetCount(), sheets);
        QCOMPARE(back.displayTextAt(0, {0, 0}), a1);
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    qputenv("QT_QUICK_BACKEND", "software");
    QGuiApplication app(argc, argv);
    CalcSampleTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_calc_sample.moc"
