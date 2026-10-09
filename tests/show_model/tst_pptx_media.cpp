// SPDX-License-Identifier: GPL-3.0-or-later
// M75 (D-084) audio and video gates: the containers tell the kind and the
// duration; a video PowerPoint embedded (poster, media and video
// relationships, p14:media, Start Automatically, Loop until Stopped, Hide
// While Not Playing) is read as a picture that plays, carried verbatim
// while unchanged and its timing rewritten when its playback changes;
// video and sound inserted in QindaShow are written as PowerPoint embeds
// them; LibreOffice converts a deck with a new slide size, a background, a
// theme, sections, comments and a video.

#include "pptx_design.h"
#include "pptx_fixture_m75.h"
#include "pptx_origin.h"
#include "pptx_reader.h"
#include "pptx_rules.h"
#include "pptx_soffice.h"
#include "pptx_writer.h"
#include "sections.h"
#include "slide_renderer.h"

#include "support/ooxml_schema.h"

#include <QDir>
#include <QGuiApplication>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Show;
namespace Ooxml = QindaOffice::Ooxml;

class PptxMediaTest : public QObject {
    Q_OBJECT
private slots:
    void probeKnowsTheContainers();
    void powerPointVideoIsRead();
    void unchangedVideoWrittenVerbatim();
    void playbackChangesRewriteTheTiming();
    void insertedMediaWrittenAsPowerPointEmbeds();
    void postersAndTheShow();
    void libreOfficeConvertsTheDeck();
};

namespace {

QByteArray part(const QByteArray &zip, const QString &name)
{
    const auto package = Ooxml::Package::fromData(zip);
    return package ? package->data(name) : QByteArray();
}

QByteArray videoFixture()
{
    return PptxFixture::edited([](Ooxml::PackageWriter &w) { PptxFixture::addVideo(w); });
}

void checkRules(const QByteArray &zip)
{
    const QStringList rules = PptxFixture::ruleViolations(zip);
    QVERIFY2(rules.isEmpty(), qPrintable(rules.join(QLatin1Char('\n'))));
    if (QindaOffice::Test::schemaValidationAvailable()) {
        const QStringList errors = QindaOffice::Test::validatePackage(*Ooxml::Package::fromData(zip));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(QLatin1Char('\n'))));
    }
}

Shape mediaShape(MediaKind kind, const QByteArray &data, const QString &name, const QRectF &rect)
{
    Shape shape;
    shape.id = name;
    shape.kind = ShapeKind::Image;
    shape.rect = rect;
    shape.strokeWidth = 0;
    shape.media.kind = kind;
    shape.media.data = data;
    shape.media.name = name;
    const MediaProbe probe = probeMedia(data, name);
    shape.media.mime = probe.mime;
    shape.media.durationMs = probe.durationMs;
    shape.imagePng = mediaPoster(kind, name, kind == MediaKind::Video ? QSize(320, 180) : QSize(96, 96));
    shape.image.mime = QStringLiteral("image/png");
    return shape;
}

} // namespace

void PptxMediaTest::probeKnowsTheContainers()
{
    const MediaProbe mp4 = probeMedia(PptxFixture::tinyMp4(4321));
    QCOMPARE(mp4.kind, MediaKind::Video);
    QCOMPARE(mp4.mime, QStringLiteral("video/mp4"));
    QCOMPARE(mp4.extension, QStringLiteral("mp4"));
    QCOMPARE(mp4.durationMs, 4321);
    const MediaProbe m4a = probeMedia(PptxFixture::tinyMp4(1500, false));
    QCOMPARE(m4a.kind, MediaKind::Audio);
    QCOMPARE(m4a.mime, QStringLiteral("audio/mp4"));
    QCOMPARE(m4a.durationMs, 1500);
    const MediaProbe wav = probeMedia(PptxFixture::tinyWav(2000));
    QCOMPARE(wav.kind, MediaKind::Audio);
    QCOMPARE(wav.mime, QStringLiteral("audio/wav"));
    QCOMPARE(wav.durationMs, 2000);
    // Unknown bytes: the name decides.
    const MediaProbe named = probeMedia(QByteArray("\x01\x02\x03\x04"), QStringLiteral("clip.mov"));
    QCOMPARE(named.kind, MediaKind::Video);
    QCOMPARE(named.mime, QStringLiteral("video/quicktime"));
    QCOMPARE(probeMedia(QByteArray("plain text")).kind, MediaKind::None);
    QCOMPARE(mediaMimeForExtension(QStringLiteral("MP3")), QStringLiteral("audio/mpeg"));
    QCOMPARE(mediaExtensionForMime(QStringLiteral("video/x-ms-wmv")), QStringLiteral("wmv"));
}

void PptxMediaTest::powerPointVideoIsRead()
{
    const Presentation deck = readPptxData(videoFixture());
    const Slide &three = deck.slides.at(2);
    QCOMPARE(three.shapes.size(), 3);
    const Shape &video = three.shapes.last();
    QCOMPARE(video.kind, ShapeKind::Image);
    QCOMPARE(video.media.kind, MediaKind::Video);
    QCOMPARE(video.media.data, PptxFixture::tinyMp4(4321));
    QCOMPARE(video.media.mime, QStringLiteral("video/mp4"));
    QCOMPARE(video.media.name, QStringLiteral("media1.mp4"));
    QCOMPARE(video.media.durationMs, 4321);
    QVERIFY(video.media.autoPlay);
    QVERIFY(video.media.loop);
    QVERIFY(video.media.hideWhenStopped);
    QCOMPARE(video.media.volume, 80);
    QCOMPARE(video.buildStep, 0);
    QVERIFY(!video.imagePng.isEmpty()); // the poster frame
    QCOMPARE(video.origin->pristine, video);
    QCOMPARE(three.origin->media.size(), 1);
}

void PptxMediaTest::unchangedVideoWrittenVerbatim()
{
    const QByteArray source = videoFixture();
    const Presentation deck = readPptxData(source);
    const QByteArray zip = writePptx(deck);
    checkRules(zip);
    const QByteArray slide = part(zip, QStringLiteral("ppt/slides/slide3.xml"));
    QVERIFY(slide.contains(R"(<p:cMediaNode vol="80000" showWhenStopped="0"><p:cTn id="7" repeatCount="indefinite")"));
    QVERIFY(slide.contains("playFrom(0.0)"));
    QCOMPARE(part(zip, QStringLiteral("ppt/media/media1.mp4")), PptxFixture::tinyMp4(4321));
    QCOMPARE(Ooxml::Package::fromData(zip)->contentType(QStringLiteral("ppt/media/media1.mp4")), QStringLiteral("video/mp4"));
    QCOMPARE(readPptxData(zip).slides.at(2).shapes, deck.slides.at(2).shapes);
}

void PptxMediaTest::playbackChangesRewriteTheTiming()
{
    Presentation deck = readPptxData(videoFixture());
    Shape &video = deck.slides[2].shapes.last();
    video.media.autoPlay = false; // Start: On Click
    video.buildStep = 1;
    video.media.loop = false;
    const QByteArray zip = writePptx(deck);
    checkRules(zip);
    const QByteArray slide = part(zip, QStringLiteral("ppt/slides/slide3.xml"));
    QVERIFY(slide.contains(R"(presetClass="mediacall" presetSubtype="0" fill="hold" nodeType="clickEffect")"));
    QVERIFY(slide.contains(R"x(<p:cmd type="call" cmd="playFrom(0.0)"><p:cBhvr><p:cTn id="6" dur="4321" fill="hold"/>)x"));
    QVERIFY(slide.contains(R"(<p:cMediaNode vol="80000" showWhenStopped="0">)"));
    QVERIFY(!slide.contains("repeatCount"));
    QVERIFY(!slide.contains("onBegin"));
    const Shape back = readPptxData(zip).slides.at(2).shapes.last();
    QVERIFY(!back.media.autoPlay);
    QVERIFY(!back.media.loop);
    QVERIFY(back.media.hideWhenStopped);
    QCOMPARE(back.buildStep, 1);
}

void PptxMediaTest::insertedMediaWrittenAsPowerPointEmbeds()
{
    // A video and a sound inserted in QindaShow, on a PowerPoint deck.
    Presentation deck = readPptxData(PptxFixture::build());
    Shape video = mediaShape(MediaKind::Video, PptxFixture::tinyMp4(2500), QStringLiteral("talk.mp4"), QRectF(500, 300, 320, 180));
    video.media.autoPlay = true;
    Shape sound = mediaShape(MediaKind::Audio, PptxFixture::tinyWav(1200), QStringLiteral("chime.wav"), QRectF(60, 440, 48, 48));
    sound.buildStep = 1;
    sound.media.volume = 50;
    deck.slides[1].shapes.append(video);
    deck.slides[1].shapes.append(sound);
    const QByteArray zip = writePptx(deck);
    checkRules(zip);
    const auto package = Ooxml::Package::fromData(zip);
    const QByteArray slide = package->data(QStringLiteral("ppt/slides/slide2.xml"));
    // (not a raw string: moc takes the // in one for a comment)
    QVERIFY(slide.contains("<a:hlinkClick r:id=\"\" action=\"ppaction:/" "/media\"/>"));
    QVERIFY(slide.contains("<a:videoFile r:link="));
    QVERIFY(slide.contains("<a:audioFile r:link="));
    QVERIFY(slide.contains(R"(<p:ext uri="{DAA4B4D4-6D71-4841-9C94-3DA2E9CE8D0B}"><p14:media)"));
    QVERIFY(slide.contains(R"(<p:audio><p:cMediaNode vol="50000">)"));
    QVERIFY(slide.contains(R"(<p:video><p:cMediaNode vol="80000">)"));
    QVERIFY(slide.contains(R"(nodeType="afterEffect")"));
    QVERIFY(slide.contains(R"(<p:cond evt="onBegin" delay="0"><p:tn val="2"/></p:cond>)"));
    // The video and media relationships name one part.
    QString videoTarget;
    QString mediaTarget;
    for (const Ooxml::Relationship &rel : package->relationships(QStringLiteral("ppt/slides/slide2.xml"))) {
        if (rel.type.endsWith(QLatin1String("/relationships/video"))) {
            videoTarget = package->targetPart(QStringLiteral("ppt/slides/slide2.xml"), rel);
        } else if (rel.type == u"http://schemas.microsoft.com/office/2007/relationships/media"
                   && package->contentType(package->targetPart(QStringLiteral("ppt/slides/slide2.xml"), rel)).startsWith(u"video/")) {
            mediaTarget = package->targetPart(QStringLiteral("ppt/slides/slide2.xml"), rel);
        }
    }
    QVERIFY(!videoTarget.isEmpty());
    QCOMPARE(videoTarget, mediaTarget);
    QCOMPARE(package->contentType(videoTarget), QStringLiteral("video/mp4"));
    QVERIFY(videoTarget.startsWith(QLatin1String("ppt/media/media")));
    const Presentation back = readPptxData(zip);
    const Slide &two = back.slides.at(1);
    QCOMPARE(two.shapes.size(), 3);
    QCOMPARE(two.shapes.at(1).media.data, video.media.data);
    QVERIFY(two.shapes.at(1).media.autoPlay);
    QCOMPARE(two.shapes.at(1).imagePng, video.imagePng);
    QCOMPARE(two.shapes.at(2).media.kind, MediaKind::Audio);
    QCOMPARE(two.shapes.at(2).media.volume, 50);
    QCOMPARE(two.shapes.at(2).buildStep, 1);
    QCOMPARE(two.shapes.at(2).media.durationMs, 1200);
    // On a deck QindaShow made, too.
    Presentation mine;
    Slide &first = mine.addSlide(QStringLiteral("Media"));
    first.shapes.append(mediaShape(MediaKind::Video, PptxFixture::tinyMp4(900), QStringLiteral("a.mp4"), QRectF(100, 100, 320, 180)));
    checkRules(writePptx(mine));
}

void PptxMediaTest::postersAndTheShow()
{
    // A new video's poster is QindaShow's (a play mark), a sound's a speaker.
    QImage poster;
    QVERIFY(poster.loadFromData(mediaPoster(MediaKind::Video, QStringLiteral("talk.mp4"), QSize(320, 180))));
    QCOMPARE(poster.size(), QSize(320, 180));
    QVERIFY(poster.pixelColor(4, 4).value() < 60);
    // In the editor the poster shows; in the show a video that hides while
    // not playing is the player's to draw.
    const Presentation deck = readPptxData(videoFixture());
    const Slide &three = deck.slides.at(2);
    const QRectF box = three.shapes.last().rect;
    const QPoint at(int(box.center().x()), int(box.center().y()));
    QImage editor(QSize(960, 540), QImage::Format_ARGB32_Premultiplied);
    QImage show(QSize(960, 540), QImage::Format_ARGB32_Premultiplied);
    for (QImage *image : {&editor, &show}) {
        image->fill(Qt::white);
        QPainter painter(image);
        renderSlide(&painter, deck, three, image == &editor ? 1000 : 0);
    }
    QCOMPARE(editor.pixelColor(at), QColor(20, 20, 20));
    QVERIFY(show.pixelColor(at) != QColor(20, 20, 20));
}

void PptxMediaTest::libreOfficeConvertsTheDeck()
{
    if (!PptxFixture::sofficeAvailable()) {
        QSKIP("LibreOffice unavailable in this environment");
    }
    // A new slide size, a background, a theme, sections, comments, a video.
    Presentation deck = readPptxData(PptxFixture::edited([](Ooxml::PackageWriter &w) {
        PptxFixture::addModernComments(w);
        PptxFixture::addVideo(w);
    }));
    deck = resized(deck, QSizeF(720, 540), SlideScale::EnsureFit);
    deck = themed(deck, builtInThemes().at(1), ThemePart::Whole);
    Background background;
    background.set = true;
    background.color = QColor(0xEE, 0xF4, 0xFA);
    deck = withBackground(deck, deck.slides.at(0).id, background, true);
    QCOMPARE(addSection(deck, deck.slides.at(2).id, QStringLiteral("Media")), 1);
    const QByteArray zip = writePptx(deck);
    checkRules(zip);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QString error;
    const QByteArray pdf = PptxFixture::convertWithSoffice(dir, QStringLiteral("m75.pptx"), zip, QStringLiteral("pdf"), &error);
    QVERIFY2(!pdf.isEmpty(), qPrintable(error));
    QCOMPARE(PptxFixture::pdfPages(pdf), 4); // the hidden slide left out
    if (const QString grabs = qEnvironmentVariable("QQO_GRAB_DIR"); !grabs.isEmpty()) {
        QDir().mkpath(grabs);
        QFile out(QDir(grabs).filePath(QStringLiteral("pptx_m75_libreoffice.pdf")));
        QVERIFY(out.open(QIODevice::WriteOnly) && out.write(pdf) == pdf.size());
        QFile deckFile(QDir(grabs).filePath(QStringLiteral("pptx_m75.pptx")));
        QVERIFY(deckFile.open(QIODevice::WriteOnly) && deckFile.write(zip) == zip.size());
    }
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    PptxMediaTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pptx_media.moc"
