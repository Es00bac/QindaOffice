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
#include "standard_actions.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMenu>
#include <QToolBar>

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

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaShow"));
    ShowUiTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_show_ui.moc"
