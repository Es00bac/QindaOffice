// SPDX-License-Identifier: GPL-3.0-or-later
#include "calc_m78_support.h"
#include "calc_engine/pivot_fixture.h"
#include "calc_io/excel_package.h"
#include "xlsx_print_assets.h"
#include <poppler-qt6.h>
#include <QBuffer>
#include <QFile>
using namespace CalcTest;
using namespace CalcTest::M78;
using namespace QindaOffice::Calc;
class CalcM90UiTest : public QObject {
    Q_OBJECT
private slots:
    void keptFormulaWarningSurvivesRecovery()
    {
        SpreadsheetDocument document;
        KeptRule rule;
        rule.kind = KeptRule::Kind::Validation;
        rule.ranges = {{{0, 0}, {2, 0}}};
        rule.xml = QStringLiteral("<dataValidation type=\"custom\"><formula1>A1 A2 (</formula1></dataValidation>");
        rule.staleFormulas = true;
        document.workbook()->sheetAt(0).setKeptRules({rule});
        SpreadsheetDocument recovered;
        QString error;
        QVERIFY2(recovered.loadRecovery(document.recoverySnapshot(), &error), qPrintable(error));
        QCOMPARE(recovered.workbook()->sheetAt(0).keptRules(), QList<KeptRule>{rule});
    }
    void calculatedFieldDialog()
    {
        Scene scene(QSize(520, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        *scene.doc->workbook() = Workbook();
        PivotTest::fillSales(*scene.doc->workbook());
        const auto ref = PivotTest::makePivot(*scene.doc->workbook());
        QVERIFY(ref.isValid());
        QVERIFY(PivotTest::change(*scene.doc->workbook(), ref, [](PivotTable &p, const PivotCache &c) { pivotPlaceField(p, c, 0, PivotArea::Rows); }));
        scene.host.setActiveSheet(1);
        scene.grid->setCursorCell(3, 0);
        trigger(scene, ActionId::kCalcPivotCalculatedField);
        QVERIFY(dialogOpened(scene, "pivotCalculatedDialog"));
        QVERIFY(setText(scene, "pivotCalculatedName", QStringLiteral("Unit Price")));
        QVERIFY(setText(scene, "pivotCalculatedFormula", QStringLiteral("=Sales/Qty")));
        grab(scene, QStringLiteral("qindacalc_m90_calculated_field_520"));
        const int steps = scene.doc->undoStack()->count();
        QVERIFY(answer(scene, "pivotCalculatedDialog", true));
        QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
        QCOMPARE(scene.doc->workbook()->pivotCaches.first().fields.last().formula, QStringLiteral("=Sales/Qty"));
        SpreadsheetDocument restored;
        QString error;
        QVERIFY2(restored.loadRecovery(scene.doc->recoverySnapshot(), &error), qPrintable(error));
        QCOMPARE(restored.workbook()->pivotCaches, scene.doc->workbook()->pivotCaches);
        scene.doc->undoStack()->undo();
        QCOMPARE(scene.doc->workbook()->pivotCaches.first().fields.size(), 5);
    }
    void dateFilterDialog()
    {
        Scene scene(QSize(520, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        *scene.doc->workbook() = Workbook();
        PivotTest::fillSales(*scene.doc->workbook());
        const auto ref = PivotTest::makePivot(*scene.doc->workbook());
        QVERIFY(PivotTest::change(*scene.doc->workbook(), ref, [](PivotTable &p, const PivotCache &c) {
            pivotPlaceField(p, c, 2, PivotArea::Rows);
            pivotPlaceField(p, c, 3, PivotArea::Values);
        }));
        scene.host.setActiveSheet(1);
        scene.grid->setCursorCell(3, 0);
        trigger(scene, ActionId::kCalcPivotFilter);
        QVERIFY(dialogOpened(scene, "pivotFilterDialog"));
        scene.item(QStringLiteral("pivotFilterKind"))->setProperty("currentIndex", 4);
        scene.item(QStringLiteral("pivotFilterDateOp"))->setProperty("currentIndex", 0);
        QVERIFY(setText(scene, "pivotFilterText1", QStringLiteral("2024-01-01")));
        QVERIFY(setText(scene, "pivotFilterText2", QStringLiteral("2024-12-31")));
        grab(scene, QStringLiteral("qindacalc_m90_date_filter_520"));
        QVERIFY(answer(scene, "pivotFilterDialog", true));
        const auto &p = scene.doc->workbook()->sheetAt(1).pivots().first();
        QCOMPARE(p.fields[2].filters.first().dateType, QStringLiteral("dateBetween"));
        const auto result = computePivot(scene.doc->workbook()->pivotCaches.first(), p);
        QCOMPARE(result.valuesAt(0, 0)->first(), Value(72.0));
    }
    void headerPicturesAndDisplayedNotes()
    {
        using namespace QindaOffice::Test;
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        type(scene, 0, 0, QStringLiteral("Printed data"));
        type(scene, 8, 5, QStringLiteral("Print extent"));
        scene.doc->setComment({0, 0}, {QStringLiteral("Reviewer"), QStringLiteral("Visible note")});
        scene.doc->setComment({1, 0}, {QString(), QStringLiteral("Hidden note")});
        QImage logo(120, 20, QImage::Format_RGB32);
        logo.fill(Qt::red);
        for (int y = 0; y < 20; ++y) for (int x = 60; x < 120; ++x) logo.setPixelColor(x, y, Qt::blue);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(logo.save(&buffer, "PNG"));
        ExcelWorkbook source;
        ExcelSheet sheet(QStringLiteral("Sheet1"), QByteArray());
        sheet.afterData = QByteArrayLiteral("<legacyDrawing r:id=\"notes\"/><legacyDrawingHF r:id=\"header\"/>");
        sheet.rels = {{QStringLiteral("header"), officeRel("vmlDrawing"), QStringLiteral("xl/drawings/header.vml")},
                      {QStringLiteral("notes"), officeRel("vmlDrawing"), QStringLiteral("xl/drawings/notes.vml")}};
        source.sheets = {sheet};
        source.parts = {
            {QStringLiteral("xl/drawings/header.vml"), QByteArrayLiteral(
                "<xml xmlns:v=\"urn:schemas-microsoft-com:vml\" xmlns:o=\"urn:schemas-microsoft-com:office:office\">"
                "<v:shape id=\"CH\" style=\"width:60pt;height:20pt\"><v:imagedata o:relid=\"image\" cropright=\"32768f\"/></v:shape></xml>"),
                QStringLiteral("application/vnd.openxmlformats-officedocument.vmlDrawing"),
                {{QStringLiteral("image"), officeRel("image"), QStringLiteral("xl/media/logo.png")}}},
            {QStringLiteral("xl/media/logo.png"), png, QStringLiteral("image/png"), {}},
            {QStringLiteral("xl/drawings/notes.vml"), QByteArrayLiteral(
                "<xml xmlns:v=\"urn:schemas-microsoft-com:vml\" xmlns:x=\"urn:schemas-microsoft-com:office:excel\">"
                "<v:shape style=\"width:108pt;height:59pt;visibility:visible\"><x:ClientData ObjectType=\"Note\">"
                "<x:Row>0</x:Row><x:Column>0</x:Column><x:Visible/></x:ClientData></v:shape>"
                "<v:shape style=\"visibility:hidden\"><x:ClientData ObjectType=\"Note\">"
                "<x:Row>1</x:Row><x:Column>0</x:Column></x:ClientData></v:shape></xml>"),
                QStringLiteral("application/vnd.openxmlformats-officedocument.vmlDrawing"), {}}};
        auto &book = *scene.doc->workbook();
        book.source.carried = source.build();
        auto sheetSource = book.sheetAt(0).source();
        sheetSource.part = QStringLiteral("xl/worksheets/sheet1.xml");
        book.sheetAt(0).setSource(sheetSource);
        auto setup = book.sheetAt(0).pageSetup();
        setup.oddHeader = QStringLiteral("&C&G");
        setup.cellComments = 2;
        scene.doc->setPrintSettings(0, setup, std::nullopt, std::nullopt, std::nullopt, QStringLiteral("Print assets"));
        const auto assets = IO::legacyPrintAssets(book, 0);
        QCOMPARE(assets.pictures.value(QStringLiteral("CH")).bytes, png);
        QCOMPARE(assets.notes.size(), 1);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("pictures-notes.pdf"));
        QVERIFY(scene.host.exportPdf(path));
        const std::unique_ptr<Poppler::Document> pdf(Poppler::Document::load(path));
        QVERIFY(pdf != nullptr);
        const std::unique_ptr<Poppler::Page> page(pdf->page(0));
        QVERIFY(page != nullptr);
        const QString printed = page->text(QRectF());
        QVERIFY2(printed.contains(QStringLiteral("Visible note")), qPrintable(printed));
        QVERIFY(!printed.contains(QStringLiteral("Hidden note")));
        const QImage pixels = page->renderToImage(72, 72);
        int red = 0;
        int blue = 0;
        for (int y = 0; y < pixels.height() / 8; ++y) for (int x = 0; x < pixels.width(); ++x) {
            const QColor c = pixels.pixelColor(x, y);
            red += c.red() > 200 && c.green() < 50 && c.blue() < 50;
            blue += c.blue() > 200 && c.green() < 50 && c.red() < 50;
        }
        QVERIFY2(red > 500, qPrintable(QString::number(red)));
        QCOMPARE(blue, 0);
    }
    void notesAtEndPaginate()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        type(scene, 0, 0, QStringLiteral("Printed data"));
        scene.doc->setComment({0, 0}, {QStringLiteral("Reviewer"), QStringLiteral("First note")});
        scene.doc->setComment({1, 0}, {QString(), QStringLiteral("Long note ").repeated(2000) + QStringLiteral("FINISH MARKER")});
        auto setup = scene.doc->workbook()->sheetAt(0).pageSetup();
        setup.cellComments = 1;
        scene.doc->setPrintSettings(0, setup, std::nullopt, std::nullopt, std::nullopt, QStringLiteral("Print notes"));
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("notes.pdf"));
        QVERIFY(scene.host.exportPdf(path));
        std::unique_ptr<Poppler::Document> pdf(Poppler::Document::load(path));
        QVERIFY(pdf != nullptr);
        QVERIFY(pdf->numPages() > 2);
        QString text;
        for (int p = 0; p < pdf->numPages(); ++p) {
            const std::unique_ptr<Poppler::Page> page(pdf->page(p));
            text += page->text(QRectF());
        }
        QVERIFY(text.contains(QStringLiteral("A1: Reviewer: First note")));
        QVERIFY(text.contains(QStringLiteral("FINISH MARKER")));
    }
};
CALC_TEST_MAIN(CalcM90UiTest)
#include "tst_calc_m90_ui.moc"
