// SPDX-License-Identifier: GPL-3.0-or-later
// M78 (D-087) through the real QindaCalc window: the Page Layout tab's
// commands (margins, orientation, size, print area, breaks, scaling, sheet
// options — one undo step each, their checks following the sheet), the
// Page Setup dialog's four tabs answering as one step (and refusing a
// reference it cannot read), View ▸ Page Break Preview, and Export PDF
// honouring all of it: print titles on every page, Fit to 1 page wide,
// the footer's &P of &N, gridlines and headings (read back through
// Poppler). QQO_GRAB_DIR saves the tab, the dialog and the preview at 1280
// and 520.

#include "calc_m78_support.h"

#include "page_layout.h"
#include "sheet.h"

#include <QUndoStack>

#include <poppler-qt6.h>

using namespace CalcTest;
using namespace CalcTest::M78;
using QindaOffice::Calc::Sheet;

class CalcPrintUiTest : public QObject {
    Q_OBJECT

    static const Sheet::PageSetup &setup(Scene &scene) { return scene.doc->workbook()->sheetAt(0).pageSetup(); }
    static bool checked(Scene &scene, const char *id)
    {
        return scene.host.actions()->entry(QString::fromLatin1(id))->isChecked();
    }
    // 80 rows of Region / Product / Sales under a header row.
    static void fillRows(Scene &scene, int rows)
    {
        type(scene, 0, 0, QStringLiteral("Region"));
        type(scene, 0, 1, QStringLiteral("Product"));
        type(scene, 0, 2, QStringLiteral("Sales"));
        QList<QPair<QindaOffice::Calc::CellAddr, QString>> edits;
        for (int r = 1; r <= rows; ++r) {
            edits.append({{r, 0}, QStringLiteral("Region %1").arg(r)});
            edits.append({{r, 1}, QStringLiteral("Item %1").arg(r)});
            edits.append({{r, 2}, QString::number(r * 10)});
        }
        scene.doc->applyInputs(edits, QStringLiteral("Fill"));
    }
    static QStringList pdfPages(const QString &path)
    {
        std::unique_ptr<Poppler::Document> pdf(Poppler::Document::load(path));
        QStringList pages;
        for (int page = 0; pdf != nullptr && page < pdf->numPages(); ++page) {
            std::unique_ptr<Poppler::Page> pdfPage(pdf->page(page));
            pages.append(pdfPage->text(QRectF()));
        }
        return pages;
    }

private slots:
    void pageLayoutCommandsAreOneStepEach()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        fillRows(scene, 10);
        QVERIFY(checked(scene, ActionId::kCalcMarginsNormal));
        QVERIFY(checked(scene, ActionId::kCalcOrientationPortrait));
        const int steps = scene.doc->undoStack()->count();
        trigger(scene, ActionId::kCalcMarginsWide);
        QCOMPARE(setup(scene).marginLeft, 1.0);
        QCOMPARE(setup(scene).marginHeader, 0.5);
        trigger(scene, ActionId::kCalcOrientationLandscape);
        QCOMPARE(setup(scene).orientation, 1);
        scene.host.actions()->trigger(QStringLiteral("calcPaperSize_9")); // A4
        QCOMPARE(setup(scene).paperSize, 9);
        trigger(scene, ActionId::kCalcScaleFitColumns);
        QVERIFY(setup(scene).fitToPage);
        QCOMPARE(setup(scene).fitToWidth, 1);
        QCOMPARE(setup(scene).fitToHeight, 0);
        trigger(scene, ActionId::kCalcPrintGridlines);
        QVERIFY(setup(scene).gridLines);
        QCOMPARE(scene.doc->undoStack()->count(), steps + 5);
        QVERIFY(checked(scene, ActionId::kCalcMarginsWide));
        QVERIFY(checked(scene, ActionId::kCalcOrientationLandscape));
        QVERIFY(checked(scene, ActionId::kCalcScaleFitColumns));
        QVERIFY(checked(scene, ActionId::kCalcPrintGridlines));
        // Print area over the selection, and cleared.
        scene.grid->setCursorCell(0, 0);
        scene.grid->setCursorCell(5, 2, true);
        trigger(scene, ActionId::kCalcSetPrintArea);
        QCOMPARE(scene.doc->workbook()->sheetAt(0).printArea(), (QindaOffice::Calc::CellRange{{0, 0}, {5, 2}}));
        QVERIFY(scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcClearPrintArea))->isEnabled());
        trigger(scene, ActionId::kCalcClearPrintArea);
        QVERIFY(!scene.doc->workbook()->sheetAt(0).printArea().has_value());
        // The ribbon's Scale slots: 2 pages wide.
        scene.host.setFitTo(2, 0);
        QCOMPARE(setup(scene).fitToWidth, 2);
        scene.host.setScalePercent(75);
        QVERIFY(!setup(scene).fitToPage);
        QCOMPARE(setup(scene).scale, 75);
        // Everything undoes back to the defaults.
        while (scene.doc->undoStack()->index() > steps) {
            scene.doc->undoStack()->undo();
        }
        QVERIFY(setup(scene) == Sheet::PageSetup{});
        QVERIFY(checked(scene, ActionId::kCalcMarginsNormal));
        scene.host.setActiveSheet(0);
        QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
        QVERIFY(ribbon != nullptr);
        ribbon->setProperty("currentTabId", QStringLiteral("pageLayout"));
        grab(scene, QStringLiteral("qindacalc_m78_page_layout_tab_1280"));
    }

    void pageSetupDialogAnswersAsOneStep()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        fillRows(scene, 10);
        const int steps = scene.doc->undoStack()->count();
        trigger(scene, ActionId::kCalcPageSetup);
        QVERIFY(dialogOpened(scene, "pageSetupDialog"));
        QObject *dialog = popup(scene, "pageSetupDialog");
        grab(scene, QStringLiteral("qindacalc_m78_page_setup_page_1280"));
        const auto set = [dialog](const char *key, const QVariant &value) {
            QMetaObject::invokeMethod(dialog, "set", Q_ARG(QVariant, QString::fromLatin1(key)), Q_ARG(QVariant, value));
        };
        set("orientation", 1);
        set("fitToPage", true);
        set("fitToWidth", 1);
        set("fitToHeight", 0);
        set("firstPageNumber", QStringLiteral("3"));
        set("centerHorizontally", true);
        set("titleRows", QStringLiteral("$1:$1"));
        set("printArea", QStringLiteral("$A$1:$C$11"));
        set("headings", true);
        QMetaObject::invokeMethod(dialog, "setIn", Q_ARG(QVariant, QStringLiteral("oddFooter")),
                                  Q_ARG(QVariant, QStringLiteral("center")), Q_ARG(QVariant, QStringLiteral("Page &[Page] of &[Pages]")));
        for (int tab = 1; tab < 4; ++tab) {
            dialog->setProperty("tab", tab);
            grab(scene, QStringLiteral("qindacalc_m78_page_setup_%1_1280").arg(QStringList{{}, QStringLiteral("margins"),
                                                                                             QStringLiteral("header"), QStringLiteral("sheet")}.at(tab)));
        }
        QVERIFY(answer(scene, "pageSetupDialog", true));
        QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
        const Sheet &sheet = scene.doc->workbook()->sheetAt(0);
        QCOMPARE(sheet.pageSetup().orientation, 1);
        QVERIFY(sheet.pageSetup().fitToPage);
        QVERIFY(sheet.pageSetup().useFirstPageNumber);
        QCOMPARE(sheet.pageSetup().firstPageNumber, 3);
        QVERIFY(sheet.pageSetup().centerHorizontally);
        QVERIFY(sheet.pageSetup().headings);
        QCOMPARE(sheet.pageSetup().oddFooter, QStringLiteral("&CPage &P of &N"));
        QCOMPARE(sheet.printTitleRows()->topLeft.row, 0);
        QCOMPARE(sheet.printTitleRows()->bottomRight.row, 0);
        QCOMPARE(sheet.printArea(), (QindaOffice::Calc::CellRange{{0, 0}, {10, 2}}));
        // A reference it cannot read reopens the dialog with the reason.
        trigger(scene, ActionId::kCalcPrintTitles);
        QVERIFY(dialogOpened(scene, "pageSetupDialog"));
        QCOMPARE(dialog->property("tab").toInt(), 3);
        set("titleCols", QStringLiteral("not a column"));
        QMetaObject::invokeMethod(dialog, "accept");
        QVERIFY(dialogOpened(scene, "pageSetupDialog"));
        QVERIFY(!dialog->property("error").toString().isEmpty());
        QVERIFY(answer(scene, "pageSetupDialog", false));
        QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
    }

    void exportPdfHonoursPageSetup()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        fillRows(scene, 120);
        Sheet::PageSetup setup;
        setup.fitToPage = true;
        setup.fitToWidth = 1;
        setup.fitToHeight = 0;
        setup.oddFooter = QStringLiteral("&CPage &P of &N");
        setup.oddHeader = QStringLiteral("&L&A&RQuarterly");
        setup.headings = true;
        scene.doc->setPrintSettings(0, setup, std::nullopt, QindaOffice::Calc::CellRange{{0, 0}, {0, Sheet::kMaxCols - 1}},
                                    std::nullopt, QStringLiteral("Page Setup"));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = (qEnvironmentVariableIsSet("QQO_GRAB_DIR") ? qEnvironmentVariable("QQO_GRAB_DIR") : dir.path())
            + QStringLiteral("/qindacalc_m78_print_titles.pdf");
        QVERIFY(scene.host.exportPdf(path));
        const QStringList pages = pdfPages(path);
        const int expected = int(QindaOffice::Calc::layoutPrintPages(*scene.doc->workbook(), 0).pages.size());
        QVERIFY2(pages.size() >= 2, qPrintable(QString::number(pages.size())));
        QCOMPARE(pages.size(), expected);
        for (int i = 0; i < pages.size(); ++i) {
            // The title row on every page, the footer counting the pages.
            QVERIFY2(pages.at(i).contains(QStringLiteral("Region")) && pages.at(i).contains(QStringLiteral("Sales")),
                     qPrintable(pages.at(i).left(200)));
            QVERIFY2(pages.at(i).contains(QStringLiteral("Page %1 of %2").arg(i + 1).arg(pages.size())), qPrintable(pages.at(i).right(200)));
            QVERIFY(pages.at(i).contains(QStringLiteral("Quarterly")));
        }
        QVERIFY(pages.last().contains(QStringLiteral("Item 120")));
    }

    void pageBreaksAndPageBreakPreview()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        fillRows(scene, 60);
        scene.grid->setCursorCell(20, 0);
        trigger(scene, ActionId::kCalcInsertPageBreak);
        QCOMPARE(setup(scene).rowBreaks, (QList<qint32>{20}));
        QVERIFY(setup(scene).colBreaks.isEmpty());
        QVERIFY(scene.host.actions()->entry(QString::fromLatin1(ActionId::kCalcRemovePageBreak))->isEnabled());
        trigger(scene, ActionId::kCalcViewPageBreakPreview);
        QVERIFY(scene.grid->pageBreakPreview());
        QVERIFY(checked(scene, ActionId::kCalcViewPageBreakPreview));
        grab(scene, QStringLiteral("qindacalc_m78_page_break_preview_1280"));
        // The break moves with an inserted row above it.
        scene.doc->insertRows(2, 1);
        QCOMPARE(setup(scene).rowBreaks, (QList<qint32>{21}));
        scene.grid->setCursorCell(21, 0);
        trigger(scene, ActionId::kCalcRemovePageBreak);
        QVERIFY(setup(scene).rowBreaks.isEmpty());
        scene.grid->setCursorCell(10, 1);
        trigger(scene, ActionId::kCalcInsertPageBreak);
        QCOMPARE(setup(scene).rowBreaks, (QList<qint32>{10}));
        QCOMPARE(setup(scene).colBreaks, (QList<qint32>{1}));
        trigger(scene, ActionId::kCalcResetPageBreaks);
        QVERIFY(setup(scene).rowBreaks.isEmpty() && setup(scene).colBreaks.isEmpty());
        trigger(scene, ActionId::kCalcViewNormal);
        QVERIFY(!scene.grid->pageBreakPreview());
    }

    void squeezedTo520()
    {
        Scene scene(QSize(520, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        fillRows(scene, 30);
        scene.item(QStringLiteral("officeRibbon"))->setProperty("currentTabId", QStringLiteral("pageLayout"));
        grab(scene, QStringLiteral("qindacalc_m78_page_layout_tab_520"));
        trigger(scene, ActionId::kCalcHeaderFooter);
        QVERIFY(dialogOpened(scene, "pageSetupDialog"));
        QCOMPARE(popup(scene, "pageSetupDialog")->property("tab").toInt(), 2);
        grab(scene, QStringLiteral("qindacalc_m78_page_setup_header_520"));
        QVERIFY(answer(scene, "pageSetupDialog", false));
        trigger(scene, ActionId::kCalcViewPageBreakPreview);
        grab(scene, QStringLiteral("qindacalc_m78_page_break_preview_520"));
    }
};

CALC_TEST_MAIN(CalcPrintUiTest)

#include "tst_calc_print_ui.moc"
