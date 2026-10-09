// SPDX-License-Identifier: GPL-3.0-or-later
// QindaShow through the real window: named strips and menus with
// persistent entries; styling ops mutate the model with undoable
// before/after payloads; the shape clipboard round-trips; insert commands
// land shapes; the edit commands follow the selection; find selects the
// matching slide; the canvas paints the page; align, bring-to-front and
// move-slide are one step each; Replace sweeps the deck; the sorter shows
// a thumbnail per slide; the player runs; structural fixtures.

#include "show_test_support.h"

#include "qindashow/src/presentation_controller.h"
#include "qindashow/src/show_clipboard.h"
#include "qindashow/src/show_commands.h"
#include "qindashow/src/show_toolbox.h"
#include "show_model/pptx_fixture_slides.h"
#include "support/quick_dump.h"
#include "support/ribbon_gate.h"

#include "menu_tree.h"
#include "standard_actions.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUndoStack>

using namespace ShowTest;
using namespace QindaOffice::Apps::Show;
using QindaOffice::Quick::ActionEntry;
using QindaOffice::Quick::MenuNode;
namespace ActionId = QindaOffice::Quick::ActionId;
namespace ShowModel = QindaOffice::Show;

namespace {

QString sid(const char *id)
{
    return QString::fromLatin1(id);
}

bool menuContains(const MenuNode *node, const QString &id)
{
    if (node == nullptr) {
        return false;
    }
    for (const MenuNode &child : node->children) {
        if (child.id == id || menuContains(&child, id)) {
            return true;
        }
    }
    return false;
}

} // namespace

class ShowUiTest : public QObject {
    Q_OBJECT
private slots:
    void toolbarsAndMenusExposeNamedCommands()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTest::qWait(100);
        QQuickItem *ribbon = scene.item(QStringLiteral("officeRibbon"));
        QVERIFY(ribbon != nullptr);
        for (const char *id : {ActionId::kEditUndo, ActionId::kEditCopy, ActionId::kSlideNew}) {
            QVERIFY2(findItem(ribbon, QStringLiteral("ribbonAction_") + sid(id)) != nullptr, id);
        }
        const ShowInsertActions &insert = scene.host.toolbox()->insertActions();
        const ShowStyleActions &style = scene.host.toolbox()->styleActions();
        for (const ActionEntry *entry : insert.all() + style.styleActions()) {
            QVERIFY2(!entry->id().isEmpty(), qPrintable(entry->text()));
        }
        for (const ActionEntry *entry : {insert.rectangle, insert.ellipse, style.bold}) {
            QVERIFY2(findItem(ribbon, QStringLiteral("ribbonAction_") + entry->id()) != nullptr,
                     qPrintable(entry->id()));
        }
        // The Shape tab (fill and stroke buttons) is contextual on a selection.
        QVERIFY(!ribbon->property("contextIds").toStringList().contains(QStringLiteral("shape")));
        // The menus name the same entries.
        const MenuNode *insertMenu = scene.host.menuTree()->find(QStringLiteral("showInsertMenu"));
        const MenuNode *shapeMenu = scene.host.menuTree()->find(QStringLiteral("showShapeMenu"));
        QVERIFY(insertMenu != nullptr && shapeMenu != nullptr);
        QVERIFY(menuContains(insertMenu, insert.rectangle->id()));
        QVERIFY(menuContains(shapeMenu, style.bold->id()));
        QVERIFY(scene.host.menuTree()->find(QStringLiteral("showSlideMenu")) != nullptr);
        QVERIFY(scene.host.menuTree()->find(QStringLiteral("showArrangeMenu")) != nullptr);
    }

    void stylingOpsAreUndoable()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.host.addSlide();
        SlideCanvasItem *canvas = scene.canvas;
        const QString shapeId = canvas->addShape(ShowModel::ShapeKind::TextBox, QRectF(10, 10, 200, 60));
        // A fresh text box has no paragraphs until typed into; seed one for
        // the styling assertions (test setup, not under test).
        {
            ShowModel::Shape *seed = canvas->slide()->findShape(shapeId);
            ShowModel::TextRun run;
            run.text = QStringLiteral("Hello");
            ShowModel::TextParagraph paragraph;
            paragraph.runs.append(run);
            seed->paragraphs.append(paragraph);
        }
        QCOMPARE(canvas->selectedShapeId(), shapeId);
        scene.host.actions()->trigger(sid(ActionId::kFormatBold));
        const ShowModel::Shape *shape = canvas->slide()->findShape(shapeId);
        QVERIFY(shape != nullptr);
        QVERIFY(shape->paragraphs.first().runs.first().bold);
        QVERIFY(scene.host.action(sid(ActionId::kFormatBold))->isChecked());

        scene.host.applyFillColor(QColor(255, 0, 0));
        QCOMPARE(canvas->slide()->findShape(shapeId)->fill, QColor(255, 0, 0));

        // Two undo steps: fill, then bold.
        scene.doc->undoStack()->undo();
        QVERIFY(canvas->slide()->findShape(shapeId)->fill != QColor(255, 0, 0));
        scene.doc->undoStack()->undo();
        QVERIFY(!canvas->slide()->findShape(shapeId)->paragraphs.first().runs.first().bold);
    }

    void shapeClipboardRoundTrips()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.host.addSlide();
        SlideCanvasItem *canvas = scene.canvas;
        const QString originalId = canvas->addShape(ShowModel::ShapeKind::Rectangle, QRectF(30, 40, 120, 80));

        copySelectedShape(canvas);
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        QVERIFY(mime->hasFormat(QString::fromLatin1(shapeClipboardMimeType())));

        const int base = int(canvas->slide()->shapes.size());
        QVERIFY(pasteShapeAtClipboard(canvas));
        QCOMPARE(canvas->slide()->shapes.size(), base + 1);
        const ShowModel::Shape &pasted = canvas->slide()->shapes.last();
        QVERIFY(pasted.id != originalId);
        QCOMPARE(pasted.rect.x(), 42.0); // +12 pt paste offset
        QCOMPARE(pasted.rect.y(), 52.0);
        QCOMPARE(canvas->selectedShapeId(), pasted.id);

        // Cut removes the source and leaves the payload pasteable.
        copySelectedShape(canvas);
        cutSelectedShape(canvas);
        QCOMPARE(canvas->slide()->shapes.size(), base);
        QVERIFY(pasteShapeAtClipboard(canvas));
        QCOMPARE(canvas->slide()->shapes.size(), base + 1);
    }

    void insertActionsLandShapes()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.host.addSlide();
        SlideCanvasItem *canvas = scene.canvas;
        const int base = int(canvas->slide()->shapes.size());
        QVERIFY(scene.host.actions()->trigger(sid(ActionId::kShowInsertEllipse)));
        QCOMPARE(canvas->slide()->shapes.size(), base + 1);
        QCOMPARE(canvas->slide()->shapes.last().kind, ShowModel::ShapeKind::Ellipse);
        // The insert went through the canvas undo payload.
        QVERIFY(scene.doc->undoStack()->canUndo());
        scene.doc->undoStack()->undo();
        QCOMPARE(canvas->slide()->shapes.size(), base);
    }

    void editingCommandsFollowSelection()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.host.addSlide();
        SlideCanvasItem *canvas = scene.canvas;
        ActionEntry *copy = scene.host.action(sid(ActionId::kEditCopy));
        ActionEntry *paste = scene.host.action(sid(ActionId::kEditPaste));
        canvas->clearSelection();
        QVERIFY(!copy->isEnabled());

        canvas->addShape(ShowModel::ShapeKind::TextBox, QRectF(0, 0, 100, 40));
        QVERIFY(copy->isEnabled());
        copy->trigger();
        QCoreApplication::processEvents();
        QVERIFY(paste->isEnabled());
        const int base = int(canvas->slide()->shapes.size());
        paste->trigger();
        QCOMPARE(canvas->slide()->shapes.size(), base + 1);

        scene.host.actions()->trigger(sid(ActionId::kEditSelectAll));
        QCOMPARE(canvas->selectionCount(), base + 1);
    }

    // Edit ▸ Find/Find Next/Find Previous were three dead menu entries in
    // the Widgets app: the deck had no search at all.
    void findSelectsTheMatchingSlide()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        ShowModel::Presentation *deck = scene.doc->presentation();
        deck->slides.first().title = QStringLiteral("Welcome");
        QList<ShowModel::Slide> extra;
        ShowModel::Slide budget;
        budget.id = QStringLiteral("s-budget");
        budget.title = QStringLiteral("Budget review");
        extra.append(budget);
        ShowModel::Slide notes;
        notes.id = QStringLiteral("s-notes");
        notes.title = QStringLiteral("Wrap up");
        notes.notes = QStringLiteral("mention the budget again");
        extra.append(notes);
        scene.host.appendSlides(extra);
        scene.host.selectSlideRow(0);

        QQuickItem *bar = scene.item(QStringLiteral("findBar"));
        QVERIFY(bar != nullptr);
        QVERIFY(!bar->isVisible());
        QVERIFY(scene.host.actions()->trigger(sid(ActionId::kEditFind)));
        QTRY_VERIFY(bar->isVisible());

        QVERIFY(scene.host.findNext(QStringLiteral("budget"), false));
        QCOMPARE(scene.host.currentSlideRow(), 1); // "Budget review"
        QVERIFY(scene.host.findNext(QStringLiteral("budget"), false));
        QCOMPARE(scene.host.currentSlideRow(), 2); // matched in the speaker notes
        QVERIFY(!scene.host.findNext(QStringLiteral("nowhere"), false));
        // Case sensitivity is honoured.
        QVERIFY(!scene.host.findNext(QStringLiteral("BUDGET"), true));
    }

    void canvasPaintsTheSlidePage()
    {
        Scene scene(QSize(1000, 700));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTest::qWait(150);
        const QImage shot = scene.window->grabWindow();
        QVERIFY(!shot.isNull());
        // The canvas paints the page itself: without it an empty slide
        // rendered as a flat dark canvas with no page anywhere in it.
        int whitePixels = 0;
        for (int y = 0; y < shot.height(); y += 4) {
            for (int x = 0; x < shot.width(); x += 4) {
                if (QColor(shot.pixel(x, y)).lightness() > 240) {
                    ++whitePixels;
                }
            }
        }
        QVERIFY2(whitePixels > 100, qPrintable(QString::number(whitePixels)));
        // And it is the item's own paint, in slide points.
        QCOMPARE(scene.canvas->implicitWidth(), scene.doc->presentation()->slideSize.width());
    }

    // M62: a PowerPoint deck opens natively and paints on the canvas with
    // its master's background, its table and its chart; a placeholder's
    // text opens in the in-place editor inside its own insets. Grabs land
    // in QQO_GRAB_DIR (qindashow_pptx_slideN.png).
    void powerPointDeckOnTheCanvas()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("fixture.pptx"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(PptxFixture::build());
        file.close();
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY2(scene.host.loadPath(path), "the .pptx did not open");
        QCOMPARE(scene.host.slideCount(), 5);
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        for (const int row : {0, 1, 3}) {
            scene.host.selectSlideRow(row);
            QTest::qWait(150);
            const QImage shot = scene.window->grabWindow();
            QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindashow_pptx_slide%1").arg(row + 1));
            int ivory = 0;
            for (int y = 0; y < shot.height(); y += 4) {
                for (int x = 0; x < shot.width(); x += 4) {
                    ivory += QColor(shot.pixel(x, y)) == QColor(0xFF, 0xFF, 0xF0) ? 1 : 0;
                }
            }
            QVERIFY2(ivory > 500, qPrintable(QStringLiteral("slide %1: %2").arg(row + 1).arg(ivory)));
        }
        scene.host.selectSlideRow(0);
        const ShowModel::Shape &title = scene.canvas->slide()->shapes.first();
        scene.canvas->beginTextEdit(title.id);
        QCOMPARE(scene.canvas->editingShapeId(), title.id);
        QCOMPARE(scene.canvas->editorRect(), title.rect.adjusted(7.2, 3.6, -7.2, -3.6));
        scene.canvas->commitTextEdit(false);
        QVERIFY(!scene.doc->isDirty()); // an unchanged edit is no edit
        // A table holds no text box of its own to edit in place.
        scene.host.selectSlideRow(1);
        scene.canvas->beginTextEdit(scene.canvas->slide()->shapes.first().id);
        QVERIFY(scene.canvas->editingShapeId().isEmpty());
    }

    void alignCentersSelectionAgainstTheSlide()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        SlideCanvasItem *canvas = scene.canvas;
        const QString id = canvas->addShape(ShowModel::ShapeKind::Rectangle, QRectF(10, 10, 100, 50));
        const int before = scene.doc->undoStack()->count();

        QVERIFY(scene.host.actions()->trigger(sid(ActionId::kShowAlignHCenter)));

        // A single shape aligns to the SLIDE, so its centre lands on the middle.
        const ShowModel::Shape *shape = canvas->slide()->findShape(id);
        QVERIFY(shape != nullptr);
        QCOMPARE(qRound(shape->rect.center().x()), qRound(canvas->slideSize().width() / 2));
        QCOMPARE(scene.doc->undoStack()->count(), before + 1);
        scene.doc->undoStack()->undo();
        QCOMPARE(qRound(canvas->slide()->findShape(id)->rect.left()), 10);
    }

    void bringToFrontIsOneStep()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        SlideCanvasItem *canvas = scene.canvas;
        const QString firstId = canvas->addShape(ShowModel::ShapeKind::Rectangle, QRectF(10, 10, 40, 40));
        canvas->addShape(ShowModel::ShapeKind::Ellipse, QRectF(60, 10, 40, 40));
        canvas->addShape(ShowModel::ShapeKind::Ellipse, QRectF(110, 10, 40, 40));
        // addShape selects what it just inserted; select the one this case
        // is about instead.
        canvas->clearSelection();
        canvas->setSelected(firstId, true);
        const auto indexOfFirst = [canvas, firstId] {
            for (int i = 0; i < canvas->slide()->shapes.size(); ++i) {
                if (canvas->slide()->shapes.at(i).id == firstId) {
                    return i;
                }
            }
            return -1;
        };
        const int indexBefore = indexOfFirst(); // the pristine slide has a placeholder

        QVERIFY(scene.host.actions()->trigger(sid(ActionId::kShowBringToFront)));

        // One command, whatever the distance: "Bring forward" repeated would
        // be as many undo steps as there were shapes in front.
        QCOMPARE(canvas->slide()->shapes.last().id, firstId);
        scene.doc->undoStack()->undo();
        QCOMPARE(indexOfFirst(), indexBefore);
    }

    void moveSlideReordersTheDeck()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        scene.host.addSlide();
        ShowModel::Presentation *deck = scene.doc->presentation();
        QCOMPARE(deck->slides.size(), 2);
        const QString secondId = deck->slides.at(1).id;
        QCOMPARE(scene.host.currentSlideRow(), 1);

        QVERIFY(scene.host.actions()->trigger(sid(ActionId::kShowSlideMoveUp)));

        QCOMPARE(deck->slides.first().id, secondId);
        QCOMPARE(scene.host.currentSlideRow(), 0);
        scene.doc->undoStack()->undo();
        QCOMPARE(scene.doc->presentation()->slides.at(1).id, secondId);

        // The sorter's drag reorders through the same one-step command.
        scene.host.moveSlide(1, 0);
        QCOMPARE(scene.doc->presentation()->slides.first().id, secondId);
    }

    void replaceSweepsTheWholeDeckInOneStep()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        ShowModel::Presentation *deck = scene.doc->presentation();
        deck->slides.first().title = QStringLiteral("Draft plan");
        deck->slides.first().notes = QStringLiteral("draft only");
        scene.host.addSlide();
        scene.doc->presentation()->slides.last().title = QStringLiteral("Draft two");
        const int before = scene.doc->undoStack()->count();

        QCOMPARE(scene.host.replaceAll(QStringLiteral("Draft"), QStringLiteral("Final")), 2);
        QCOMPARE(scene.doc->presentation()->slides.first().title, QStringLiteral("Final plan"));
        const int afterSweep = scene.doc->undoStack()->count();
        QCOMPARE(afterSweep, before + 1);
        scene.doc->undoStack()->undo();
        QCOMPARE(scene.doc->presentation()->slides.first().title, QStringLiteral("Draft plan"));
        // A miss reports and pushes nothing (undo leaves the command on the
        // stack, so it is the count that must not grow).
        QCOMPARE(scene.host.replaceAll(QStringLiteral("nowhere"), QStringLiteral("x")), 0);
        QCOMPARE(scene.doc->undoStack()->count(), afterSweep);
    }

    void sorterShowsAThumbnailPerSlide()
    {
        Scene scene(QSize(1280, 800));
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        scene.host.addSlide();
        QTest::qWait(150);
        QVERIFY(scene.item(QStringLiteral("slideThumb_0")) != nullptr);
        QTRY_VERIFY(scene.item(QStringLiteral("slideThumb_1")) != nullptr);
        QQuickItem *thumb = scene.item(QStringLiteral("slideThumbImage_1"));
        QVERIFY(thumb != nullptr);
        QVERIFY(thumb->width() > 0 && thumb->height() > 0);
        // Clicking a thumbnail selects its slide.
        scene.host.selectSlideRow(0);
        QCOMPARE(scene.host.currentSlideRow(), 0);
    }

    void playerRunsAndAdvancesThroughTheController()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        ShowModel::Presentation *deck = scene.doc->presentation();
        ShowModel::Shape build;
        build.id = QStringLiteral("b");
        build.kind = ShowModel::ShapeKind::Rectangle;
        build.rect = QRectF(10, 120, 100, 60);
        build.buildStep = 1;
        deck->slides.first().shapes.append(build);
        scene.host.addSlide();
        scene.host.selectSlideRow(0);

        QQuickWindow *player = scene.childWindow(QStringLiteral("showPlayer"));
        QVERIFY(player != nullptr);
        QVERIFY(!player->isVisible());
        scene.host.startShow();
        QVERIFY(scene.host.player() != nullptr);
        QTRY_VERIFY(player->isVisible());

        // The player advances through the shared controller: builds first,
        // then the next slide.
        QVERIFY(QMetaObject::invokeMethod(player, "advance", Q_ARG(QVariant, QVariant(1))));
        QCOMPARE(scene.host.player()->build(), 1);
        QVERIFY(QMetaObject::invokeMethod(player, "advance", Q_ARG(QVariant, QVariant(1))));
        QCOMPARE(scene.host.player()->slide(), 1);
        QCOMPARE(scene.host.player()->build(), 0);

        scene.host.endShow();
        QVERIFY(scene.host.player() == nullptr);
        QTRY_VERIFY(!player->isVisible());
        // The editor lands on the slide the show ended on.
        QCOMPARE(scene.host.currentSlideRow(), 1);
    }

    void freshHostIsPristineSoOpenCanReuseIt()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        // AGENT-CONTRACT (ADR-0110): a freshly created host is PRISTINE,
        // which lets File ▸ Open replace its blank deck instead of opening a
        // second window and leaving the blank one behind.
        QVERIFY(!scene.doc->isDirty());
        QVERIFY(scene.host.isPristineUntitled());
    }

    // The ribbon gate (tests/support/ribbon_gate.h, D-041) at a desktop
    // width and squeezed: every menu command reachable, every icon real,
    // every item of every tab (contextual ones forced on) inside the ribbon.
    void ribbonReachesEveryCommandAndSqueezes()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, 700));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QTest::qWait(150);
            const QStringList problems = QindaOffice::Test::ribbonProblems(
                scene.window, scene.host.menuTree(), {QStringLiteral("showFillColorAction"), QStringLiteral("showStrokeColorAction")});
            QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
        }
    }

    void structuralFixtures()
    {
        for (const int width : {1280, 520}) {
            Scene scene(QSize(width, width == 1280 ? 800 : 700));
            QVERIFY2(scene.ready(), qPrintable(scene.error));
            QVERIFY(QTest::qWaitForWindowExposed(scene.window));
            QTest::qWait(150);
            QindaOffice::Test::saveGrab(scene.window, QStringLiteral("qindashow_%1").arg(width));
            QString diff;
            const QString path = QStringLiteral(QQO_FIXTURE_DIR "/qindashow_%1.txt").arg(width);
            QVERIFY2(QindaOffice::Test::matchesFixture(scene.window, path, &diff), qPrintable(diff));
        }
    }
};

SHOW_TEST_MAIN(ShowUiTest)

#include "tst_show_ui.moc"
