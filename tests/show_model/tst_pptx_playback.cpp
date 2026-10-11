// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_show_support.h"
using namespace PptxShowSupport;

class PptxPlaybackTest : public QObject {
    Q_OBJECT
private slots:
    void customShowsAndSettingsRead();
    void settingsWrittenWhereTheyChanged();
    void timingsReadAndWritten();
    void mediaEditsReadAndWritten();
    void effectDirectionsAndExitsRoundTrip();
    void directionsAndExitsAreDrawn();
    void playlistFollowsTheSettings();
    void clicksHitActions();
};

void PptxPlaybackTest::customShowsAndSettingsRead()
{
    const Presentation deck = fixture();
    QCOMPARE(deck.customShows.size(), 2);
    QCOMPARE(deck.customShows.at(0).name, QStringLiteral("Short"));
    QCOMPARE(deck.customShows.at(0).number, 1u);
    QCOMPARE(deck.customShows.at(0).slides, (QStringList{deck.slides.at(0).id, deck.slides.at(2).id}));
    QCOMPARE(deck.customShows.at(1).number, 4u);
    QCOMPARE(deck.customShows.at(1).slides, (QStringList{deck.slides.at(3).id, deck.slides.at(1).id}));
    const ShowSettings &s = deck.showSettings;
    QCOMPARE(s.type, ShowType::Kiosk);
    QCOMPARE(s.kioskRestartMs, 60000);
    QCOMPARE(s.range, ShowRange::Slides);
    QCOMPARE(s.from, 2);
    QCOMPARE(s.to, 4);
    QVERIFY(s.loop);
    QVERIFY(s.narration);
    QVERIFY(s.animation);
    QVERIFY(!s.useTimings);
    QCOMPARE(s.penColor, QColor(0x00, 0x00, 0xFF));
    QCOMPARE(s.laserColor, QColor(0x00, 0xB0, 0x50));
    // A deck without presProps has the schema's defaults.
    const Presentation plain = readPptxData(PptxFixture::build());
    QCOMPARE(plain.showSettings, ShowSettings::fileDefaults());
    QVERIFY(plain.customShows.isEmpty());
}

void PptxPlaybackTest::settingsWrittenWhereTheyChanged()
{
    Presentation deck = fixture();
    deck.showSettings.type = ShowType::Speaker;
    deck.showSettings.loop = false;
    deck.showSettings.range = ShowRange::Custom;
    deck.showSettings.customShow = 4;
    deck.showSettings.useTimings = true;
    deck.showSettings.penColor = QColor(0xC0, 0x00, 0x00);
    // A slide gone from the deck leaves the custom shows that held it.
    deck.slides.removeAt(2);
    deck.customShows[0].slides.removeAll(fixture().slides.at(2).id);
    deck.customShows.append({7, QStringLiteral("New"), {deck.slides.at(0).id}});
    deck.showSettings = deck.showSettings.normalized(); // as Set Up Slide Show applies it
    QString error;
    const QByteArray zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    const QStringList found = problems(zip);
    QVERIFY2(found.isEmpty(), qPrintable(found.join(QLatin1Char('\n'))));
    const QByteArray props = part(zip, QStringLiteral("ppt/presProps.xml"));
    QVERIFY(props.contains("<p:present/><p:custShow id=\"4\"/><p:penClr><a:srgbClr val=\"C00000\"/></p:penClr>"));
    // PowerPoint's other extensions stay: the media controls, the image data.
    QVERIFY(props.contains("showMediaCtrls"));
    QVERIFY(props.contains("discardImageEditData"));
    const Presentation back = readPptxData(zip, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(back.showSettings, deck.showSettings);
    QCOMPARE(back.customShows.size(), 3);
    QCOMPARE(back.customShows.at(0).slides, (QStringList{back.slides.at(0).id}));
    QCOMPARE(back.customShows.at(2).name, QStringLiteral("New"));
    QCOMPARE(back.customShows.at(2).number, 7u);
}

void PptxPlaybackTest::timingsReadAndWritten()
{
    Presentation deck = fixture();
    QCOMPARE(deck.slides.at(1).advanceAfterMs, 5000);
    QVERIFY(!deck.slides.at(1).advanceOnClick);
    QCOMPARE(deck.slides.at(3).advanceAfterMs, 3000);
    QVERIFY(deck.slides.at(3).advanceOnClick);
    QCOMPARE(deck.slides.at(0).advanceAfterMs, -1);
    // Rehearsed again: both branches of the kept transition take the new
    // timing; a slide without a transition gets a bare one.
    deck.slides[1].advanceAfterMs = 7250;
    deck.slides[1].advanceOnClick = true;
    deck.slides[0].advanceAfterMs = 1500;
    deck.slides[3].advanceAfterMs = -1;
    QString error;
    const QByteArray zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    const QStringList found = problems(zip);
    QVERIFY2(found.isEmpty(), qPrintable(found.join(QLatin1Char('\n'))));
    const QByteArray two = part(zip, QStringLiteral("ppt/slides/slide2.xml"));
    QCOMPARE(two.count("advTm=\"7250\""), 2);
    QVERIFY(!two.contains("advClick"));
    QVERIFY(two.contains("p14:prism"));
    QVERIFY(part(zip, QStringLiteral("ppt/slides/slide1.xml")).contains("<p:transition advTm=\"1500\"/>"));
    QVERIFY(!part(zip, QStringLiteral("ppt/slides/slide4.xml")).contains("advTm"));
    const Presentation back = readPptxData(zip, &error);
    for (qsizetype i = 0; i < deck.slides.size(); ++i) {
        QCOMPARE(back.slides.at(i).advanceAfterMs, deck.slides.at(i).advanceAfterMs);
        QCOMPARE(back.slides.at(i).advanceOnClick, deck.slides.at(i).advanceOnClick);
    }
}

void PptxPlaybackTest::mediaEditsReadAndWritten()
{
    Presentation deck = fixture();
    Shape *video = byOfficeId(deck.slides[2], 7);
    QVERIFY(video != nullptr && video->media.isMedia());
    QCOMPARE(video->media.trimStartMs, 1000);
    QCOMPARE(video->media.trimEndMs, 500);
    QCOMPARE(video->media.fadeInMs, 250);
    QCOMPARE(video->media.fadeOutMs, 750);
    QCOMPARE(video->media.bookmarks.size(), 2);
    QCOMPARE(video->media.bookmarks.at(0), (MediaBookmark{QStringLiteral("Intro"), 200}));
    QCOMPARE(video->media.bookmarks.at(1), (MediaBookmark{QStringLiteral("Chorus"), 2100}));
    video->media.trimStartMs = 0;
    video->media.trimEndMs = 1200;
    video->media.fadeInMs = 0;
    video->media.bookmarks.append({QStringLiteral("Outro"), 3000});
    QString error;
    QByteArray zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    QStringList found = problems(zip);
    QVERIFY2(found.isEmpty(), qPrintable(found.join(QLatin1Char('\n'))));
    const QByteArray three = part(zip, QStringLiteral("ppt/slides/slide3.xml"));
    QVERIFY(three.contains("<p14:trim end=\"1200\"/><p14:fade out=\"750\"/><p14:bmkLst>"));
    Presentation back = readPptxData(zip, &error);
    QCOMPARE(byOfficeId(back.slides[2], 7)->media, video->media);
    // A sound inserted here gets them written the way PowerPoint does.
    Shape sound;
    sound.id = QStringLiteral("snd");
    sound.kind = ShapeKind::Image;
    sound.rect = QRectF(20, 20, 48, 48);
    sound.media.kind = MediaKind::Audio;
    sound.media.data = PptxFixture::tinyWav(4000);
    sound.media.mime = QStringLiteral("audio/wav");
    sound.media.name = QStringLiteral("tone.wav");
    sound.media.trimStartMs = 500;
    sound.media.fadeOutMs = 1000;
    sound.media.bookmarks = {{QStringLiteral("Middle"), 2000}};
    sound.imagePng = mediaPoster(MediaKind::Audio, sound.media.name, QSize(96, 96));
    sound.image.mime = QStringLiteral("image/png");
    deck.slides[0].shapes.append(sound);
    zip = writePptx(deck, &error);
    found = problems(zip);
    QVERIFY2(found.isEmpty(), qPrintable(found.join(QLatin1Char('\n'))));
    back = readPptxData(zip, &error);
    const Shape &read = back.slides.at(0).shapes.last();
    QCOMPARE(read.media.trimStartMs, 500);
    QCOMPARE(read.media.fadeOutMs, 1000);
    QCOMPARE(read.media.bookmarks, sound.media.bookmarks);
}

void PptxPlaybackTest::effectDirectionsAndExitsRoundTrip()
{
    Presentation deck = fixture();
    const Slide &slide = deck.slides.at(3);
    const Shape *chart = byOfficeId(slide, 2);
    QCOMPARE(chart->buildStep, 1);
    QCOMPARE(effectiveDirection(chart->animation, chart->animationDirection), Direction::TopLeft);
    const Shape *peek = byOfficeId(slide, 30);
    QCOMPARE(peek->animation, ShapeAnimation::Peek);
    QCOMPARE(peek->buildStep, 2);
    QCOMPARE(peek->animationDirection, Direction::Right);
    QCOMPARE(peek->exit.step, 4);
    QCOMPARE(peek->exit.animation, ShapeAnimation::Wipe);
    QCOMPARE(peek->exit.direction, Direction::Left);
    const Shape *floating = byOfficeId(slide, 31);
    QCOMPARE(floating->animation, ShapeAnimation::Float);
    QCOMPARE(effectiveDirection(floating->animation, floating->animationDirection), Direction::Bottom); // Float Down
    QCOMPARE(floating->buildStep, 3);
    const Shape *split = byOfficeId(slide, 32);
    QCOMPARE(split->animation, ShapeAnimation::Split);
    QCOMPARE(split->animationDirection, Direction::HorizontalIn);
    QCOMPARE(split->buildStep, 1);
    // Unchanged: the timing is PowerPoint's own; changed: written from the model.
    QString error;
    QByteArray zip = writePptx(deck, &error);
    QVERIFY(part(zip, QStringLiteral("ppt/slides/slide4.xml")).contains("bldLst"));
    Shape *edit = byOfficeId(deck.slides[3], 31);
    edit->animationDirection = 0; // Float Up
    edit->exit = {5, ShapeAnimation::FlyInBottom, Direction::BottomRight, 600};
    byOfficeId(deck.slides[3], 2)->animation = flyInFor(Direction::BottomLeft); // as Effect Options sets it
    byOfficeId(deck.slides[3], 2)->animationDirection = Direction::BottomLeft;
    zip = writePptx(deck, &error);
    QVERIFY2(!zip.isEmpty(), qPrintable(error));
    const QStringList found = problems(zip);
    QVERIFY2(found.isEmpty(), qPrintable(found.join(QLatin1Char('\n'))));
    const QByteArray four = part(zip, QStringLiteral("ppt/slides/slide4.xml"));
    QVERIFY(four.contains("presetID=\"42\" presetClass=\"entr\" presetSubtype=\"0\""));
    QVERIFY(four.contains("presetID=\"2\" presetClass=\"exit\" presetSubtype=\"6\""));
    QVERIFY(four.contains("presetID=\"2\" presetClass=\"entr\" presetSubtype=\"12\""));
    QVERIFY(four.contains("presetID=\"22\" presetClass=\"exit\" presetSubtype=\"8\""));
    QVERIFY(four.contains("presetID=\"12\" presetClass=\"entr\" presetSubtype=\"2\""));
    QVERIFY(four.contains("presetID=\"16\" presetClass=\"entr\" presetSubtype=\"21\""));
    const Presentation back = readPptxData(zip, &error);
    for (const quint32 id : {2u, 30u, 31u, 32u}) {
        const Shape *was = byOfficeId(deck.slides.at(3), id);
        const Shape *is = byOfficeId(back.slides.at(3), id);
        QCOMPARE(is->buildStep, was->buildStep);
        QCOMPARE(is->animation, was->animation);
        QCOMPARE(effectiveDirection(is->animation, is->animationDirection),
                 effectiveDirection(was->animation, was->animationDirection));
        QCOMPARE(is->exit.step, was->exit.step);
        QCOMPARE(is->exit.animation, was->exit.animation);
        QCOMPARE(is->exit.direction, was->exit.direction);
    }
}

void PptxPlaybackTest::directionsAndExitsAreDrawn()
{
    // A red square entering by Wipe from the left, half way: its left half.
    Presentation deck;
    Slide &slide = deck.addSlide(QStringLiteral("Wipe"));
    Shape square = box(QStringLiteral("s"), QRectF(100, 100, 200, 200));
    square.fill = Qt::red;
    square.strokeWidth = 0;
    square.buildStep = 1;
    square.animation = ShapeAnimation::Wipe;
    square.animationDirection = Direction::Left;
    square.exit = {2, ShapeAnimation::FlyInLeft, Direction::Right, 500};
    slide.shapes.append(square);
    const auto render = [&deck](int build, qreal progress) {
        QImage image(QSize(720, 405), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        renderSlide(&painter, deck, deck.slides.first(), build, progress);
        return image;
    };
    QImage half = render(1, 0.5);
    QCOMPARE(QColor(half.pixel(130, 200)), QColor(Qt::red));   // the left part is in
    QCOMPARE(QColor(half.pixel(290, 200)), QColor(Qt::white)); // the right not yet
    // The exit flies out to the right: half way, the square has moved right.
    QImage leaving = render(2, 0.5);
    QCOMPARE(QColor(leaving.pixel(110, 200)), QColor(Qt::white));
    // After its exit it is gone; the editor (build 1000) still shows it.
    QCOMPARE(QColor(render(3, 1.0).pixel(200, 200)), QColor(Qt::white));
    QCOMPARE(QColor(render(1000, 1.0).pixel(200, 200)), QColor(Qt::red));
}

void PptxPlaybackTest::playlistFollowsTheSettings()
{
    Presentation deck;
    for (int i = 0; i < 6; ++i) {
        Q_UNUSED(deck.addSlide(QStringLiteral("S%1").arg(i + 1)));
    }
    deck.slides[2].hidden = true;
    QCOMPARE(showPlaylist(deck), (QList<int>{0, 1, 3, 4, 5}));
    deck.showSettings.range = ShowRange::Slides;
    deck.showSettings.from = 2;
    deck.showSettings.to = 4;
    QCOMPARE(showPlaylist(deck), (QList<int>{1, 3}));
    deck.customShows = {{5, QStringLiteral("Back to front"), {deck.slides[5].id, deck.slides[2].id, deck.slides[0].id}}};
    deck.showSettings.range = ShowRange::Custom;
    deck.showSettings.customShow = 5;
    QCOMPARE(showPlaylist(deck), (QList<int>{5, 0})); // hidden left out
    QCOMPARE(showPlaylist(deck, 0), (QList<int>{5, 0}));
    deck.showSettings.customShow = 9; // a show the deck does not have: every slide
    QCOMPARE(showPlaylist(deck), (QList<int>{0, 1, 3, 4, 5}));
    QCOMPARE(nextCustomShowNumber(deck), 6u);
    QCOMPARE(formatTiming(65400), QStringLiteral("01:05"));
    QCOMPARE(formatTiming(3723000), QStringLiteral("01:02:03"));
}

void PptxPlaybackTest::clicksHitActions()
{
    const Presentation deck = fixture();
    const Slide &slide = deck.slides.at(1);
    // Shape 10 sits at (20, 400) 40x40 pt; shape 16 at (320, 400).
    std::optional<ActionHit> hit = actionAt(slide, QPointF(40, 420), 0, false);
    QVERIFY(hit.has_value());
    QCOMPARE(parseAction(hit->action).kind, ActionKind::NextSlide);
    QVERIFY(hit->action.highlight);
    QCOMPARE(hit->bounds, QRectF(20, 400, 40, 40));
    hit = actionAt(slide, QPointF(40, 420), 0, true);
    QCOMPARE(parseAction(hit->action).kind, ActionKind::LastSlide);
    QCOMPARE(parseAction(actionAt(slide, QPointF(340, 420), 0, false)->action).kind, ActionKind::Slide);
    QVERIFY(!actionAt(slide, QPointF(700, 20), 0, false).has_value());
    // Linked text (at y 300–340 pt): across the line, the first word goes
    // to slide 3, the plain words between do nothing, the last words go to
    // the web, and past the text nothing.
    hit = actionAt(slide, QPointF(24, 312), 0, false);
    QVERIFY(hit.has_value());
    QVERIFY(hit->text);
    QStringList seen;
    for (qreal x = 21; x < 499; x += 1) {
        const std::optional<ActionHit> at = actionAt(slide, QPointF(x, 312), 0, false);
        const QString link = at ? at->action.link : QString();
        if (seen.isEmpty() || seen.last() != link) {
            seen.append(link);
        }
    }
    QCOMPARE(seen, (QStringList{slideLink(deck.slides.at(2).id), QString(), QStringLiteral("https://example.org/m91"), QString()}));
    // A shape not on the slide yet, or gone, is not hit.
    Slide built = slide;
    Shape *next = byOfficeId(built, 10);
    next->buildStep = 2;
    QVERIFY(!actionAt(built, QPointF(40, 420), 1, false).has_value());
    QVERIFY(actionAt(built, QPointF(40, 420), 2, false).has_value());
    next->exit = {3, ShapeAnimation::Fade, 0, 500};
    QVERIFY(!actionAt(built, QPointF(40, 420), 3, false).has_value());
    // A group member's action wins over its group's.
    Slide grouped;
    Shape group = box(QStringLiteral("g"), QRectF(100, 100, 200, 100));
    group.kind = ShapeKind::Group;
    group.childRect = QRectF(0, 0, 400, 200);
    group.click.link = jumpLink(ActionKind::EndShow);
    Shape member = box(QStringLiteral("m"), QRectF(0, 0, 200, 200));
    member.click.link = jumpLink(ActionKind::FirstSlide);
    group.children = {member, box(QStringLiteral("plain"), QRectF(200, 0, 200, 200))};
    grouped.shapes.append(group);
    hit = actionAt(grouped, QPointF(150, 150), 0, false);
    QCOMPARE(parseAction(hit->action).kind, ActionKind::FirstSlide);
    QCOMPARE(hit->bounds, QRectF(100, 100, 100, 100));
    QCOMPARE(parseAction(actionAt(grouped, QPointF(250, 150), 0, false)->action).kind, ActionKind::EndShow);
}

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    PptxPlaybackTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_pptx_playback.moc"
