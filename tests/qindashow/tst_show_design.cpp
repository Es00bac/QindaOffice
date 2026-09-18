// SPDX-License-Identifier: GPL-3.0-or-later
// QindaShow's design layer: slide masters, build animations, snapping and
// grouping.
//
// All four are model state, so the cases that matter are the ones that
// prove they are: the renderer really uses a master, an animation really
// changes what is drawn mid-build, snapping really moves a rect to the
// candidate, grouping really travels through the native format, and a
// multi-shape gesture is one undo step.

#include "show_test_support.h"

#include "qindashow/src/show_commands.h"
#include "qindashow/src/show_design_actions.h"

#include "fodp_reader.h"
#include "fodp_writer.h"
#include "presentation.h"
#include "slide_renderer.h"
#include "standard_actions.h"

#include <QImage>
#include <QPainter>
#include <QUndoStack>

using namespace ShowTest;
using namespace QindaOffice::Show;
namespace ActionId = QindaOffice::Quick::ActionId;

namespace {

Shape makeBox(const QString &id, const QRectF &rect)
{
    Shape shape;
    shape.id = id;
    shape.kind = ShapeKind::Rectangle;
    shape.rect = rect;
    shape.fill = QColor(Qt::blue);
    return shape;
}

QColor pixelAt(const Slide &slide, const QSizeF &size, const SlideMaster *master, int x, int y,
               int buildStep = 1000, qreal progress = 1.0)
{
    QImage image(200, 120, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.scale(200.0 / size.width(), 120.0 / size.height());
    renderSlide(&painter, slide, size, buildStep, master, progress);
    painter.end();
    return QColor(image.pixel(x, y));
}

QString sid(const char *id)
{
    return QString::fromLatin1(id);
}

} // namespace

class ShowDesignTest : public QObject {
    Q_OBJECT
private slots:
    void masterPaintsTheBackground();
    void masterSuppliesDefaultTypeOnly();
    void buildAnimationChangesWhatIsDrawn();
    void snapPullsAnEdgeToTheSlideCentre();
    void groupingSelectsAndTravelsThroughFodp();
    void multiShapeGestureIsOneUndoStep();
    void mastersRoundTripThroughTheDialogSeam();
    void animationEntriesFollowTheSelection();
};

void ShowDesignTest::masterPaintsTheBackground()
{
    Presentation deck;
    Slide &slide = deck.addSlide(QStringLiteral("One"));
    SlideMaster master;
    master.id = QStringLiteral("m1");
    master.background = QColor(20, 40, 80);
    deck.masters.append(master);
    slide.master = master.id;

    QCOMPARE(deck.masterFor(slide), &deck.masters.first());
    // Without a master a slide is white paper; with one it is the master's.
    QCOMPARE(pixelAt(slide, deck.slideSize, nullptr, 5, 5).name(), QColor(Qt::white).name());
    QCOMPARE(pixelAt(slide, deck.slideSize, deck.masterFor(slide), 5, 5).name(), master.background.name());
}

void ShowDesignTest::masterSuppliesDefaultTypeOnly()
{
    SlideMaster master;
    master.bodyColor = QColor(200, 0, 0);
    master.fontFamily = QStringLiteral("Courier");

    Shape shape = makeBox(QStringLiteral("s"), QRectF(0, 0, 100, 50));
    TextRun defaulted;
    defaulted.text = QStringLiteral("inherits");
    TextRun chosen;
    chosen.text = QStringLiteral("keeps");
    chosen.color = QColor(0, 128, 0);
    chosen.fontFamily = QStringLiteral("Times");
    TextParagraph paragraph;
    paragraph.runs = {defaulted, chosen};
    shape.paragraphs = {paragraph};

    const Shape styled = withMasterDefaults(shape, &master);
    // A run that never chose for itself inherits; a deliberately green word
    // stays green when the master changes.
    QCOMPARE(styled.paragraphs.first().runs.at(0).color.name(), master.bodyColor.name());
    QCOMPARE(styled.paragraphs.first().runs.at(0).fontFamily, QStringLiteral("Courier"));
    QCOMPARE(styled.paragraphs.first().runs.at(1).color.name(), QColor(0, 128, 0).name());
    QCOMPARE(styled.paragraphs.first().runs.at(1).fontFamily, QStringLiteral("Times"));
}

void ShowDesignTest::buildAnimationChangesWhatIsDrawn()
{
    Presentation deck;
    Slide &slide = deck.addSlide(QStringLiteral("One"));
    Shape box = makeBox(QStringLiteral("s"), QRectF(0, 0, deck.slideSize.width(), deck.slideSize.height()));
    box.buildStep = 1;
    box.animation = ShapeAnimation::Fade;
    slide.shapes.append(box);

    // Before its step the shape is not drawn at all.
    QCOMPARE(pixelAt(slide, deck.slideSize, nullptr, 100, 60, 0).name(), QColor(Qt::white).name());
    // At its step, half-faded is neither the background nor the final fill.
    const QColor half = pixelAt(slide, deck.slideSize, nullptr, 100, 60, 1, 0.5);
    const QColor done = pixelAt(slide, deck.slideSize, nullptr, 100, 60, 1, 1.0);
    QCOMPARE(done.name(), QColor(Qt::blue).name());
    QVERIFY2(half.name() != done.name(), qPrintable(half.name()));
    QVERIFY2(half.name() != QColor(Qt::white).name(), qPrintable(half.name()));
}

void ShowDesignTest::snapPullsAnEdgeToTheSlideCentre()
{
    Scene scene(QSize(900, 600));
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    SlideCanvasItem *canvas = scene.canvas;
    const QString id = canvas->addShape(ShapeKind::Rectangle, QRectF(10, 10, 100, 50));
    QVERIFY(!id.isEmpty());
    const qreal centre = canvas->slideSize().width() / 2.0;

    // Three points shy of the slide's vertical centre: inside the threshold.
    const QRectF proposed(centre - 3, 200, 100, 50);
    QCOMPARE(qRound(canvas->snapRect(id, proposed).left()), qRound(centre));

    // Far away: left exactly where it was.
    const QRectF far(centre - 60, 200, 100, 50);
    QCOMPARE(qRound(canvas->snapRect(id, far).left()), qRound(centre - 60));

    canvas->setSnapEnabled(false);
    QCOMPARE(qRound(canvas->snapRect(id, proposed).left()), qRound(centre - 3));
    // The View menu's toggle is the same state.
    QVERIFY(!scene.host.action(sid(ActionId::kShowSnapToGuides))->isChecked());
    scene.host.action(sid(ActionId::kShowSnapToGuides))->setChecked(true);
    QVERIFY(canvas->snapEnabled());
}

void ShowDesignTest::groupingSelectsAndTravelsThroughFodp()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    SlideCanvasItem *canvas = scene.canvas;
    const QString a = canvas->addShape(ShapeKind::Rectangle, QRectF(0, 0, 40, 40));
    const QString b = canvas->addShape(ShapeKind::Ellipse, QRectF(60, 0, 40, 40));
    canvas->setSelected(a, true);
    canvas->setSelected(b, true);

    QVERIFY(scene.host.actions()->trigger(sid(ActionId::kShowGroup)));

    const Shape *first = canvas->slide()->findShape(a);
    QVERIFY(first != nullptr);
    QVERIFY(!first->groupId.isEmpty());
    QCOMPARE(canvas->slide()->shapeIdsInGroup(first->groupId).size(), 2);
    // An empty group id matches nothing: an ungrouped shape is not "in the
    // group of all ungrouped shapes".
    QVERIFY(canvas->slide()->shapeIdsInGroup(QString()).isEmpty());
    QVERIFY(canvas->selectionIsGrouped());

    // The group must survive the native format, or reopening scatters it.
    QString error;
    const QByteArray fodp = writeFodp(*scene.doc->presentation(), &error);
    QVERIFY2(!fodp.isEmpty(), qPrintable(error));
    const Presentation back = readFodp(fodp, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.slides.size(), 1);
    QString groupA;
    QString groupB;
    for (const Shape &shape : back.slides.first().shapes) {
        if (shape.id == a) {
            groupA = shape.groupId;
        } else if (shape.id == b) {
            groupB = shape.groupId;
        }
    }
    QVERIFY2(!groupA.isEmpty(), "the group id did not survive the fodp round trip");
    QCOMPARE(groupA, groupB);

    QVERIFY(scene.host.actions()->trigger(sid(ActionId::kShowUngroup)));
    QVERIFY(canvas->slide()->findShape(a)->groupId.isEmpty());
}

void ShowDesignTest::multiShapeGestureIsOneUndoStep()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    SlideCanvasItem *canvas = scene.canvas;
    const QString a = canvas->addShape(ShapeKind::Rectangle, QRectF(0, 0, 40, 40));
    const QString b = canvas->addShape(ShapeKind::Ellipse, QRectF(60, 0, 40, 40));
    canvas->setSelected(a, true);
    canvas->setSelected(b, true);
    const int before = scene.doc->undoStack()->count();

    // The bracket a multi-shape drag emits: everything between becomes one
    // command instead of one per shape.
    emit canvas->compoundEditStarted();
    QHash<QString, QRectF> moved;
    moved.insert(a, QRectF(20, 20, 40, 40));
    moved.insert(b, QRectF(80, 20, 40, 40));
    canvas->setShapeRects(moved);
    emit canvas->compoundEditFinished();

    QCOMPARE(scene.doc->undoStack()->count(), before + 1);
    scene.doc->undoStack()->undo();
    QCOMPARE(qRound(canvas->slide()->findShape(a)->rect.left()), 0);
    QCOMPARE(qRound(canvas->slide()->findShape(b)->rect.left()), 60);
}

void ShowDesignTest::mastersRoundTripThroughTheDialogSeam()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    QVERIFY(scene.host.masters().isEmpty());
    // The QML editor hands the whole list back; one undo step for the deck.
    QVariantList masters;
    masters.append(QVariantMap{{QStringLiteral("id"), QString()},
                               {QStringLiteral("name"), QStringLiteral("Dusk")},
                               {QStringLiteral("background"), QColor(20, 40, 80)},
                               {QStringLiteral("titleColor"), QColor(Qt::white)},
                               {QStringLiteral("bodyColor"), QColor(220, 220, 220)},
                               {QStringLiteral("fontFamily"), QStringLiteral("Courier")}});
    const int before = scene.doc->undoStack()->count();
    scene.host.applyMasters(masters);
    QCOMPARE(scene.doc->presentation()->masters.size(), 1);
    QVERIFY(!scene.doc->presentation()->masters.first().id.isEmpty()); // generated
    QCOMPARE(scene.host.masterNames(), QStringList{QStringLiteral("Dusk")});
    QCOMPARE(scene.doc->undoStack()->count(), before + 1);

    // Applying it to the slide is a slide edit, and the canvas repaints with it.
    scene.host.applyMasterToSlide(0);
    QCOMPARE(scene.canvas->slide()->master, scene.doc->presentation()->masters.first().id);
    QVERIFY(scene.canvas->master() != nullptr);
    QCOMPARE(scene.canvas->master()->background.name(), QColor(20, 40, 80).name());
    scene.doc->undoStack()->undo();
    QVERIFY(scene.canvas->slide()->master.isEmpty());

    // "None" puts the slide back on plain white.
    scene.host.applyMasterToSlide(0);
    scene.host.applyMasterToSlide(-1);
    QVERIFY(scene.canvas->slide()->master.isEmpty());
}

void ShowDesignTest::animationEntriesFollowTheSelection()
{
    Scene scene;
    QVERIFY2(scene.ready(), qPrintable(scene.error));
    SlideCanvasItem *canvas = scene.canvas;
    const QString id = canvas->addShape(ShapeKind::Rectangle, QRectF(0, 0, 40, 40));
    QVERIFY(scene.host.actions()->trigger(QStringLiteral("showAnimFade")));
    const Shape *shape = canvas->slide()->findShape(id);
    QCOMPARE(shape->animation, ShapeAnimation::Fade);
    // An animation only plays when the shape ARRIVES, so a shape that is on
    // the slide from the start gets a build step too.
    QCOMPARE(shape->buildStep, 1);
    QVERIFY(scene.host.action(QStringLiteral("showAnimFade"))->isChecked());
    QVERIFY(!scene.host.action(QStringLiteral("showAnimAppear"))->isChecked());
    // Nothing selected: the entries are inert.
    canvas->clearSelection();
    QVERIFY(!scene.host.action(QStringLiteral("showAnimFade"))->isEnabled());
}

SHOW_TEST_MAIN(ShowDesignTest)

#include "tst_show_design.moc"
