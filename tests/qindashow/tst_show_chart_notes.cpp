// SPDX-License-Identifier: GPL-3.0-or-later
// Chart data, speaker notes and the presenter view through the real window
// (M67, D-076): Edit Data on a PowerPoint chart opens the data sheet over
// the chart's own numbers, edits there (a value, a new series, another
// kind) land in the chart as one undo step and are saved into the chart
// part and its embedded workbook; the notes pane sits under the slide,
// typing into it edits the slide's notes undoably and View ▸ Notes hides
// it; Presenter View on one screen shows the presenter (current and next
// slide, the notes, the timer) instead of the player. M82 (D-091): a
// combo chart's data sheet offers a kind per plot, and an added series
// joins the plot of the one before it. Grabs (QQO_GRAB_DIR): the data
// sheet (also a combo's), the notes pane, the presenter view.

#include "show_test_support.h"

#include "qindashow/src/presentation_controller.h"
#include "qindashow/src/show_chart_data.h"
#include "show_model/pptx_fixture_combo.h"
#include "show_model/pptx_fixture_slides.h"
#include "support/quick_dump.h"

#include "ooxml_package.h"
#include "pptx_reader.h"

#include <QSignalSpy>
#include <QUndoStack>

#include <cmath>

using namespace ShowTest;
using namespace QindaOffice::Apps::Show;
namespace ShowModel = QindaOffice::Show;
namespace Ooxml = QindaOffice::Ooxml;

namespace {

QString writeFixture(const QTemporaryDir &dir)
{
    const QString path = dir.filePath(QStringLiteral("deck.pptx"));
    QFile file(path);
    const QByteArray bytes = PptxFixture::build();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        return {};
    }
    return path;
}

// QTest::keyClicks takes widgets only: one key per character.
void typeText(QWindow *window, const QString &text)
{
    for (const QChar c : text) {
        QTest::keyClick(window, c.toLatin1());
    }
}

void stage(QindaTK::EditableGridModel *grid, int row, int column, const QString &text)
{
    QVERIFY(grid->beginEdit(QStringLiteral("test")));
    QVERIFY(grid->stageCell(grid->rowIdAt(row), grid->columnIdAt(column), text));
    QVERIFY(grid->commitEdit());
}

} // namespace

class ShowChartNotesTest : public QObject {
    Q_OBJECT
private slots:
    void editDataReachesTheChartAndTheFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QVERIFY(scene.host.loadPath(writeFixture(dir)));
        scene.host.selectSlideRow(3);
        SlideCanvasItem *canvas = scene.canvas;
        const QString id = canvas->slide()->shapes.first().id;
        QVERIFY(!scene.host.action(QStringLiteral("showChartEditDataAction"))->isEnabled());
        canvas->setSelected(id, true);
        QVERIFY(scene.host.action(QStringLiteral("showChartEditDataAction"))->isEnabled());
        QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
        QTRY_VERIFY(ribbon->property("contextIds").toStringList().contains(QStringLiteral("chart")));
        QSignalSpy asked(&scene.host, &ShowHost::chartDataRequested);
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showChartEditDataAction")));
        QCOMPARE(asked.count(), 1);
        QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("chartDataDialog"));
        QVERIFY(dialog != nullptr);
        QTRY_VERIFY(dialog->property("opened").toBool());
        ChartDataEditor *editor = scene.host.chartData();
        QindaTK::EditableGridModel *grid = editor->grid();
        // The sheet is the chart's: names across, categories down.
        QCOMPARE(grid->rows(), 4);
        QCOMPARE(grid->columns(), 3);
        QCOMPARE(grid->cellValue(grid->rowIdAt(0), grid->columnIdAt(1)).toString(), QStringLiteral("North"));
        QCOMPARE(grid->cellValue(grid->rowIdAt(3), grid->columnIdAt(0)).toString(), QStringLiteral("Q3"));
        QCOMPARE(grid->cellValue(grid->rowIdAt(2), grid->columnIdAt(2)).toString(), QStringLiteral("1"));
        QCOMPARE(editor->kindNames().at(editor->kindIndex()), QStringLiteral("Clustered column"));
        stage(grid, 2, 1, QStringLiteral("8.5"));
        editor->addSeries();
        QCOMPARE(grid->columns(), 4);
        QCOMPARE(grid->cellValue(grid->rowIdAt(0), grid->columnIdAt(3)).toString(), QStringLiteral("Series 3"));
        stage(grid, 1, 3, QStringLiteral("2"));
        stage(grid, 3, 3, QStringLiteral("7"));
        editor->setKindIndex(1); // stacked column
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindashow_chart_data"));
        const int steps = scene.doc->undoStack()->count();
        QVERIFY(scene.host.applyChartData());
        QCOMPARE(scene.doc->undoStack()->count(), steps + 1);
        const ShowModel::ChartData chart = canvas->slide()->findShape(id)->chart;
        QCOMPARE(chart.grouping, QStringLiteral("stacked"));
        QCOMPARE(chart.series.size(), 3);
        QCOMPARE(chart.series.at(0).values.at(1), 8.5);
        QCOMPARE(chart.series.at(2).name, QStringLiteral("Series 3"));
        QVERIFY(std::isnan(chart.series.at(2).values.at(1)));
        QMetaObject::invokeMethod(dialog, "close");
        // Saved: the chart reads back as edited, its workbook travels along.
        const QString saved = dir.filePath(QStringLiteral("saved.pptx"));
        QVERIFY(scene.doc->save(saved));
        QString error;
        const ShowModel::Presentation back = ShowModel::readPptxFile(saved, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(back.slides.at(3).shapes.first().chart, chart);
        const auto package = Ooxml::Package::load(saved);
        QVERIFY(package);
        QCOMPARE(package->partNames().filter(QStringLiteral("ppt/embeddings/")).size(), 1);
        scene.doc->undoStack()->undo();
        QCOMPARE(canvas->slide()->findShape(id)->chart.series.size(), 2);
    }

    void comboChartEditsEachPlot()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        const QString path = dir.filePath(QStringLiteral("combo.pptx"));
        QFile file(path);
        const QByteArray bytes = PptxComboFixture::deckWith(PptxComboFixture::comboChart(), {});
        QVERIFY(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size());
        file.close();
        QVERIFY(scene.host.loadPath(path));
        scene.host.selectSlideRow(3);
        SlideCanvasItem *canvas = scene.canvas;
        const QString id = canvas->slide()->shapes.first().id;
        canvas->setSelected(id, true);
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showChartEditDataAction")));
        QObject *dialog = scene.window->findChild<QObject *>(QStringLiteral("chartDataDialog"));
        QVERIFY(dialog != nullptr);
        QTRY_VERIFY(dialog->property("opened").toBool());
        ChartDataEditor *editor = scene.host.chartData();
        // A kind per plot, each named by its series; Total is a line.
        const QVariantList plots = editor->plots();
        QCOMPARE(plots.size(), 2);
        QCOMPARE(plots.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("North, South"));
        QCOMPARE(plots.at(1).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("Total"));
        const QStringList kinds = editor->comboKindNames();
        QCOMPARE(kinds.at(plots.at(1).toMap().value(QStringLiteral("comboIndex")).toInt()), QStringLiteral("Line"));
        QVERIFY(!kinds.contains(QStringLiteral("Pie")));
        QTRY_VERIFY(scene.item(QStringLiteral("chartPlotKind_1")) != nullptr);
        QVERIFY(!scene.item(QStringLiteral("chartKindCombo"))->isVisible());
        // Total becomes an area; a new series joins it.
        editor->setPlotKind(1, int(kinds.indexOf(QStringLiteral("Area"))));
        editor->addSeries();
        QindaTK::EditableGridModel *grid = editor->grid();
        for (int row = 1; row < grid->rows(); ++row) {
            stage(grid, row, 4, QStringLiteral("5"));
        }
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindashow_m82_combo_data"));
        QVERIFY(scene.host.applyChartData());
        const ShowModel::ChartData chart = canvas->slide()->findShape(id)->chart;
        QCOMPARE(chart.plots.at(0).type, QStringLiteral("column"));
        QCOMPARE(chart.plots.at(1).type, QStringLiteral("area"));
        QCOMPARE(chart.series.size(), 4);
        QCOMPARE(chart.series.at(3).plot, 1);
        QCOMPARE(chart.categoryLevels.first().at(2), QStringLiteral("West"));
        QMetaObject::invokeMethod(dialog, "close");
        const QString saved = dir.filePath(QStringLiteral("saved.pptx"));
        QVERIFY(scene.doc->save(saved));
        QString error;
        const ShowModel::Presentation back = ShowModel::readPptxFile(saved, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(back.slides.at(3).shapes.first().chart, chart);
    }

    void notesPaneUnderTheSlide()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(scene.window));
        QQuickItem *editor = scene.item(QStringLiteral("slideEditor"));
        QVERIFY(editor != nullptr);
        QQuickItem *pane = findItem(editor, QStringLiteral("notesPane"));
        QQuickItem *notes = findItem(editor, QStringLiteral("slideNotes"));
        QVERIFY(pane != nullptr && notes != nullptr);
        // Under the slide, across its width.
        const QRectF slideArea = scene.canvas->parentItem()->mapRectToItem(editor, scene.canvas->parentItem()->boundingRect());
        const QRectF paneArea = pane->mapRectToItem(editor, pane->boundingRect());
        QVERIFY(paneArea.top() >= slideArea.bottom() - 1);
        QVERIFY(pane->isVisible());
        notes->forceActiveFocus();
        typeText(scene.window, QStringLiteral("Open with the numbers"));
        QTRY_COMPARE(scene.host.notes(), QStringLiteral("Open with the numbers"));
        QCOMPARE(scene.doc->undoStack()->undoText(), QObject::tr("Edit speaker notes"));
        QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindashow_notes_pane"));
        scene.doc->undoStack()->undo(); // the typing is one step
        QCOMPARE(scene.host.notes(), QString());
        QTRY_COMPARE(notes->property("text").toString(), QString());
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showViewNotesAction")));
        QTRY_VERIFY(!pane->isVisible());
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showViewNotesAction")));
        QTRY_VERIFY(pane->isVisible());
    }

    void presenterViewOnOneScreen()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.doc->presentation()->slides.first().notes = QStringLiteral("Say hello");
        scene.host.addSlide();
        scene.doc->presentation()->slides.last().notes = QStringLiteral("Then the plan");
        scene.host.selectSlideRow(0);
        QQuickWindow *presenter = scene.childWindow(QStringLiteral("showPresenter"));
        QQuickWindow *player = scene.childWindow(QStringLiteral("showPlayer"));
        QVERIFY(presenter != nullptr && player != nullptr);
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showPresenterViewAction")));
        QVERIFY(scene.host.player() != nullptr);
        QVERIFY(scene.host.presenterView());
        // One screen: the presenter shows, the audience player does not.
        QTRY_VERIFY(presenter->isVisible());
        QVERIFY(!player->isVisible());
        QQuickItem *notes = findItem(presenter->contentItem(), QStringLiteral("presenterNotes"));
        QVERIFY(notes != nullptr);
        QTRY_COMPARE(notes->property("text").toString(), QStringLiteral("Say hello"));
        QVERIFY(findItem(presenter->contentItem(), QStringLiteral("presenterNext")) != nullptr);
        QVERIFY(findItem(presenter->contentItem(), QStringLiteral("presenterTimer")) != nullptr);
        QQuickItem *end = findItem(presenter->contentItem(), QStringLiteral("presenterEnd"));
        QVERIFY(end != nullptr && !end->isVisible());
        scene.host.player()->advance(1);
        QTRY_COMPARE(notes->property("text").toString(), QStringLiteral("Then the plan"));
        QTRY_VERIFY(end->isVisible()); // nothing after the last slide
        QindaOffice::Test::saveGrab(presenter, QStringLiteral("qindashow_presenter"));
        scene.host.endShow();
        QVERIFY(!scene.host.presenterView());
        QTRY_VERIFY(!presenter->isVisible());
    }
};

SHOW_TEST_MAIN(ShowChartNotesTest)

#include "tst_show_chart_notes.moc"
