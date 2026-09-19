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
        // The known array-arithmetic gap (D-028, fixed in M38): SUMPRODUCT
        // with an array expression argument yields #VALUE! today. Anything
        // beyond those twelve is a new import failure.
        QCOMPARE(totalErrors, 12);
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
