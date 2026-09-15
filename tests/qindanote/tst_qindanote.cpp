// SPDX-License-Identifier: GPL-3.0-or-later
// M7 gates (plan section 8): a notebook survives reopen (ink + text),
// bounded snapshot undo works, tablet pen events draw while touch events
// are consumed without inking (palm-rejection baseline).

#include "qindanote/src/note_page_view.h"
#include "qindanote/src/note_window.h"

#include <QApplication>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QTabletEvent>
#include <QtTest>

using namespace QindaOffice::Apps::Notes;
namespace Ink = QindaOffice::Ink;

class QindaNoteTest : public QObject {
    Q_OBJECT
private slots:
    void notebookSurvivesReopen();
    void undoRemovesStrokes();
    void penEventsInkAndTouchIsIgnored();
};

void QindaNoteTest::notebookSurvivesReopen()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/Work");
    {
        NoteDocument doc;
        QVERIFY2(doc.save(path), "create notebook");
        NoteWindow window;
        // Drive through the document directly (window wiring shares paths).
        Notebook *notebook = doc.notebook();
        QCOMPARE(notebook->sections().size(), 1);
        NoteSection &section = const_cast<QVector<NoteSection> &>(notebook->sections()).first();
        NotePage &page = section.pages.first();
        QString error;
        QVERIFY(doc.notebook()->loadPage(section, page, &error));
        Ink::Stroke stroke;
        stroke.samples = {{QPointF(10, 10), 0.7}, {QPointF(200, 120), 0.7}};
        stroke.complete = true;
        page.envelope.ink.appendStroke(stroke);
        page.envelope.blocksJson = R"({"blocks":[{"type":"text","text":"remember milk"}]})";
        QVERIFY(doc.notebook()->persistPage(section, page, &error));
        QVERIFY(doc.notebook()->save(&error));
    }
    {
        NoteDocument doc;
        QString error;
        QVERIFY2(doc.load(path, &error), qPrintable(error));
        Notebook *notebook = doc.notebook();
        NoteSection &section = const_cast<QVector<NoteSection> &>(notebook->sections()).first();
        NotePage &page = section.pages.first();
        QVERIFY(notebook->loadPage(section, page, &error));
        QCOMPARE(page.envelope.ink.strokes().size(), 1);
        QVERIFY(page.envelope.blocksJson.contains("remember milk"));
    }
}

void QindaNoteTest::undoRemovesStrokes()
{
    NotePageView view;
    NotePage page;
    page.id = QStringLiteral("p");
    view.setPage(&page);
    // Two strokes via the mouse fallback path.
    for (int i = 0; i < 2; ++i) {
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, QPoint(100 + i * 40, 80));
        for (int step = 1; step <= 5; ++step) {
            QTest::mouseMove(&view, QPoint(100 + i * 40 + step * 20, 80 + step * 10));
        }
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, QPoint(220, 140));
    }
    QCOMPARE(page.envelope.ink.strokes().size(), 2);
    view.undo();
    QCOMPARE(page.envelope.ink.strokes().size(), 1);
    view.undo();
    QCOMPARE(page.envelope.ink.strokes().size(), 0);
}

void QindaNoteTest::penEventsInkAndTouchIsIgnored()
{
    NotePageView view;
    NotePage page;
    view.setPage(&page);

    // A pen tablet event draws.
    const QPointF pos(120, 100);
    auto *stylus = new QPointingDevice(QStringLiteral("test-stylus"), 1,
                                       QInputDevice::DeviceType::Stylus,
                                       QPointingDevice::PointerType::Pen,
                                       QInputDevice::Capability::Position
                                           | QInputDevice::Capability::Pressure,
                                       1, 1);
    QTabletEvent press(QEvent::TabletPress, stylus, pos, pos, 0.9, 0, 0, 0, 0, 0,
                       Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::sendEvent(&view, &press);
    QTabletEvent move(QEvent::TabletMove, stylus, QPointF(180, 140), QPointF(180, 140), 0.9, 0,
                      0, 0, 0, 0, Qt::NoModifier, Qt::NoButton, Qt::NoButton);
    QCoreApplication::sendEvent(&view, &move);
    QTabletEvent release(QEvent::TabletRelease, stylus, QPointF(180, 140), QPointF(180, 140),
                         0.0, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::NoButton, Qt::NoButton);
    QCoreApplication::sendEvent(&view, &release);
    QCOMPARE(page.envelope.ink.strokes().size(), 1);

    // A touch tablet event (resting palm) is consumed without inking.
    const int before = page.envelope.ink.strokes().size();
    auto *screen = new QPointingDevice(QStringLiteral("test-screen"), 2,
                                       QInputDevice::DeviceType::TouchScreen,
                                       QPointingDevice::PointerType::Finger,
                                       QInputDevice::Capability::Position, 1, 1);
    QTabletEvent palm(QEvent::TabletPress, screen, pos, pos, 1.0, 0, 0, 0, 0, 0, Qt::NoModifier,
                      Qt::LeftButton, Qt::LeftButton);
    QCoreApplication::sendEvent(&view, &palm);
    QCOMPARE(page.envelope.ink.strokes().size(), before);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaNote"));
    QindaNoteTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_qindanote.moc"
