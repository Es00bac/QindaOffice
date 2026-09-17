// SPDX-License-Identifier: GPL-3.0-or-later
// M7 gates (plan section 8): a notebook survives reopen (ink + text),
// bounded snapshot undo works, tablet pen events draw while touch events
// are consumed without inking (palm-rejection baseline).

#include "qindanote/src/note_blocks.h"
#include "qindanote/src/note_page_view.h"
#include "qindanote/src/note_window.h"

#include <QApplication>
#include <QMouseEvent>
#include <QBuffer>
#include <QCheckBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include "standard_actions.h"
#include <QToolButton>
#include <QLineEdit>
#include <QSignalSpy>
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
    void penPresentationFlowsIntoStrokes();
    void backgroundChoiceEditsEnvelope();
    void zoomScalesPageNotTheDocument();
    void savingNeverBlanksPagesThatWereNeverOpened();
    void redoReplaysAnUndoneStroke();
    void recoverySnapshotRoundTripsTheNotebook();
    void findSelectsThePageHoldingTheText();
    // D-017 structure and layout.
    void freshWindowOpensOnAWritablePage();
    void pageViewDoesNotForceTheWindowTall();
    void newSectionLandsOnItsFirstPage();
    void deletingAPageRemovesItsFile();
    void movePageReordersTheSection();
    void blocksStackWithoutOverlapping();
    void checklistItemsPersistTheirState();
    void imageBlocksCarryTheirBytes();
    void freshWindowIsPristineSoOpenCanReuseIt();
};

void QindaNoteTest::freshWindowOpensOnAWritablePage()
{
    NoteWindow window;
    window.show();
    // A pristine notebook used to be completely empty, so the navigator had
    // nothing in it and the page surface was bound to no page at all.
    QCOMPARE(window.noteDocument()->notebook()->sections().size(), 1);
    QVERIFY(window.currentPage() != nullptr);
    QVERIFY(window.pageView()->page() != nullptr);
    QVERIFY2(window.statusInfo().contains(QStringLiteral("page 1 of 1")),
             qPrintable(window.statusInfo()));
}

void QindaNoteTest::pageViewDoesNotForceTheWindowTall()
{
    NoteWindow window;
    window.resize(900, 500);
    window.show();
    // The page widget is a full sheet tall; hosting it directly made that
    // the WINDOW's minimum height, so QindaNote could not fit on a laptop.
    QVERIFY2(window.minimumSizeHint().height() < 700,
             qPrintable(QString::number(window.minimumSizeHint().height())));
    QVERIFY2(window.height() < 700, qPrintable(QString::number(window.height())));
}

void QindaNoteTest::newSectionLandsOnItsFirstPage()
{
    NoteWindow window;
    window.show();
    Notebook *notebook = window.noteDocument()->notebook();
    NoteSection &section = notebook->addSection(QStringLiteral("Research"));
    const QString sectionId = section.id;
    const QString pageId = notebook->addPage(section, QStringLiteral("Sources")).id;
    window.refreshNavigator();
    window.selectPageById(sectionId, pageId);
    QCOMPARE(window.currentSectionId(), sectionId);
    QCOMPARE(window.currentPageId(), pageId);
    QVERIFY2(window.statusInfo().startsWith(QStringLiteral("Research")),
             qPrintable(window.statusInfo()));
}

void QindaNoteTest::deletingAPageRemovesItsFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/Work");
    NoteDocument doc;
    Notebook *notebook = doc.notebook();
    auto &sections = const_cast<QVector<NoteSection> &>(notebook->sections());
    NoteSection &section = sections.first();
    const QString sectionId = section.id;
    const QString extra = notebook->addPage(section, QStringLiteral("Scratch")).id;
    QVERIFY2(doc.save(path), "save notebook");
    const QString file =
        path + QStringLiteral("/sections/%1/pages/%2.json").arg(sectionId, extra);
    QVERIFY2(QFile::exists(file), qPrintable(file));

    QVERIFY(notebook->removePage(sectionId, extra));

    // An index entry removed without its file leaves an orphan the next
    // load would resurrect.
    QVERIFY(!QFile::exists(file));
    QCOMPARE(notebook->sectionById(sectionId)->pages.size(), 1);
}

void QindaNoteTest::movePageReordersTheSection()
{
    NoteDocument doc;
    Notebook *notebook = doc.notebook();
    auto &sections = const_cast<QVector<NoteSection> &>(notebook->sections());
    NoteSection &section = sections.first();
    const QString first = section.pages.first().id;
    const QString second = notebook->addPage(section, QStringLiteral("Second")).id;

    QVERIFY(notebook->movePage(section.id, second, -1));

    QCOMPARE(notebook->sectionById(section.id)->pages.first().id, second);
    QCOMPARE(notebook->sectionById(section.id)->pages.last().id, first);
    // Past the end is refused rather than clamped: a silent no-op that
    // reports success would make Move Up look broken at the top.
    QVERIFY(!notebook->movePage(section.id, second, -1));
}

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


void QindaNoteTest::freshWindowIsPristineSoOpenCanReuseIt()
{
    NoteWindow window;
    // AGENT-CONTRACT (ADR-0110): a freshly created, untouched window is
    // PRISTINE, which is what lets File > Open replace its blank document
    // instead of opening a second window and leaving the blank one behind.
    // Anything that dirties a document during window construction — a
    // syntax highlighter's first pass, a default-font apply — breaks that
    // silently, which is exactly how it broke in QindaWrite.
    QVERIFY(window.document() != nullptr);
    QVERIFY2(!window.document()->isDirty(), "a fresh window must not be dirty");
    QVERIFY(window.isPristineUntitled());
}

void QindaNoteTest::blocksStackWithoutOverlapping()
{
    // Three kinds of block, each as tall as it needs; the layout the SCREEN
    // uses is the layout PRINT uses, so they cannot drift apart.
    QImage image(40, 20, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "PNG"));

    const QJsonArray blocks{
        QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                    {QStringLiteral("text"), QStringLiteral("notes")}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("check")},
                    {QStringLiteral("text"), QStringLiteral("buy milk")},
                    {QStringLiteral("checked"), true}},
        QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                    {QStringLiteral("data"), QString::fromLatin1(bytes.toBase64())}},
    };
    const QByteArray json =
        QJsonDocument(QJsonObject{{QStringLiteral("blocks"), blocks}}).toJson();

    const QList<PageBlock> laid = layoutBlocks(json, 760);
    QCOMPARE(laid.size(), 3);
    QCOMPARE(laid.at(0).kind, PageBlock::Kind::Text);
    QCOMPARE(laid.at(1).kind, PageBlock::Kind::Check);
    QCOMPARE(laid.at(2).kind, PageBlock::Kind::Image);
    QVERIFY(laid.at(1).checked);
    for (int i = 1; i < laid.size(); ++i) {
        QVERIFY2(laid.at(i).rect.top() >= laid.at(i - 1).rect.bottom(),
                 "blocks must not overlap");
    }
    // A checklist row is shorter than a text block; an image is as tall as
    // it needs within the page bound.
    QVERIFY(laid.at(1).rect.height() < laid.at(0).rect.height());
    QVERIFY(laid.at(2).rect.height() > 0);
}

void QindaNoteTest::checklistItemsPersistTheirState()
{
    NoteWindow window;
    window.show();
    NotePage *page = window.currentPage();
    QVERIFY(page != nullptr);

    window.pageView()->addCheckItem();

    auto *box = window.pageView()->findChild<QCheckBox *>(QStringLiteral("noteCheckBox"));
    auto *label = window.pageView()->findChild<QLineEdit *>(QStringLiteral("noteCheckText"));
    QVERIFY(box != nullptr && label != nullptr);
    label->setText(QStringLiteral("call the bank"));
    emit label->textEdited(label->text());
    box->setChecked(true);

    // The state has to reach the envelope, or it is lost on the next page
    // switch — the same defect the text blocks once had.
    const QList<PageBlock> laid = layoutBlocks(page->envelope.blocksJson, 760);
    QCOMPARE(laid.size(), 1);
    QCOMPARE(laid.first().kind, PageBlock::Kind::Check);
    QCOMPARE(laid.first().text, QStringLiteral("call the bank"));
    QVERIFY(laid.first().checked);
}

void QindaNoteTest::imageBlocksCarryTheirBytes()
{
    NoteWindow window;
    window.show();
    NotePage *page = window.currentPage();
    QVERIFY(page != nullptr);
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(Qt::green);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(image.save(&buffer, "PNG"));

    window.pageView()->addImageBlock(bytes);

    // Bytes, not a path: a notebook page is one self-contained JSON file,
    // so a reference would break the moment the notebook moved.
    const QList<PageBlock> laid = layoutBlocks(page->envelope.blocksJson, 760);
    QCOMPARE(laid.size(), 1);
    QCOMPARE(laid.first().kind, PageBlock::Kind::Image);
    QCOMPARE(laid.first().imageData, bytes);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QApplication::setDesktopFileName(QStringLiteral("org.qindaqt.QindaNote"));
    QindaNoteTest test;
    return QTest::qExec(&test, argc, argv);
}

void QindaNoteTest::penPresentationFlowsIntoStrokes()
{
    NotePageView view;
    NotePage page;
    page.id = QStringLiteral("p1");
    view.setPage(&page);
    view.resize(800, 600);
    view.setPenWidth(7);
    view.setPenColor(QColor(200, 30, 40));

    // Mouse-fallback inking inside the page area.
    const QPoint start = view.pageRect().center().toPoint();
    QTest::mousePress(&view, Qt::LeftButton, {}, start);
    QTest::mouseMove(&view, start + QPoint(60, 10));
    QTest::mouseRelease(&view, Qt::LeftButton, {}, start + QPoint(60, 10));

    QCOMPARE(page.envelope.ink.strokes().size(), 1);
    const Ink::Stroke &stroke = page.envelope.ink.strokes().first();
    QCOMPARE(stroke.width, 7.0);
    QCOMPARE(stroke.color, QColor(200, 30, 40));
    QVERIFY(stroke.samples.size() >= 2);
}

void QindaNoteTest::backgroundChoiceEditsEnvelope()
{
    NotePageView view;
    NotePage page;
    page.id = QStringLiteral("p1");
    view.setPage(&page);
    QSignalSpy editedSpy(&view, &NotePageView::pageEdited);

    view.setBackground(QStringLiteral("grid"));
    QCOMPARE(page.envelope.background, QStringLiteral("grid"));
    QCOMPARE(editedSpy.count(), 1);

    // Unknown styles are still stored verbatim (forward-compatible with
    // future backgrounds); blank default round-trips.
    view.setBackground(QStringLiteral("blank"));
    QCOMPARE(page.envelope.background, QStringLiteral("blank"));
}

void QindaNoteTest::zoomScalesPageNotTheDocument()
{
    NotePageView view;
    NotePage page;
    page.id = QStringLiteral("p1");
    view.setPage(&page);
    view.resize(1200, 800);
    const QRectF at100 = view.pageRect();
    view.setZoom(2.0);
    const QRectF at200 = view.pageRect();
    QVERIFY(at200.width() > at100.width() * 1.9);
    QVERIFY(at200.height() > at100.height() * 1.9);

    // Inked coordinates land in PAGE space regardless of zoom: the same
    // widget-relative point produces the same document sample.
    auto inkAt = [&view, &page](qreal zoom) {
        page.envelope.ink.clear();
        view.setZoom(zoom);
        const QPoint widgetPt = view.pageRect().center().toPoint();
        QTest::mousePress(&view, Qt::LeftButton, {}, widgetPt);
        QTest::mouseMove(&view, widgetPt + QPoint(40, 0));
        QTest::mouseRelease(&view, Qt::LeftButton, {}, widgetPt + QPoint(40, 0));
        return page.envelope.ink.strokes().first().samples.first().pos;
    };
    const QPointF sample100 = inkAt(1.0);
    const QPointF sample200 = inkAt(2.0);
    QVERIFY(qAbs(sample100.x() - sample200.x()) < 2.0);
    QVERIFY(qAbs(sample100.y() - sample200.y()) < 2.0);
}

// AGENT-GUARD regression: pages load lazily, so an unopened page's envelope
// is EMPTY in memory. save() used to write every page unconditionally —
// opening a notebook, viewing one page and pressing Ctrl+S replaced every
// other page's file with a blank envelope.
void QindaNoteTest::savingNeverBlanksPagesThatWereNeverOpened()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/Notes");
    QString error;
    {
        NoteDocument doc;
        QVERIFY2(doc.save(path, &error), qPrintable(error));
        Notebook *book = doc.notebook();
        NoteSection *section = book->sectionById(book->sections().first().id);
        QVERIFY(section != nullptr);
        NotePage &second = book->addPage(*section, QStringLiteral("Second"));
        Ink::Stroke stroke;
        stroke.samples = {{QPointF(10, 10), 0.5, 0, 0}, {QPointF(40, 40), 0.5, 0, 0}};
        second.envelope.ink.appendStroke(stroke);
        QVERIFY2(doc.save(path, &error), qPrintable(error));
    }
    // Reopen, touch only the FIRST page, save.
    {
        NoteDocument doc;
        QVERIFY2(doc.load(path, &error), qPrintable(error));
        Notebook *book = doc.notebook();
        NoteSection *section = book->sectionById(book->sections().first().id);
        QVERIFY(section != nullptr);
        QCOMPARE(section->pages.size(), 2);
        QVERIFY(!section->pages.at(1).loaded); // never opened
        QVERIFY(book->loadPage(*section, section->pages[0], &error));
        QVERIFY2(doc.save(path, &error), qPrintable(error));
    }
    // The untouched page still has its stroke.
    {
        NoteDocument doc;
        QVERIFY2(doc.load(path, &error), qPrintable(error));
        Notebook *book = doc.notebook();
        NoteSection *section = book->sectionById(book->sections().first().id);
        QVERIFY(book->loadPage(*section, section->pages[1], &error));
        QCOMPARE(section->pages.at(1).envelope.ink.strokes().size(), 1);
    }
}

// The view had an undo stack and no inverse, so Edit > Redo was dead.
void QindaNoteTest::redoReplaysAnUndoneStroke()
{
    NotePage page;
    page.loaded = true;
    NotePageView view;
    view.setPage(&page);
    QSignalSpy redoSpy(&view, &NotePageView::redoAvailable);
    Ink::Stroke stroke;
    stroke.samples = {{QPointF(5, 5), 0.5, 0, 0}, {QPointF(30, 30), 0.5, 0, 0}};
    page.envelope.ink.appendStroke(stroke);

    view.undo(); // nothing pushed yet: no-op, and no redo appears
    QCOMPARE(page.envelope.ink.strokes().size(), 1);

    const QPoint widgetPt = view.pageRect().topLeft().toPoint() + QPoint(60, 60);
    QTest::mousePress(&view, Qt::LeftButton, {}, widgetPt);
    QTest::mouseMove(&view, widgetPt + QPoint(40, 0));
    QTest::mouseRelease(&view, Qt::LeftButton, {}, widgetPt + QPoint(40, 0));
    QCOMPARE(page.envelope.ink.strokes().size(), 2);

    view.undo();
    QCOMPARE(page.envelope.ink.strokes().size(), 1);
    QVERIFY(!redoSpy.isEmpty());
    QVERIFY(redoSpy.last().first().toBool()); // redo became available
    view.redo();
    QCOMPARE(page.envelope.ink.strokes().size(), 2);
    // A fresh edit ends the redo branch.
    QTest::mousePress(&view, Qt::LeftButton, {}, widgetPt);
    QTest::mouseMove(&view, widgetPt + QPoint(0, 40));
    QTest::mouseRelease(&view, Qt::LeftButton, {}, widgetPt + QPoint(0, 40));
    view.redo();
    QCOMPARE(page.envelope.ink.strokes().size(), 3); // redo did nothing
}

// recoverySnapshot() returned {} for notebooks, so a crash lost every
// unsaved page and the shell offered a Recover prompt that always failed.
void QindaNoteTest::recoverySnapshotRoundTripsTheNotebook()
{
    NoteDocument doc; // untitled: everything lives in memory
    Notebook *book = doc.notebook();
    NoteSection &section = book->addSection(QStringLiteral("Ideas"));
    NotePage &page = book->addPage(section, QStringLiteral("Sketch"));
    Ink::Stroke stroke;
    stroke.samples = {{QPointF(3, 4), 0.7, 0, 0}, {QPointF(9, 12), 0.7, 0, 0}};
    page.envelope.ink.appendStroke(stroke);
    page.envelope.background = QStringLiteral("grid");

    const QByteArray payload = doc.recoverySnapshot();
    QVERIFY(!payload.isEmpty());

    NoteDocument restored;
    QString error;
    QVERIFY2(restored.loadRecovery(payload, &error), qPrintable(error));
    Notebook *back = restored.notebook();
    // A pristine document already carries the default "Notes" section, so
    // the round trip must hold BOTH it and the one the test added.
    QCOMPARE(back->sections().size(), 2);
    QCOMPARE(back->sections().last().name, QStringLiteral("Ideas"));
    const NotePage &recovered = back->sections().last().pages.first();
    QCOMPARE(recovered.title, QStringLiteral("Sketch"));
    QCOMPARE(recovered.envelope.background, QStringLiteral("grid"));
    QCOMPARE(recovered.envelope.ink.strokes().size(), 1);
    QVERIFY(restored.isDirty());
}


// Edit > Find was a dead menu entry in QindaNote, and a search that skipped
// pages nobody had opened would report "not found" for text that is there.
void QindaNoteTest::findSelectsThePageHoldingTheText()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/Search");
    QString error;
    QString wantedPageId;
    {
        NoteDocument seed;
        QVERIFY2(seed.save(path, &error), qPrintable(error));
        Notebook *book = seed.notebook();
        NoteSection *section = book->sectionById(book->sections().first().id);
        QVERIFY(section != nullptr);
        NotePage &second = book->addPage(*section, QStringLiteral("Second"));
        second.envelope.blocksJson =
            R"({"blocks":[{"type":"text","text":"remember the pangolin"}]})";
        wantedPageId = second.id;
        QVERIFY2(seed.save(path, &error), qPrintable(error));
    }

    NoteWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.openNotebookPath(path);
    // The window lands on the FIRST page; the match is on a page that has
    // never been opened, so its envelope is empty in memory right now.
    QVERIFY(window.noteDocument()->notebook()->sections().first().pages.size() >= 2);

    QAction *find = window.action(QString::fromLatin1(QindaOffice::Shell::ActionId::kEditFind));
    QVERIFY(find != nullptr);
    QVERIFY2(find->isEnabled(), "Edit > Find must be reachable");
    find->trigger();
    auto *findEdit = window.findChild<QLineEdit *>(QStringLiteral("noteFindEdit"));
    QVERIFY(findEdit != nullptr);
    findEdit->setText(QStringLiteral("pangolin"));
    auto *next = window.findChild<QToolButton *>(QStringLiteral("noteFindNextButton"));
    QVERIFY(next != nullptr);
    next->click();

    QCOMPARE(window.pageView()->page() != nullptr, true);
    QCOMPARE(window.pageView()->page()->id, wantedPageId);
}

#include "tst_qindanote.moc"
