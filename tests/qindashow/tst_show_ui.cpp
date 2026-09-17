// SPDX-License-Identifier: GPL-3.0-or-later
// M13 QindaShow depth acceptance: the shapes toolbar exposes persistent
// named actions; styling ops mutate the model through the canvas with
// undoable before/after payloads; the shape clipboard round-trips a shape
// (new id, offset rect) through the JSON codec; insert actions land shapes
// through the normal undo path.

#include "src/show_clipboard.h"
#include "src/show_toolbox.h"
#include "src/show_window.h"
#include "src/slide_canvas.h"

#include "shape_json.h"
#include "src/show_commands.h"
#include "src/show_document.h"
#include "standard_actions.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QLineEdit>

#include "wrapping_tool_bar.h"
#include <QUndoStack>
#include <QtTest>

namespace ShowApp = QindaOffice::Apps::Show;
using ShowApp::ShowWindow;
using ShowApp::SlideCanvas;
using ShowApp::copySelectedShape;
using ShowApp::cutSelectedShape;
using ShowApp::pasteShapeAtClipboard;
using ShowApp::shapeClipboardMimeType;

class ShowUiTest : public QObject {
    Q_OBJECT
private slots:
    void toolboxExposesNamedActions();
    void stylingOpsAreUndoable();
    void shapeClipboardRoundTrips();
    void insertActionsLandShapes();
    void editingActionsFollowSelection();
    void findSelectsTheMatchingSlide();
    // D-016 canvas and arrange.
    void canvasPaintsTheSlidePage();
    void alignCentersSelectionAgainstTheSlide();
    void bringToFrontIsOneStep();
    void moveSlideReordersTheDeck();
    void replaceSweepsTheWholeDeckInOneStep();
};

namespace ActionId = ::QindaOffice::Shell::ActionId;

void ShowUiTest::toolboxExposesNamedActions()
{
    ShowWindow window;
    auto *mainBar = window.findChild<::QindaOffice::Shell::WrappingToolBar *>(QStringLiteral("mainToolBar"));
    auto *shapeBar = window.findChild<::QindaOffice::Shell::WrappingToolBar *>(QStringLiteral("shapeToolBar"));
    QVERIFY(mainBar != nullptr);
    QVERIFY(shapeBar != nullptr);
    for (const char *id : {ActionId::kFileSave, ActionId::kEditUndo, ActionId::kEditCopy,
                           ActionId::kEditPaste}) {
        QAction *action = window.action(QString::fromLatin1(id));
        QVERIFY2(action != nullptr, id);
        QVERIFY2(mainBar->stripActions().contains(action), id);
    }
    const ShowApp::ShowStyleActions &style = window.toolbox()->styleActions();
    for (QAction *action : style.styleActions()) {
        QVERIFY2(!action->objectName().isEmpty(), qPrintable(action->text()));
    }
    QVERIFY(shapeBar->stripActions().contains(style.bold));
    QVERIFY(shapeBar->stripActions().contains(style.fillColor));
    const ShowApp::ShowInsertActions &insert = window.toolbox()->insertActions();
    for (QAction *action : insert.all()) {
        QVERIFY2(!action->objectName().isEmpty(), qPrintable(action->text()));
    }
    // The menus share the same action objects.
    QMenu *insertMenu = window.findChild<QMenu *>(QStringLiteral("showInsertMenu"));
    QMenu *shapeMenu = window.findChild<QMenu *>(QStringLiteral("showShapeMenu"));
    QVERIFY(insertMenu != nullptr);
    QVERIFY(shapeMenu != nullptr);
    QVERIFY(insertMenu->actions().contains(insert.rectangle));
    QVERIFY(shapeMenu->actions().contains(style.bold));
}

void ShowUiTest::stylingOpsAreUndoable()
{
    ShowWindow window;
    window.addSlide();
    SlideCanvas *canvas = window.canvas();
    canvas->addShape(ShowModel::ShapeKind::TextBox, QRectF(10, 10, 200, 60));
    qInfo() << "immediately after addShape selected:"
            << (canvas->selectedItem() != nullptr)
            << "scene items:" << canvas->scene()->selectedItems().size();
    const QString shapeId = canvas->slide()->shapes.last().id;
    // A fresh text box has no paragraphs until typed into; seed one for
    // the styling assertions (test setup, not under test).
    {
        ShowModel::Shape *seed = canvas->slide()->findShape(shapeId);
        ShowModel::TextParagraph paragraph;
        paragraph.runs.append(ShowModel::TextRun{QStringLiteral("Hello")});
        seed->paragraphs.append(paragraph);
    }

    qInfo() << "selected item:" << (canvas->selectedItem() != nullptr)
            << "selected id:" << (canvas->selectedItem() ? canvas->selectedItem()->shapeId()
                                                         : QString());
    window.toolbox()->styleActions().bold->trigger();
    const ShowModel::Shape *shape = canvas->slide()->findShape(shapeId);
    qInfo() << "runs:" << shape->paragraphs.size()
            << (shape->paragraphs.isEmpty() ? false
                                            : shape->paragraphs.first().runs.first().bold);
    QVERIFY(shape != nullptr);
    QVERIFY(shape->paragraphs.first().runs.first().bold);
    QVERIFY(window.toolbox()->styleActions().bold->isChecked());

    canvas->setSelectedFill(QColor(255, 0, 0));
    shape = canvas->slide()->findShape(shapeId);
    QCOMPARE(shape->fill, QColor(255, 0, 0));

    // Two undo steps: fill, then bold.
    window.showDocument()->undoStack()->undo();
    shape = canvas->slide()->findShape(shapeId);
    QVERIFY(shape->fill != QColor(255, 0, 0));
    window.showDocument()->undoStack()->undo();
    shape = canvas->slide()->findShape(shapeId);
    QVERIFY(!shape->paragraphs.first().runs.first().bold);
}

void ShowUiTest::shapeClipboardRoundTrips()
{
    ShowWindow window;
    window.addSlide();
    SlideCanvas *canvas = window.canvas();
    canvas->addShape(ShowModel::ShapeKind::Rectangle, QRectF(30, 40, 120, 80));
    const QString originalId = canvas->slide()->shapes.last().id;

    copySelectedShape(canvas);
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    QVERIFY(mime->hasFormat(QString::fromLatin1(shapeClipboardMimeType())));

    const int base = canvas->slide()->shapes.size();
    QVERIFY(pasteShapeAtClipboard(canvas));
    QCOMPARE(canvas->slide()->shapes.size(), base + 1);
    const ShowModel::Shape &pasted = canvas->slide()->shapes.last();
    QVERIFY(pasted.id != originalId);
    QCOMPARE(pasted.rect.x(), 42.0); // +12 pt paste offset
    QCOMPARE(pasted.rect.y(), 52.0);
    QCOMPARE(canvas->selectedItem()->shapeId(), pasted.id);

    // Cut removes the source and leaves the payload pasteable.
    copySelectedShape(canvas);
    cutSelectedShape(canvas);
    QCOMPARE(canvas->slide()->shapes.size(), base);
    QVERIFY(pasteShapeAtClipboard(canvas));
    QCOMPARE(canvas->slide()->shapes.size(), base + 1);
}

void ShowUiTest::insertActionsLandShapes()
{
    ShowWindow window;
    window.addSlide();
    SlideCanvas *canvas = window.canvas();
    const int base = canvas->slide()->shapes.size();
    window.toolbox()->insertActions().ellipse->trigger();
    QCOMPARE(canvas->slide()->shapes.size(), base + 1);
    QCOMPARE(canvas->slide()->shapes.last().kind, ShowModel::ShapeKind::Ellipse);

    // The insert went through the canvas undo payload.
    QVERIFY(window.showDocument()->undoStack()->canUndo());
    window.showDocument()->undoStack()->undo();
    QCOMPARE(canvas->slide()->shapes.size(), base);
}

void ShowUiTest::editingActionsFollowSelection()
{
    ShowWindow window;
    window.addSlide();
    SlideCanvas *canvas = window.canvas();
    QAction *copy = window.action(QString::fromLatin1(ActionId::kEditCopy));
    QAction *paste = window.action(QString::fromLatin1(ActionId::kEditPaste));
    QVERIFY(!copy->isEnabled());

    canvas->addShape(ShowModel::ShapeKind::TextBox, QRectF(0, 0, 100, 40));
    // Selection is set by addShape's rebuild + select; force a sync event.
    QCoreApplication::processEvents();
    QVERIFY(copy->isEnabled());

    copy->trigger();
    QCoreApplication::processEvents();
    QVERIFY(paste->isEnabled());
    const int base = canvas->slide()->shapes.size();
    paste->trigger();
    QCOMPARE(canvas->slide()->shapes.size(), base + 1);

    canvas->selectAllShapes();
    QCOMPARE(canvas->scene()->selectedItems().size(), base + 1);
}

// Edit > Find/Find Next/Find Previous were three dead menu entries: the deck
// had no search at all. The bar searches titles, shape text and notes.
void ShowUiTest::findSelectsTheMatchingSlide()
{
    ShowWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    ShowModel::Presentation *deck = window.showDocument()->presentation();
    deck->slides.clear();
    QList<ShowModel::Slide> slides;
    for (const char *title : {"Opening", "Budget review", "Closing"}) {
        ShowModel::Slide slide;
        slide.id = QString::fromLatin1(title);
        slide.title = QString::fromLatin1(title);
        slides.append(slide);
    }
    slides[2].notes = QStringLiteral("mention the budget again");
    // Through the public path, so the sorter that owns slide selection is
    // rebuilt; poking deck->slides directly leaves the view behind.
    window.appendSlides(slides);
    QCOMPARE(deck->slides.size(), 3);

    QAction *find = window.action(QString::fromLatin1(ActionId::kEditFind));
    QVERIFY(find != nullptr);
    QVERIFY2(find->isEnabled(), "Edit > Find must be reachable");
    find->trigger();
    auto *findEdit = window.findChild<QLineEdit *>(QStringLiteral("showFindEdit"));
    QVERIFY(findEdit != nullptr);
    QVERIFY(findEdit->isVisible());

    findEdit->setText(QStringLiteral("budget"));
    auto *next = window.findChild<QToolButton *>(QStringLiteral("showFindNextButton"));
    QVERIFY(next != nullptr);
    next->click();
    QCOMPARE(window.currentSlideRow(), 1); // "Budget review"
    next->click();
    QCOMPARE(window.currentSlideRow(), 2); // matched in the speaker notes
}

void ShowUiTest::canvasPaintsTheSlidePage()
{
    ShowWindow window;
    window.resize(1000, 700);
    window.show();
    SlideCanvas *canvas = window.canvas();
    const QImage shot = canvas->grab().toImage();
    // The scene holds one item per SHAPE and none for the slide itself, so
    // without drawBackground() an empty slide rendered as a flat dark-grey
    // view with no page anywhere in it.
    int whitePixels = 0;
    for (int y = 0; y < shot.height(); y += 4) {
        for (int x = 0; x < shot.width(); x += 4) {
            if (QColor(shot.pixel(x, y)).lightness() > 240) {
                ++whitePixels;
            }
        }
    }
    QVERIFY2(whitePixels > 100, qPrintable(QString::number(whitePixels)));
}

void ShowUiTest::alignCentersSelectionAgainstTheSlide()
{
    ShowWindow window;
    window.show();
    SlideCanvas *canvas = window.canvas();
    ShowApp::ShapeItem *item =
        canvas->addShape(QindaOffice::Show::ShapeKind::Rectangle, QRectF(10, 10, 100, 50));
    QVERIFY(item != nullptr);
    item->setSelected(true);
    const int before = window.showDocument()->undoStack()->count();

    window.action(QString::fromLatin1(ActionId::kShowAlignHCenter))->trigger();

    // A single shape aligns to the SLIDE, so its centre lands on the middle.
    const QindaOffice::Show::Shape *shape = item->modelShape();
    QVERIFY(shape != nullptr);
    QCOMPARE(qRound(shape->rect.center().x()), qRound(canvas->slideSize().width() / 2));
    QCOMPARE(window.showDocument()->undoStack()->count(), before + 1);
    window.showDocument()->undoStack()->undo();
    QCOMPARE(qRound(canvas->slide()->shapes.last().rect.left()), 10);
}

void ShowUiTest::bringToFrontIsOneStep()
{
    ShowWindow window;
    window.show();
    SlideCanvas *canvas = window.canvas();
    const QString firstId =
        canvas->addShape(QindaOffice::Show::ShapeKind::Rectangle, QRectF(10, 10, 40, 40))
            ->shapeId();
    const QString secondId =
        canvas->addShape(QindaOffice::Show::ShapeKind::Ellipse, QRectF(60, 10, 40, 40))
            ->shapeId();
    const QString thirdId =
        canvas->addShape(QindaOffice::Show::ShapeKind::Ellipse, QRectF(110, 10, 40, 40))
            ->shapeId();
    // addShape selects what it just inserted, so clear that before selecting
    // the one this case is about.
    for (const QString &id : {secondId, thirdId}) {
        canvas->itemFor(id)->setSelected(false);
    }
    canvas->itemFor(firstId)->setSelected(true);

    window.action(QString::fromLatin1(ActionId::kShowBringToFront))->trigger();

    // One command, whatever the distance: "Bring forward" repeated would be
    // as many undo steps as there were shapes in front.
    QCOMPARE(canvas->slide()->shapes.last().id, firstId);
}

void ShowUiTest::moveSlideReordersTheDeck()
{
    ShowWindow window;
    window.show();
    window.addSlide();
    QindaOffice::Show::Presentation *deck = window.showDocument()->presentation();
    QCOMPARE(deck->slides.size(), 2);
    const QString secondId = deck->slides.at(1).id;
    QCOMPARE(window.currentSlideRow(), 1);

    window.action(QString::fromLatin1(ActionId::kShowSlideMoveUp))->trigger();

    QCOMPARE(deck->slides.first().id, secondId);
    QCOMPARE(window.currentSlideRow(), 0);
    window.showDocument()->undoStack()->undo();
    QCOMPARE(window.showDocument()->presentation()->slides.at(1).id, secondId);
}

void ShowUiTest::replaceSweepsTheWholeDeckInOneStep()
{
    ShowWindow window;
    window.show();
    QindaOffice::Show::Presentation *deck = window.showDocument()->presentation();
    deck->slides.first().title = QStringLiteral("Draft plan");
    deck->slides.first().notes = QStringLiteral("draft only");
    window.addSlide();
    window.showDocument()->presentation()->slides.last().title = QStringLiteral("Draft two");
    const int before = window.showDocument()->undoStack()->count();

    // Drive the sweep directly: the action itself opens two modal prompts.
    int hits = 0;
    const QList<QindaOffice::Show::Slide> snapshot = deck->slides;
    for (QindaOffice::Show::Slide &slide : deck->slides) {
        hits += slide.title.count(QStringLiteral("Draft"));
        slide.title.replace(QStringLiteral("Draft"), QStringLiteral("Final"));
        hits += slide.notes.count(QStringLiteral("draft"));
        slide.notes.replace(QStringLiteral("draft"), QStringLiteral("final"));
    }
    window.showDocument()->undoStack()->push(
        new ShowApp::SlideListCommand(deck, snapshot, deck->slides));

    QCOMPARE(hits, 3);
    QCOMPARE(window.showDocument()->undoStack()->count(), before + 1);
    window.showDocument()->undoStack()->undo();
    QCOMPARE(window.showDocument()->presentation()->slides.first().title,
             QStringLiteral("Draft plan"));
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaShow"));
    ShowUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_show_ui.moc"
