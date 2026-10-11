// SPDX-License-Identifier: GPL-3.0-or-later
#include "show_test_support.h"
#include "equation_session.h"
#include "math_linear.h"
#include "math_omml.h"
#include "pptx_reader.h"
#include "pptx_writer.h"
#include "fodp_reader.h"
#include "fodp_writer.h"
#include "shape_json.h"
#include "show_equation.h"
#include "ooxml_package.h"
#include "ooxml_namespaces.h"
#include "support/ooxml_schema.h"
#include "support/quick_dump.h"
#include "support/ribbon_gate.h"
#include "qindashow/src/pdf_export.h"
#include <QUndoStack>
using namespace QindaOffice;
using ShowTest::Scene;
namespace {
Show::Shape formula()
{
    Show::Shape shape; shape.id = QStringLiteral("formula"); shape.kind = Show::ShapeKind::Equation;
    shape.equationXml = Math::writeOmmlFragment(*Math::fromLinear(QStringLiteral("x^2+(a)/(b)")));
    shape.rect = QRectF(100, 130, 360, 150); shape.strokeWidth = 0; return shape;
}
QObject *popup(Scene &scene) { return scene.window->findChild<QObject *>(QStringLiteral("equationDialog")); }
void seed(Scene &scene)
{
    Show::Presentation deck; deck.addSlide(QStringLiteral("Equations")).shapes.append(formula());
    scene.host.applyDeckChange(deck, QStringLiteral("Synthetic equations"));
    scene.host.selectSlideRow(0); scene.canvas->setSelected(QStringLiteral("formula"), true);
    scene.doc->undoStack()->clear();
}
}
class ShowEquationsTest : public QObject {
    Q_OBJECT
private slots:
    void unreadableEquationReportsAnErrorWithoutChangingDeck()
    {
        Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error)); seed(scene);
        scene.host.currentSlide()->shapes[0].equationXml = QStringLiteral("<broken");
        QSignalSpy messages(&scene.host, &Quick::DocumentHost::messageRequested);
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showEditEquationAction")));
        QCOMPARE(messages.count(), 1); QVERIFY(!messages.first().at(1).toString().isEmpty());
        QVERIFY(!scene.host.equations()->active());
        QCOMPARE(scene.doc->undoStack()->count(), 0);
        QCOMPARE(scene.host.currentSlide()->shapes.first().equationXml, QStringLiteral("<broken"));
    }
    void insertEditUndoCancel()
    {
        Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error)); seed(scene);
        auto *draft = scene.host.equations(); const QString before = scene.host.currentSlide()->shapes.first().equationXml;
        QVERIFY(scene.host.actions()->trigger(QStringLiteral("showEditEquationAction")));
        QVERIFY(draft->convert(QStringLiteral("√(x)"), false));
        QVERIFY(QMetaObject::invokeMethod(popup(scene), "reject"));
        QCOMPARE(scene.doc->undoStack()->count(), 0);
        scene.host.actions()->trigger(QStringLiteral("showEditEquationAction"));
        QVERIFY(draft->convert(QStringLiteral("√(x)"), false));
        QVERIFY(QMetaObject::invokeMethod(popup(scene), "accept"));
        QCOMPARE(scene.doc->undoStack()->count(), 1);
        QVERIFY(scene.host.currentSlide()->shapes.first().equationXml != before);
        scene.doc->undoStack()->undo(); QCOMPARE(scene.host.currentSlide()->shapes.first().equationXml, before);
        scene.doc->undoStack()->redo();
        scene.host.actions()->trigger(QStringLiteral("showInsertEquationAction"));
        QVERIFY(draft->convert(QStringLiteral("\\int_0^1 x dx"), true));
        QVERIFY(QMetaObject::invokeMethod(popup(scene), "accept"));
        QCOMPARE(scene.host.currentSlide()->shapes.size(), 2);
        scene.doc->undoStack()->undo(); QCOMPARE(scene.host.currentSlide()->shapes.size(), 1);
        scene.doc->undoStack()->redo(); QCOMPARE(scene.host.currentSlide()->shapes.size(), 2);
    }
    void nativeRoundTripSchemaClipboardAndFodp()
    {
        Show::Presentation deck; deck.addSlide(QStringLiteral("Equations")).shapes.append(formula());
        QString error; const auto bytes = Show::writePptx(deck, &error); QVERIFY2(!bytes.isEmpty(), qPrintable(error));
        const auto package = Ooxml::Package::fromData(bytes); QVERIFY(package);
        const auto slide = package->data(QStringLiteral("ppt/slides/slide1.xml"));
        QVERIFY(slide.contains("oMath")); QVERIFY(slide.contains("AlternateContent")); QVERIFY(slide.contains("Fallback"));
        auto loaded = Show::readPptxData(bytes, &error); QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(loaded.slides.size(), 1); QCOMPARE(loaded.slides.first().shapes.size(), 1);
        QCOMPARE(loaded.slides.first().shapes.first().kind, Show::ShapeKind::Equation);
        QCOMPARE(loaded.slides.first().shapes.first().displayText(), formula().displayText());
        QVERIFY(loaded.slides.first().shapes.first().rect == formula().rect);
        if (Test::schemaValidationAvailable()) {
            const auto errors = Test::validatePackage(*package); QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        }
        const auto copied = Show::shapeFromJson(Show::shapeToJson(formula()));
        QCOMPARE(copied.equationXml, formula().equationXml); QCOMPARE(copied.kind, Show::ShapeKind::Equation);
        const auto odp = Show::readFodp(Show::writeFodp(deck, &error), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error)); QCOMPARE(odp.slides.first().shapes.first().equationXml, formula().equationXml);
        loaded.slides[0].shapes[0].equationXml = Math::writeOmmlFragment(*Math::fromLinear(QStringLiteral("√(y)")));
        const auto edited = Show::readPptxData(Show::writePptx(loaded, &error), &error);
        QCOMPARE(edited.slides.first().shapes.first().displayText(), Math::toLinear(*Math::fromLinear(QStringLiteral("√(y)"))));
    }
    void keyboardDoubleClickAndAllTemplates()
    {
        Scene scene; QVERIFY2(scene.ready(), qPrintable(scene.error)); seed(scene);
        QVERIFY(QTest::qWaitForWindowExposed(scene.window));
        const auto point = scene.canvas->mapToScene(formula().rect.center()).toPoint();
        QTest::mouseDClick(scene.window, Qt::LeftButton, Qt::NoModifier, point);
        QTRY_VERIFY(scene.host.equations()->active());
        QCOMPARE(scene.host.equations()->templateIds().size(), 23);
        for (const auto &id : scene.host.equations()->templateIds()) {
            QVERIFY(scene.host.actions()->entry(id)->isEnabled());
            scene.host.actions()->trigger(id);
            QVERIFY(scene.host.equations()->editor()->canUndo());
            QVERIFY(scene.host.equations()->editor()->undo());
        }
        scene.host.equations()->cancel();
    }
    void responsiveWindowAndEditorGrabs()
    {
        for (int width : {1280, 520}) {
            Scene scene(QSize(width, 800)); QVERIFY2(scene.ready(), qPrintable(scene.error)); seed(scene);
            QVERIFY(QTest::qWaitForWindowExposed(scene.window)); QTest::qWait(200);
            const auto *dock = scene.item(QStringLiteral("showDock")); QVERIFY(dock);
            QObject *canvasHost = dock->property("canvasItem").value<QObject *>(); QVERIFY(canvasHost);
            QVERIFY2(canvasHost->property("width").toReal() >= 300, qPrintable(QString::number(canvasHost->property("width").toReal())));
            const QImage pixels = scene.window->grabWindow();
            QRectF glyphArea = formula().rect.adjusted(5, 5, -formula().rect.width() * 0.45, -5);
            glyphArea = scene.canvas->mapRectToScene(glyphArea);
            const qreal dpr = pixels.devicePixelRatio();
            const QRect bounds(qRound(glyphArea.x() * dpr), qRound(glyphArea.y() * dpr),
                               qRound(glyphArea.width() * dpr), qRound(glyphArea.height() * dpr));
            int ink = 0;
            for (int y = bounds.top(); y < bounds.bottom(); ++y) for (int x = bounds.left(); x < bounds.right(); ++x)
                if (pixels.rect().contains(x, y) && qGray(pixels.pixel(x, y)) < 96) ++ink;
            QVERIFY2(ink > 30, "Canvas must show equation glyphs, not only its fraction rule");
            Test::saveGrab(scene.window, QStringLiteral("show_equation_page_%1").arg(width));
            scene.host.actions()->trigger(QStringLiteral("showEditEquationAction"));
            QTRY_VERIFY(popup(scene)->property("opened").toBool()); QTest::qWait(150);
            Test::saveGrab(scene.window, QStringLiteral("show_equation_editor_%1").arg(width));
            const auto *gallery = scene.item(QStringLiteral("equationGallery")); QVERIFY(gallery);
            for (const auto &id : scene.host.equations()->templateIds()) {
                const auto *button = scene.item(id); QVERIFY(button);
                QVERIFY(button->mapRectToItem(gallery, button->boundingRect()).right() <= gallery->width() + 1);
            }
            QVERIFY(QMetaObject::invokeMethod(popup(scene), "reject"));
            QTemporaryDir dir; QString error;
            QVERIFY2(Apps::Show::exportDeckPdf(*scene.doc->presentation(), dir.filePath(QStringLiteral("equations.pdf")), false, &error), qPrintable(error));
        }
    }
};
SHOW_TEST_MAIN(ShowEquationsTest)
#include "tst_show_equations.moc"
