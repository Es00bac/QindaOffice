// SPDX-License-Identifier: GPL-3.0-or-later
// More of PowerPoint's common effects (M67, D-076): the Wipe and Split
// entrances, Spin and Grow/Shrink emphasis, and the Wipe and Split slide
// transitions — read from the presets PowerPoint writes (presetID /
// presetClass / presetSubtype, the animEffect filters, animRot and
// animScale, p:wipe / p:split), written back the same way so PowerPoint
// plays them, kept verbatim while unchanged, valid against the schemas,
// carried through .fodp, and drawn by the renderer: a wipe reveals from
// the bottom, a split from the centre, an emphasis shape is on the slide
// before its step and a grown one stays grown in the show (but not on the
// editor's or the PDF's finished slide).

#include "fodp_reader.h"
#include "fodp_writer.h"
#include "pptx_fixture_slides.h"
#include "pptx_reader.h"
#include "pptx_writer.h"
#include "presentation.h"
#include "slide_renderer.h"

#include "ooxml_package.h"
#include "ooxml_xml.h"
#include "support/ooxml_schema.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QtTest>

using namespace QindaOffice::Show;
namespace Ooxml = QindaOffice::Ooxml;

namespace {

Shape box(const QString &id, quint32 nvId, const QRectF &rect, ShapeAnimation animation, int step)
{
    Shape shape;
    shape.id = id;
    shape.nvId = nvId;
    shape.kind = ShapeKind::Rectangle;
    shape.rect = rect;
    shape.fill = QColor(200, 0, 0);
    shape.strokeWidth = 0;
    shape.animation = animation;
    shape.buildStep = step;
    shape.animationMs = isEmphasis(animation) ? 2000 : 500;
    return shape;
}

Presentation deck()
{
    Presentation deck;
    Slide &one = deck.addSlide(QStringLiteral("Effects"));
    one.transition = QStringLiteral("wipe");
    one.shapes = {box(QStringLiteral("w"), 2, QRectF(40, 40, 200, 120), ShapeAnimation::Wipe, 1),
                  box(QStringLiteral("s"), 3, QRectF(300, 40, 200, 120), ShapeAnimation::Split, 2),
                  box(QStringLiteral("r"), 4, QRectF(40, 220, 200, 120), ShapeAnimation::Spin, 3),
                  box(QStringLiteral("g"), 5, QRectF(300, 220, 200, 120), ShapeAnimation::GrowShrink, 4)};
    Slide &two = deck.addSlide(QStringLiteral("Split"));
    two.transition = QStringLiteral("split");
    return deck;
}

QColor pixel(const Slide &slide, const QPointF &at, int step, qreal progress = 1.0)
{
    QImage image(720, 405, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    renderSlide(&painter, slide, QSizeF(720, 405), SlideBackdrop(), step, progress);
    painter.end();
    return QColor(image.pixel(at.toPoint()));
}

QString partXml(const QByteArray &zip, const QString &part)
{
    const auto package = Ooxml::Package::fromData(zip);
    return package ? QString::fromUtf8(package->data(part)) : QString();
}

} // namespace

class ShowEffectsTest : public QObject {
    Q_OBJECT
private slots:
    void writtenWithPowerPointPresets()
    {
        const Presentation source = deck();
        QString error;
        const QByteArray zip = writePptx(source, &error);
        QVERIFY2(!zip.isEmpty(), qPrintable(error));
        const QString xml = partXml(zip, QStringLiteral("ppt/slides/slide1.xml"));
        QVERIFY(xml.contains(QStringLiteral("presetID=\"22\" presetClass=\"entr\" presetSubtype=\"4\"")));
        QVERIFY(xml.contains(QStringLiteral("filter=\"wipe(up)\"")));
        QVERIFY(xml.contains(QStringLiteral("presetID=\"16\" presetClass=\"entr\" presetSubtype=\"37\"")));
        QVERIFY(xml.contains(QStringLiteral("filter=\"barn(outVertical)\"")));
        QVERIFY(xml.contains(QStringLiteral("presetID=\"8\" presetClass=\"emph\"")));
        QVERIFY(xml.contains(QStringLiteral("<p:animRot by=\"21600000\">")));
        QVERIFY(xml.contains(QStringLiteral("presetID=\"6\" presetClass=\"emph\"")));
        QVERIFY(xml.contains(QStringLiteral("<p:by x=\"150000\" y=\"150000\"/>")));
        QVERIFY(xml.contains(QStringLiteral("<p:wipe dir=\"l\"/>")));
        QVERIFY(partXml(zip, QStringLiteral("ppt/slides/slide2.xml")).contains(QStringLiteral("<p:split orient=\"vert\" dir=\"out\"/>")));
        if (QindaOffice::Test::schemaValidationAvailable()) {
            const QStringList errors = QindaOffice::Test::validatePackage(*Ooxml::Package::fromData(zip));
            QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
        }
        // Read back: the same effects, steps, durations and transitions.
        const Presentation back = readPptxData(zip, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(back.slides.at(0).transition, QStringLiteral("wipe"));
        QCOMPARE(back.slides.at(1).transition, QStringLiteral("split"));
        for (qsizetype i = 0; i < source.slides.at(0).shapes.size(); ++i) {
            const Shape &a = source.slides.at(0).shapes.at(i);
            const Shape &b = back.slides.at(0).shapes.at(i);
            QCOMPARE(b.animation, a.animation);
            QCOMPARE(b.buildStep, a.buildStep);
            QCOMPARE(b.animationMs, a.animationMs);
        }
        // Unchanged, the timing and the transition are written verbatim.
        const QByteArray again = writePptx(back);
        QCOMPARE(partXml(again, QStringLiteral("ppt/slides/slide1.xml")).section(QStringLiteral("<p:timing>"), 1),
                 xml.section(QStringLiteral("<p:timing>"), 1));
    }

    void travelsThroughFodp()
    {
        const Presentation source = deck();
        const Presentation back = readFodp(writeFodp(source));
        QCOMPARE(back.slides.at(0).transition, QStringLiteral("wipe"));
        QCOMPARE(back.slides.at(1).transition, QStringLiteral("split"));
        for (qsizetype i = 0; i < source.slides.at(0).shapes.size(); ++i) {
            QCOMPARE(back.slides.at(0).shapes.at(i).animation, source.slides.at(0).shapes.at(i).animation);
        }
    }

    void drawnAsTheyPlay()
    {
        const Slide slide = deck().slides.first();
        const QColor red(200, 0, 0);
        // Before any click: the entrances are not there, the emphasis shapes are.
        QCOMPARE(pixel(slide, QPointF(140, 100), 0), QColor(Qt::white));
        QCOMPARE(pixel(slide, QPointF(140, 280), 0), red);
        QCOMPARE(pixel(slide, QPointF(400, 280), 0), red);
        // Half-way through the wipe the bottom shows, the top not yet.
        QCOMPARE(pixel(slide, QPointF(140, 150), 1, 0.5), red);
        QCOMPARE(pixel(slide, QPointF(140, 45), 1, 0.5), QColor(Qt::white));
        // Half-way through the split the centre shows, the edges not yet.
        QCOMPARE(pixel(slide, QPointF(400, 100), 2, 0.5), red);
        QCOMPARE(pixel(slide, QPointF(305, 100), 2, 0.5), QColor(Qt::white));
        // A spin ends where it began.
        QCOMPARE(pixel(slide, QPointF(45, 225), 3, 1.0), red);
        // In the show a grown shape stays grown; the finished slide (the
        // editor, thumbnails, print) shows it as designed.
        const QPointF outside(295, 210);
        QCOMPARE(pixel(slide, outside, 5), red);
        QCOMPARE(pixel(slide, outside, 1000), QColor(Qt::white));
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    ShowEffectsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_show_effects.moc"
