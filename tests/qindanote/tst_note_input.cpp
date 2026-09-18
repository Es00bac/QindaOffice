// SPDX-License-Identifier: GPL-3.0-or-later
// Pen input over the real window: a tablet pen inks and touch never does,
// the eraser acts DURING the drag as one undo step, the barrel button
// borrows the eraser, zoom scales the view and not the document, tool
// shortcuts switch tools but lose to a text field, the page is paper under
// the dark preset, and page writes are debounced.

#include "note_test_support.h"

#include "preferences.h"

#include <QFileInfo>
#include <QSignalSpy>

using namespace NoteTest;
using namespace QindaOffice::Apps::Notes;

class NoteInputTest final : public QObject {
    Q_OBJECT

    QPointingDevice *m_pen = nullptr;
    QPointingDevice *m_finger = nullptr;

    static int strokes(const Scene &scene) { return scene.host.currentPage()->envelope.ink.strokes().size(); }

    // The devices are process-wide: a test that fails mid-gesture leaves
    // the pen down, and the next press would read as a move. Lift it.
    void liftPen(const Scene &scene)
    {
        tabletAt(scene.window, m_pen, scene.at(QPointF(10, 10)), Qt::NoButton, 0.0);
        tabletAt(scene.window, m_finger, scene.at(QPointF(10, 10)), Qt::NoButton, 0.0);
    }

private slots:
    void initTestCase()
    {
        m_pen = makeDevice(QStringLiteral("test-pen"), 1, QInputDevice::DeviceType::Stylus,
                           QPointingDevice::PointerType::Pen);
        m_finger = makeDevice(QStringLiteral("test-finger"), 2, QInputDevice::DeviceType::TouchScreen,
                              QPointingDevice::PointerType::Finger);
    }

    void penInksAndTouchIsIgnored()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        liftPen(scene);
        scene.page->setStrokeSmoothing(0.0);
        scene.page->setPenWidth(7);
        scene.page->setPenColor(QColor(200, 30, 40));

        tabletStroke(scene.window, m_pen, scene.at(QPointF(200, 200)), scene.at(QPointF(400, 200)));
        QCOMPARE(strokes(scene), 1);
        const QindaOffice::Ink::Stroke &stroke = scene.host.currentPage()->envelope.ink.strokes().first();
        QCOMPARE(stroke.width, 7.0);
        QCOMPARE(stroke.color, QColor(200, 30, 40));
        QVERIFY(stroke.samples.size() >= 2);
        // Pressure reached the samples (D-004: pressure changes the width).
        QVERIFY(qAbs(stroke.samples.first().pressure - 0.8) < 0.05);
        // Samples are DOCUMENT coordinates, not window pixels.
        QVERIFY(qAbs(stroke.samples.first().pos.x() - 200.0) < 2.0);
        QVERIFY(qAbs(stroke.samples.first().pos.y() - 200.0) < 2.0);

        // A resting palm (touch device) is consumed without inking.
        tabletStroke(scene.window, m_finger, scene.at(QPointF(100, 300)), scene.at(QPointF(300, 300)));
        QCOMPARE(strokes(scene), 1);
        QVERIFY(scene.host.isDirty());
    }

    void undoAndRedoReplayStrokes()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        liftPen(scene);
        scene.page->setStrokeSmoothing(0.0);
        QSignalSpy history(scene.page, &NotePageItem::historyChanged);
        // The mouse fallback inks too.
        mouseStroke(scene.window, scene.at(QPointF(100, 100)), scene.at(QPointF(220, 140)));
        mouseStroke(scene.window, scene.at(QPointF(100, 200)), scene.at(QPointF(220, 240)));
        QCOMPARE(strokes(scene), 2);
        QVERIFY(scene.page->canUndo());
        scene.host.actions()->trigger(QStringLiteral("editUndoAction"));
        QCOMPARE(strokes(scene), 1);
        QVERIFY(scene.page->canRedo());
        scene.host.actions()->trigger(QStringLiteral("editRedoAction"));
        QCOMPARE(strokes(scene), 2);
        scene.page->undo();
        scene.page->undo();
        QCOMPARE(strokes(scene), 0);
        // A fresh edit ends the redo branch.
        mouseStroke(scene.window, scene.at(QPointF(100, 300)), scene.at(QPointF(220, 300)));
        QVERIFY(!scene.page->canRedo());
        QCOMPARE(strokes(scene), 1);
        QVERIFY(history.count() > 0);
    }

    void eraserErasesDuringTheDragAsOneUndoStep()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        liftPen(scene);
        scene.page->setStrokeSmoothing(0.0);
        // An empty swipe must not grow the history.
        scene.host.actions()->trigger(QStringLiteral("noteEraserTool"));
        QCOMPARE(scene.page->tool(), NoteTool::Eraser);
        tabletStroke(scene.window, m_pen, scene.at(QPointF(100, 100)), scene.at(QPointF(200, 100)));
        QVERIFY(!scene.page->canUndo());

        scene.host.actions()->trigger(QStringLiteral("notePenTool"));
        tabletStroke(scene.window, m_pen, scene.at(QPointF(200, 200)), scene.at(QPointF(400, 200)));
        tabletStroke(scene.window, m_pen, scene.at(QPointF(200, 300)), scene.at(QPointF(400, 300)));
        QCOMPARE(strokes(scene), 2);

        // Defect P4: the eraser used to act on pen-UP. The first stroke
        // must be gone while the pen is still down.
        scene.host.actions()->trigger(QStringLiteral("noteEraserTool"));
        tabletAt(scene.window, m_pen, scene.at(QPointF(300, 200)), Qt::LeftButton, 0.8);
        tabletAt(scene.window, m_pen, scene.at(QPointF(300, 206)), Qt::LeftButton, 0.8);
        QCOMPARE(strokes(scene), 1);
        tabletAt(scene.window, m_pen, scene.at(QPointF(300, 300)), Qt::LeftButton, 0.8);
        tabletAt(scene.window, m_pen, scene.at(QPointF(300, 300)), Qt::NoButton, 0.0);
        QCOMPARE(strokes(scene), 0);
        // … and ONE undo brings both back: the gesture is a single step.
        scene.page->undo();
        QCOMPARE(strokes(scene), 2);
    }

    void barrelButtonIsATemporaryEraser()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        liftPen(scene);
        scene.page->setStrokeSmoothing(0.0);
        tabletStroke(scene.window, m_pen, scene.at(QPointF(200, 200)), scene.at(QPointF(400, 200)));
        QCOMPARE(strokes(scene), 1);
        QCOMPARE(scene.page->tool(), NoteTool::Pen);
        // Tip down WITH the barrel button: the eraser is borrowed for
        // exactly this gesture.
        tabletAt(scene.window, m_pen, scene.at(QPointF(300, 200)), Qt::LeftButton | Qt::MiddleButton, 0.8);
        QCOMPARE(scene.page->tool(), NoteTool::Eraser);
        QCOMPARE(strokes(scene), 0);
        tabletAt(scene.window, m_pen, scene.at(QPointF(300, 200)), Qt::NoButton, 0.0);
        QCOMPARE(scene.page->tool(), NoteTool::Pen);
    }

    void zoomScalesTheViewNotTheDocument()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        liftPen(scene);
        scene.page->setStrokeSmoothing(0.0);
        QQuickItem *viewport = findItem(scene.window->contentItem(), QStringLiteral("noteViewport"));
        QVERIFY(viewport != nullptr);
        // Fit-to-width on open; then an explicit 200%: the page's on-screen
        // width is exactly twice its document width.
        QVERIFY(viewport->property("zoom").toReal() > 0.5);
        QVERIFY(viewport->setProperty("fitMode", QStringLiteral("none")));
        QVERIFY(viewport->setProperty("zoom", 2.0));
        QTRY_VERIFY(qAbs((scene.at(QPointF(760, 0)) - scene.at(QPointF(0, 0))).x() - 1520.0) < 1.0);
        // Inked coordinates land in PAGE space regardless of zoom.
        tabletStroke(scene.window, m_pen, scene.at(QPointF(150, 150)), scene.at(QPointF(250, 150)));
        QCOMPARE(strokes(scene), 1);
        const QPointF first = scene.host.currentPage()->envelope.ink.strokes().first().samples.first().pos;
        QVERIFY2(qAbs(first.x() - 150.0) < 2.0, qPrintable(QString::number(first.x())));
        QVERIFY2(qAbs(first.y() - 150.0) < 2.0, qPrintable(QString::number(first.y())));
        // View > Zoom In drives the same viewport.
        scene.host.actions()->trigger(QStringLiteral("viewZoomInAction"));
        QTRY_VERIFY(viewport->property("zoom").toReal() > 2.0);
    }

    void toolShortcutsSwitchToolsButLoseToATextField()
    {
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTest::keyClick(scene.window, Qt::Key_H);
        QCOMPARE(scene.page->tool(), NoteTool::Highlighter);
        QTest::keyClick(scene.window, Qt::Key_E);
        QCOMPARE(scene.page->tool(), NoteTool::Eraser);
        QTest::keyClick(scene.window, Qt::Key_T);
        QCOMPARE(scene.page->tool(), NoteTool::Text);
        QTest::keyClick(scene.window, Qt::Key_P);
        QCOMPARE(scene.page->tool(), NoteTool::Pen);
        QVERIFY(scene.host.actions()->entry(QStringLiteral("notePenTool"))->isChecked());

        // With the find field focused the letter types instead — typing
        // must never swap tools.
        scene.host.actions()->trigger(QStringLiteral("editFindAction"));
        QQuickItem *field = nullptr;
        QTRY_VERIFY((field = findItem(scene.window->contentItem(), QStringLiteral("findField"))) != nullptr);
        QTRY_VERIFY(field->isVisible());
        field->forceActiveFocus();
        QTRY_VERIFY(field->hasActiveFocus());
        QTest::keyClick(scene.window, Qt::Key_H);
        QCOMPARE(field->property("text").toString(), QStringLiteral("h"));
        QCOMPARE(scene.page->tool(), NoteTool::Pen);
    }

    void pageIsPaperUnderTheDarkPreset()
    {
        // D-004: the page is paper. The chrome follows the dark preset, the
        // sheet stays white so the default black pen reads.
        QindaOffice::Quick::Preferences::instance().setThemePreset(QStringLiteral("sloom-dark"));
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        QTRY_VERIFY(scene.page->width() > 0);
        const QImage shot = scene.window->grabWindow();
        QVERIFY(!shot.isNull());
        const QPoint centre = scene.at(QPointF(380, 500)).toPoint();
        QCOMPARE(shot.pixelColor(centre), QColor(Qt::white));
        // And the chrome is not white: the status bar is a dark surface.
        QQuickItem *status = findItem(scene.window->contentItem(), QStringLiteral("officeStatusBar"));
        QVERIFY(status != nullptr);
        const QPointF statusCentre = status->mapToScene(QPointF(status->width() / 2, status->height() / 2));
        QVERIFY(shot.pixelColor(statusCentre.toPoint()).lightness() < 128);
    }

    void persistIsDebounced()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.path() + QStringLiteral("/Work");
        Scene scene;
        QVERIFY2(scene.ready(), qPrintable(scene.error));
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        liftPen(scene);
        scene.page->setStrokeSmoothing(0.0);
        QString error;
        QVERIFY2(scene.host.noteDocument()->save(path, &error), qPrintable(error));
        const QString file = path
            + QStringLiteral("/sections/%1/pages/%2.json")
                  .arg(scene.host.currentSectionId(), scene.host.currentPageId());
        QVERIFY2(QFile::exists(file), qPrintable(file));
        const QDateTime savedAt = QFileInfo(file).lastModified();
        for (int i = 0; i < 3; ++i) {
            tabletStroke(scene.window, m_pen, scene.at(QPointF(100 + i * 40, 400)),
                         scene.at(QPointF(120 + i * 40, 400)));
        }
        QCOMPARE(strokes(scene), 3);
        // Three quick edits coalesce into ONE debounced write…
        QCOMPARE(QFileInfo(file).lastModified(), savedAt);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(file).lastModified() > savedAt, 3000);
        // … and flushPersist lands a pending write at once.
        const QDateTime firstWrite = QFileInfo(file).lastModified();
        QTest::qWait(1100); // mtime resolution
        tabletStroke(scene.window, m_pen, scene.at(QPointF(100, 500)), scene.at(QPointF(200, 500)));
        scene.host.flushPersist();
        QVERIFY(QFileInfo(file).lastModified() > firstWrite);
    }
};

NOTE_TEST_MAIN(NoteInputTest)

#include "tst_note_input.moc"
