// SPDX-License-Identifier: GPL-3.0-or-later
// M22 pen gates (plan "QindaNote UX overhaul" section 4/M22): the eraser
// acts during the drag as ONE undo step, barrel buttons borrow the eraser,
// inking repaints only the fresh segment, the page is paper under a dark
// palette, single-letter tool shortcuts switch tools (and lose to a text
// field), and per-keystroke page writes are debounced.

#include "qindanote/src/note_page_view.h"
#include "qindanote/src/note_window.h"

#include <QApplication>
#include <QFileInfo>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QtTest>

#include "standard_actions.h"

using namespace QindaOffice::Apps::Notes;
namespace Ink = QindaOffice::Ink;

class NoteInputTest : public QObject {
    Q_OBJECT
private slots:
    void eraserErasesDuringTheDrag();
    void eraserGestureIsOneUndoStepAndNoneWhenNothingErased();
    void barrelButtonIsATemporaryEraser();
    void inkingRepaintsOnlyTheSegmentRect();
    void pageIsPaperUnderADarkPalette();
    void toolShortcutsSwitchTools();
    void persistIsDebounced();
};

namespace {

// Inks one horizontal mouse stroke across the page point (x, y).
void mouseStroke(NotePageView *view, const QPointF &from, const QPointF &to)
{
    QTest::mousePress(view, Qt::LeftButton, {}, from.toPoint());
    QTest::mouseMove(view, to.toPoint());
    QTest::mouseRelease(view, Qt::LeftButton, {}, to.toPoint());
}

// These tests place strokes and eraser presses at exact coordinates; the
// position-smoothing EMA (Preferences default "light") deliberately lags a
// fast drag's tail, so they pin it off to keep the geometry deterministic.
void noSmoothing(NotePageView *view)
{
    view->setStrokeSmoothing(0.0);
}

} // namespace

void NoteInputTest::eraserErasesDuringTheDrag()
{
    NotePageView view;
    noSmoothing(&view);
    NotePage page;
    view.setPage(&page);
    const QPointF origin = view.pageRect().topLeft();
    mouseStroke(&view, origin + QPointF(200, 200), origin + QPointF(400, 200));
    QCOMPARE(page.envelope.ink.strokes().size(), 1);

    view.setTool(NoteTool::Eraser);
    QTest::mousePress(&view, Qt::LeftButton, {}, (origin + QPointF(300, 200)).toPoint());
    QTest::mouseMove(&view, (origin + QPointF(300, 206)).toPoint());
    // Defect P4: the eraser used to act on pen-UP, so dragging it changed
    // nothing until the release. The stroke must be gone while the button
    // is still down.
    QCOMPARE(page.envelope.ink.strokes().size(), 0);
    QTest::mouseRelease(&view, Qt::LeftButton, {}, (origin + QPointF(300, 206)).toPoint());
    QCOMPARE(page.envelope.ink.strokes().size(), 0);
}

void NoteInputTest::eraserGestureIsOneUndoStepAndNoneWhenNothingErased()
{
    NotePageView view;
    noSmoothing(&view);
    NotePage page;
    view.setPage(&page);
    const QPointF origin = view.pageRect().topLeft();
    QSignalSpy undoSpy(&view, &NotePageView::undoAvailable);

    // An empty swipe must not grow the history (defect P5).
    view.setTool(NoteTool::Eraser);
    QTest::mousePress(&view, Qt::LeftButton, {}, (origin + QPointF(100, 100)).toPoint());
    QTest::mouseMove(&view, (origin + QPointF(200, 100)).toPoint());
    QTest::mouseRelease(&view, Qt::LeftButton, {}, (origin + QPointF(200, 100)).toPoint());
    QCOMPARE(page.envelope.ink.strokes().size(), 0);
    QCOMPARE(undoSpy.count(), 0);

    view.setTool(NoteTool::Pen);
    mouseStroke(&view, origin + QPointF(200, 200), origin + QPointF(400, 200));
    mouseStroke(&view, origin + QPointF(200, 300), origin + QPointF(400, 300));
    QCOMPARE(page.envelope.ink.strokes().size(), 2);

    // One drag across both strokes …
    view.setTool(NoteTool::Eraser);
    QTest::mousePress(&view, Qt::LeftButton, {}, (origin + QPointF(300, 200)).toPoint());
    QTest::mouseMove(&view, (origin + QPointF(300, 300)).toPoint());
    QTest::mouseRelease(&view, Qt::LeftButton, {}, (origin + QPointF(300, 300)).toPoint());
    QCOMPARE(page.envelope.ink.strokes().size(), 0);

    // … and ONE undo brings both back: the gesture is a single step.
    view.undo();
    QCOMPARE(page.envelope.ink.strokes().size(), 2);
}

void NoteInputTest::barrelButtonIsATemporaryEraser()
{
    NotePageView view;
    noSmoothing(&view);
    NotePage page;
    view.setPage(&page);
    const QPointF origin = view.pageRect().topLeft();
    mouseStroke(&view, origin + QPointF(200, 200), origin + QPointF(400, 200));
    QCOMPARE(page.envelope.ink.strokes().size(), 1);
    QCOMPARE(view.tool(), NoteTool::Pen);

    auto *stylus = new QPointingDevice(QStringLiteral("test-stylus"), 1,
                                       QInputDevice::DeviceType::Stylus,
                                       QPointingDevice::PointerType::Pen,
                                       QInputDevice::Capability::Position
                                           | QInputDevice::Capability::Pressure,
                                       1, 1);
    // Press with the tip down AND the barrel button held: the eraser is
    // borrowed for exactly this gesture (plan section 3.3 rule 2).
    const QPointF pos = origin + QPointF(300, 200);
    QTabletEvent press(QEvent::TabletPress, stylus, pos, pos, 0.9, 0, 0, 0, 0, 0,
                       Qt::NoModifier, Qt::LeftButton, Qt::LeftButton | Qt::MiddleButton);
    QCoreApplication::sendEvent(&view, &press);
    QCOMPARE(view.tool(), NoteTool::Eraser);
    QCOMPARE(page.envelope.ink.strokes().size(), 0); // live eraser, at press
    QTabletEvent release(QEvent::TabletRelease, stylus, pos, pos, 0.0, 0, 0, 0, 0, 0,
                         Qt::NoModifier, Qt::NoButton, Qt::NoButton);
    QCoreApplication::sendEvent(&view, &release);
    QCOMPARE(view.tool(), NoteTool::Pen); // reverted on release
}

void NoteInputTest::inkingRepaintsOnlyTheSegmentRect()
{
    NotePageView view;
    NotePage page;
    view.setPage(&page);
    view.show();
    QVERIFY(QTest::qWaitForWindowExposed(&view));

    // Counts paint regions seen during the stroke (defect P7: every pen
    // sample used to repaint the WHOLE visible page).
    struct PaintCounter : QObject {
        int paints = 0;
        qint64 maxArea = 0;
        bool eventFilter(QObject *, QEvent *event) override
        {
            if (event->type() == QEvent::Paint) {
                const QRect rect = static_cast<QPaintEvent *>(event)->rect();
                ++paints;
                maxArea = qMax(maxArea, qint64(rect.width()) * rect.height());
            }
            return false;
        }
    } counter;
    view.installEventFilter(&counter);

    const QPointF start = view.pageRect().topLeft() + QPointF(100, 100);
    QTest::mousePress(&view, Qt::LeftButton, {}, start.toPoint());
    for (int i = 1; i <= 20; ++i) {
        QTest::mouseMove(&view, (start + QPointF(i * 4.0, i)).toPoint());
    }
    QCoreApplication::processEvents();
    QTest::mouseRelease(&view, Qt::LeftButton, {}, (start + QPointF(84, 20)).toPoint());

    QVERIFY2(counter.paints > 0, "the stroke repainted something");
    const qint64 pageArea =
        qint64(view.pageRect().width()) * qint64(view.pageRect().height());
    QVERIFY2(counter.maxArea < pageArea / 4,
             qPrintable(QStringLiteral("largest in-stroke repaint %1 px² vs page %2 px²")
                            .arg(counter.maxArea)
                            .arg(pageArea)));
}

void NoteInputTest::pageIsPaperUnderADarkPalette()
{
    NotePageView view;
    // Defect P12: the page followed palette().base(), so a dark theme made
    // the PAGE dark and the default black pen vanished (D-004: the page is
    // paper).
    QPalette dark;
    dark.setColor(QPalette::Window, QColor(0x2b, 0x2b, 0x30));
    dark.setColor(QPalette::Base, QColor(0x21, 0x21, 0x26));
    dark.setColor(QPalette::Text, QColor(0xf0, 0xf0, 0xf0));
    dark.setColor(QPalette::WindowText, QColor(0xf0, 0xf0, 0xf0));
    view.setPalette(dark);
    NotePage page;
    view.setPage(&page);
    const QImage shot = view.grab().toImage();
    const QColor centre = shot.pixelColor(view.pageRect().center().toPoint());
    QCOMPARE(centre, QColor(Qt::white));
}

void NoteInputTest::toolShortcutsSwitchTools()
{
    NoteWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QTest::keyClick(&window, Qt::Key_H);
    QCOMPARE(window.pageView()->tool(), NoteTool::Highlighter);
    QTest::keyClick(&window, Qt::Key_E);
    QCOMPARE(window.pageView()->tool(), NoteTool::Eraser);
    QTest::keyClick(&window, Qt::Key_P);
    QCOMPARE(window.pageView()->tool(), NoteTool::Pen);

    // With a text field focused the letter types instead (QLineEdit accepts
    // ShortcutOverride for printable keys) — typing must never swap tools.
    QAction *find =
        window.action(QString::fromLatin1(QindaOffice::Shell::ActionId::kEditFind));
    QVERIFY(find != nullptr);
    find->trigger();
    auto *findEdit = window.findChild<QLineEdit *>(QStringLiteral("noteFindEdit"));
    QVERIFY(findEdit != nullptr);
    findEdit->setFocus();
    QTRY_VERIFY(findEdit->hasFocus());
    QTest::keyClick(findEdit, Qt::Key_H);
    QCOMPARE(findEdit->text(), QStringLiteral("h"));
    QCOMPARE(window.pageView()->tool(), NoteTool::Pen);
}

void NoteInputTest::persistIsDebounced()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/Work");
    NoteWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QString error;
    QVERIFY2(window.noteDocument()->save(path, &error), qPrintable(error));
    const QString file = path
        + QStringLiteral("/sections/%1/pages/%2.json")
              .arg(window.currentSectionId(), window.currentPageId());
    QVERIFY2(QFile::exists(file), qPrintable(file));

    const QPointF centre = window.pageView()->pageRect().center();
    const auto threeStrokes = [&window, centre](qreal yOffset) {
        for (int i = 0; i < 3; ++i) {
            mouseStroke(window.pageView(), centre + QPointF(-60 + i * 20, yOffset),
                        centre + QPointF(-40 + i * 20, yOffset));
        }
    };

    // Defect T10: every stroke used to rewrite the page JSON on the spot.
    // Three quick edits coalesce into ONE debounced write…
    const QDateTime savedAt = QFileInfo(file).lastModified();
    threeStrokes(0);
    QCOMPARE(QFileInfo(file).lastModified(), savedAt); // timer still pending
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(file).lastModified() > savedAt, 3000);

    // … and the next burst is again a pending timer, not three writes.
    const QDateTime firstWrite = QFileInfo(file).lastModified();
    threeStrokes(40);
    QCOMPARE(QFileInfo(file).lastModified(), firstWrite);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(file).lastModified() > firstWrite, 3000);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaNote"));
    NoteInputTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_note_input.moc"
