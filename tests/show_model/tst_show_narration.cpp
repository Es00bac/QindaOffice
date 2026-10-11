// SPDX-License-Identifier: GPL-3.0-or-later
// Standard p:audio narration and the Office 2013 shape marker, plus the
// action-button symbols that must remain visible in every render target.
#include "pptx_show_support.h"
#include "preset_geometry.h"
#include <QDir>
using namespace PptxShowSupport;
class ShowNarrationTest final : public QObject {
    Q_OBJECT
private slots:
    void readsPowerPointAudioNarration()
    {
        const QByteArray bytes = PptxFixture::edited([](Ooxml::PackageWriter &writer) {
            PptxFixture::addVideo(writer);
            const QString part = QStringLiteral("ppt/slides/slide3.xml");
            QByteArray xml = writer.partData(part);
            xml.replace("p:video", "p:audio");
            xml.replace("a:videoFile", "a:audioFile");
            xml.replace("<p:audio>", "<p:audio isNarration=\"1\">");
            writer.addPart(part, xml, PptxFixture::M91::kSlideType);
        });
        const auto deck = readPptxData(bytes);
        bool found = false;
        for (const auto &shape : deck.slides[2].shapes) {
            if (shape.media.isMedia()) {
                found = true;
                QVERIFY(shape.media.narration);
            }
        }
        QVERIFY(found);
    }
    void narrationRoundTripsThroughBothMarkers()
    {
        Presentation deck;
        auto &slide = deck.addSlide(QStringLiteral("Narration"));
        Shape sound;
        sound.id = QStringLiteral("voice");
        sound.kind = ShapeKind::Image;
        sound.rect = QRectF(10, 10, 80, 80);
        sound.media.kind = MediaKind::Audio;
        sound.media.data = builtInSound(QStringLiteral("Beep"));
        sound.media.mime = QStringLiteral("audio/wav");
        sound.media.name = QStringLiteral("voice.wav");
        sound.media.autoPlay = true;
        sound.media.narration = true;
        slide.shapes.append(sound);
        QString error;
        const QByteArray zip = writePptx(deck, &error);
        QVERIFY2(!zip.isEmpty(), qPrintable(error));
        const auto issues = problems(zip);
        QVERIFY2(issues.isEmpty(), qPrintable(issues.join(QLatin1Char('\n'))));
        const QByteArray xml = part(zip, QStringLiteral("ppt/slides/slide1.xml"));
        QVERIFY(xml.contains("isNarration=\"1\""));
        QVERIFY(xml.contains("42D2F446-02D8-4167-A562-619A0277C38B"));
        auto back = readPptxData(zip, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(back.slides[0].shapes[0].media.narration);
        back.slides[0].shapes[0].media.narration = false;
        const auto cleared = writePptx(back, &error);
        QVERIFY2(!cleared.isEmpty(), qPrintable(error));
        const auto roundTrip = readPptxData(cleared, &error);
        QVERIFY(!roundTrip.slides[0].shapes[0].media.narration);
        const auto clearedIssues = problems(cleared);
        QVERIFY2(clearedIssues.isEmpty(), qPrintable(clearedIssues.join(QLatin1Char('\n'))));
    }
    void everyActionButtonHasItsSymbol()
    {
        Presentation gallery;
        gallery.slideSize = QSizeF(660, 360);
        auto &slide = gallery.addSlide(QStringLiteral("Action buttons"));
        int index = 0;
        for (const auto &preset : actionButtonPresets()) {
            Shape shape;
            shape.id = preset.geometry;
            shape.kind = ShapeKind::AutoShape;
            shape.geometry = preset.geometry;
            shape.rect = QRectF(10 + (index % 4) * 160, 10 + (index / 4) * 115, 140, 95);
            shape.fill = Qt::white;
            shape.stroke = Qt::black;
            shape.strokeWidth = 2;
            slide.shapes.append(shape);
            ++index;
            const auto outline = Geometry::presetOutline(preset.geometry, QRectF(0, 0, 100, 100));
            QVERIFY2(!outline.isEmpty(), qPrintable(preset.geometry));
            if (preset.geometry != u"actionButtonBlank") {
                QVERIFY2(outline.stroke.elementCount() > outline.fill.elementCount(), qPrintable(preset.geometry));
            }
        }
        const QString dir = qEnvironmentVariable("QQO_GRAB_DIR");
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            QVERIFY(renderSlideToImage(gallery, slide, QSize(1320, 720)).save(dir + QStringLiteral("/qindashow_m91_action_buttons.png")));
        }
    }
};
int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    ShowNarrationTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_show_narration.moc"
