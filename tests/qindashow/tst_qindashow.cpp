// SPDX-License-Identifier: GPL-3.0-or-later
// QindaShow gates (plan section 8): a deck round-trips through
// ShowDocument (.fodp native, .pptx through LibreOffice live/skipped); the
// canvas item mutates the model (insert/delete/z-order/build) and reports
// edits; the host wires the deck, notes and undo; the controller reveals
// builds before advancing slides.

#include "show_test_support.h"

#include "qindashow/src/pdf_export.h"
#include "qindashow/src/presentation_controller.h"
#include "qindashow/src/show_ai.h"
#include "qindashow/src/text_bridge.h"

#include <QProcess>
#include <QSignalSpy>
#include <QTextDocument>
#include <QUndoStack>

namespace ShowModel = QindaOffice::Show;
using namespace ShowTest;
using namespace QindaOffice::Apps::Show;

class QindaShowTest : public QObject {
    Q_OBJECT
private slots:
    void documentRoundTrip();
    void documentPptxSave();
    void canvasEditsModel();
    void hostWiresDeckNotesAndUndo();
    void controllerAdvancesBuildsBeforeSlides();
    void slidesFromOutlineParse();
    void pdfExportWritesSlidesAndNotes();
    void textBridgeKeepsRuns();
};

void QindaShowTest::documentRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/deck.fodp");
    {
        ShowDocument doc;
        doc.presentation()->addSlide(QStringLiteral("Second")).transition = QStringLiteral("fade");
        QVERIFY2(doc.save(path), "save fodp");
        QCOMPARE(doc.canonicalPath(), path);
        QVERIFY(!doc.isDirty());
    }
    ShowDocument back;
    QString error;
    QVERIFY2(back.load(path, &error), qPrintable(error));
    QCOMPARE(back.presentation()->slides.size(), 2); // ctor slide + added
    QCOMPARE(back.presentation()->slides.at(1).title, QStringLiteral("Second"));
    QCOMPARE(back.presentation()->slides.at(1).transition, QStringLiteral("fade"));
    QVERIFY(!back.isDirty());
}

void QindaShowTest::documentPptxSave()
{
    QProcess probe;
    probe.start(QStringLiteral("soffice"), {QStringLiteral("--headless"), QStringLiteral("--version")});
    if (!probe.waitForFinished(15000) || probe.exitCode() != 0) {
        QSKIP("LibreOffice unavailable in this environment");
    }
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/deck.pptx");
    ShowDocument doc;
    Q_UNUSED(doc.presentation()->addSlide(QStringLiteral("Export")));
    QString error;
    QVERIFY2(doc.save(path, &error), qPrintable(error));
    QVERIFY(QFile::exists(path));
    QVERIFY(QFile(path).size() > 0);
}

void QindaShowTest::canvasEditsModel()
{
    ShowModel::Slide slide;
    slide.id = QStringLiteral("s1");
    SlideCanvasItem canvas;
    QSignalSpy removedSpy(&canvas, &SlideCanvasItem::shapeRemoved);
    QSignalSpy selectionSpy(&canvas, &SlideCanvasItem::selectionChanged);
    canvas.setSlide(&slide, QSizeF(720, 405));

    const QString first = canvas.addShape(ShowModel::ShapeKind::Rectangle, QRectF(10, 10, 100, 80));
    QVERIFY(!first.isEmpty());
    QCOMPARE(slide.shapes.size(), 1);
    QCOMPARE(slide.shapes.first().kind, ShowModel::ShapeKind::Rectangle);
    // An inserted shape is the selection, so the styling commands work on it.
    QCOMPARE(canvas.selectedShapeId(), first);
    QVERIFY(!selectionSpy.isEmpty());

    canvas.toggleBuildSelected();
    QCOMPARE(slide.shapes.first().buildStep, 1);

    const QString second = canvas.addShape(ShowModel::ShapeKind::Ellipse, QRectF(200, 10, 50, 50));
    canvas.raiseSelected(); // ellipse already on top: no-op
    QCOMPARE(slide.shapes.at(1).kind, ShowModel::ShapeKind::Ellipse);
    canvas.lowerSelected();
    QCOMPARE(slide.shapes.at(0).kind, ShowModel::ShapeKind::Ellipse);
    QCOMPARE(slide.shapes.at(1).kind, ShowModel::ShapeKind::Rectangle);
    canvas.raiseSelected();
    QCOMPARE(slide.shapes.at(1).kind, ShowModel::ShapeKind::Ellipse);

    // Hit-testing is topmost-first over the model's z order.
    QCOMPARE(canvas.shapeAt(QPointF(210, 20)), second);
    QCOMPARE(canvas.shapeAt(QPointF(20, 20)), first);
    QVERIFY(canvas.shapeAt(QPointF(600, 300)).isEmpty());

    canvas.deleteSelected();
    QCOMPARE(slide.shapes.size(), 1);
    QCOMPARE(removedSpy.count(), 1);
    QVERIFY(!canvas.hasSelection());
}

void QindaShowTest::hostWiresDeckNotesAndUndo()
{
    ShowHost host;
    ShowDocument *doc = host.showDocument();
    QCOMPARE(doc->presentation()->slides.size(), 1);
    QCOMPARE(host.canvas()->slide(), &doc->presentation()->slides.first());

    host.addSlide();
    host.addSlide();
    QCOMPARE(doc->presentation()->slides.size(), 3);
    QCOMPARE(host.slideCount(), 3);
    QCOMPARE(host.currentSlideRow(), 2);
    // The canvas follows the current slide, not a detached copy.
    QCOMPARE(host.canvas()->slide(), &doc->presentation()->slides.at(2));

    host.setNotes(QStringLiteral("Remember the demo flow"));
    QCOMPARE(doc->presentation()->slides.at(2).notes, QStringLiteral("Remember the demo flow"));
    host.setTransitionIndex(1);
    QCOMPARE(doc->presentation()->slides.at(2).transition, QStringLiteral("fade"));

    doc->undoStack()->undo(); // undo the second addSlide
    QCOMPARE(doc->presentation()->slides.size(), 2);
    QCOMPARE(host.slideCount(), 2);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/win.fodp");
    QVERIFY2(doc->save(path), "save from the host");
    ShowHost reopen;
    QVERIFY2(reopen.loadPath(path), "load through the shell");
    QCOMPARE(reopen.showDocument()->presentation()->slides.size(), 2);
    QCOMPARE(reopen.currentSlideRow(), 0);
}

void QindaShowTest::controllerAdvancesBuildsBeforeSlides()
{
    ShowModel::Presentation deck;
    ShowModel::Slide &first = deck.addSlide(QStringLiteral("One"));
    ShowModel::Shape build;
    build.id = QStringLiteral("b");
    build.buildStep = 2;
    build.animation = ShowModel::ShapeAnimation::Fade;
    build.animationMs = 400;
    first.shapes.append(build);
    deck.addSlide(QStringLiteral("Two"));
    PresentationController controller(&deck);
    QCOMPARE(controller.buildCap(0), 2);
    QCOMPARE(controller.slideCount(), 2);
    controller.advance(1);
    QCOMPARE(controller.build(), 1);
    controller.advance(1);
    QCOMPARE(controller.build(), 2);
    // The QML player reads the duration of the step it is arriving at.
    QCOMPARE(controller.buildDurationMs(), 400);
    controller.advance(1); // builds exhausted: next slide
    QCOMPARE(controller.slide(), 1);
    QCOMPARE(controller.build(), 0);
    QCOMPARE(controller.buildDurationMs(), 0);
    QVERIFY(controller.atEnd());
    controller.advance(1); // no-op at the end
    QCOMPARE(controller.slide(), 1);
    controller.advance(-1);
    QCOMPARE(controller.slide(), 0);
    QCOMPARE(controller.build(), 2); // fully revealed going back
}

void QindaShowTest::slidesFromOutlineParse()
{
    const QString outline = QStringLiteral("Here is your deck.\n"
                                           "## Intro\n"
                                           "- First point\n"
                                           "- Second point\n"
                                           "Notes: say hello\n"
                                           "## Deep dive\n"
                                           "- Detail\n");
    const QList<ShowModel::Slide> slides = slidesFromOutline(outline, QSizeF(720, 405));
    QCOMPARE(slides.size(), 2);
    QCOMPARE(slides.at(0).title, QStringLiteral("Intro"));
    QCOMPARE(slides.at(0).shapes.size(), 2);
    QCOMPARE(slides.at(0).shapes.at(0).paragraphs.first().runs.first().text, QStringLiteral("First point"));
    QCOMPARE(slides.at(0).notes, QStringLiteral("say hello"));
    QCOMPARE(slides.at(1).title, QStringLiteral("Deep dive"));
    QCOMPARE(slides.at(1).shapes.size(), 1);
}

void QindaShowTest::pdfExportWritesSlidesAndNotes()
{
    ShowModel::Presentation deck;
    deck.addSlide(QStringLiteral("One")).notes = QStringLiteral("First notes");
    deck.addSlide(QStringLiteral("Two"));
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.path() + QStringLiteral("/deck.pdf");
    QString error;
    QVERIFY2(exportDeckPdf(deck, path, /*withNotesPages=*/true, &error), qPrintable(error));
    QVERIFY(QFile::exists(path));
    QFile pdf(path);
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    QCOMPARE(pdf.read(5), QByteArrayLiteral("%PDF-"));
}

void QindaShowTest::textBridgeKeepsRuns()
{
    ShowModel::TextParagraph paragraph;
    ShowModel::TextRun bold;
    bold.text = QStringLiteral("Bold");
    bold.bold = true;
    bold.pointSize = 32;
    paragraph.runs.append(bold);
    ShowModel::TextRun colored;
    colored.text = QStringLiteral(" color");
    colored.color = QColor(200, 30, 40);
    paragraph.runs.append(colored);

    QTextDocument *document = documentFromParagraphs({paragraph});
    const QList<ShowModel::TextParagraph> back = paragraphsFromDocument(*document);
    delete document;
    QCOMPARE(back.size(), 1);
    QCOMPARE(back.first().runs.size(), 2);
    QCOMPARE(back.first().runs.at(0).text, QStringLiteral("Bold"));
    QVERIFY(back.first().runs.at(0).bold);
    QCOMPARE(back.first().runs.at(0).pointSize, 32);
    QCOMPARE(back.first().runs.at(1).color, QColor(200, 30, 40));
}

SHOW_TEST_MAIN(QindaShowTest)

#include "tst_qindashow.moc"
