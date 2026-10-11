// SPDX-License-Identifier: GPL-3.0-or-later
// M91 (D-099) Export ▸ Create a Video gates (video_export.h): the
// timeline follows the slides' rehearsed timings and leaves hidden slides
// out; frames come from the slide renderer, with builds spread over the
// slide and a fade transition cross-faded; a three-slide deck at a tiny
// size becomes an MP4 that ffprobe reads with the right duration and
// frame count, a slide's sound mixed in; Cancel leaves no file behind;
// a missing ffmpeg is reported. The ffmpeg cases QSKIP when it is absent.

#include "show_action.h"
#include "video_export.h"

#include <QDir>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using namespace QindaOffice::Show;

namespace {

Presentation colourDeck()
{
    Presentation deck;
    const QColor colours[] = {QColor(200, 0, 0), QColor(0, 160, 0), QColor(90, 90, 90), QColor(0, 0, 200)};
    for (int i = 0; i < 4; ++i) {
        Slide &slide = deck.addSlide(QStringLiteral("S%1").arg(i + 1));
        slide.background.set = true;
        slide.background.color = colours[i];
    }
    deck.slides[1].advanceAfterMs = 500;
    deck.slides[2].hidden = true;
    deck.slides[3].transition = QStringLiteral("fade");
    // A white square on slide 1 arriving at its one click (half way in).
    Shape square;
    square.id = QStringLiteral("sq");
    square.kind = ShapeKind::Rectangle;
    square.rect = QRectF(310, 152, 100, 100);
    square.fill = Qt::white;
    square.strokeWidth = 0;
    square.buildStep = 1;
    deck.slides[0].shapes.append(square);
    return deck;
}

QColor centre(const QImage &image)
{
    return QColor(image.pixel(image.width() / 2, image.height() / 2));
}

QColor corner(const QImage &image)
{
    return QColor(image.pixel(2, 2));
}

QString probe(const QString &file, const QStringList &entries)
{
    QProcess process;
    process.start(QStandardPaths::findExecutable(QStringLiteral("ffprobe")),
                  QStringList{QStringLiteral("-v"), QStringLiteral("error")} + entries + QStringList{file});
    process.waitForFinished(60000);
    return QString::fromUtf8(process.readAllStandardOutput());
}

} // namespace

class VideoExportTest : public QObject {
    Q_OBJECT
private slots:
    void timelineFollowsTimingsAndHiddenSlides()
    {
        const Presentation deck = colourDeck();
        VideoSettings settings;
        settings.secondsPerSlide = 1;
        QList<VideoSegment> timeline = videoTimeline(deck, settings);
        QCOMPARE(timeline.size(), 3);
        QCOMPARE(timeline.at(0).slide, 0);
        QCOMPARE(timeline.at(1).startMs, 1000);
        QCOMPARE(timeline.at(1).durationMs, 500);
        QCOMPARE(timeline.at(2).slide, 3); // the hidden slide is left out
        QVERIFY(timeline.at(2).fadeIn);
        QCOMPARE(videoDurationMs(timeline), 2500);
        settings.useTimings = false;
        timeline = videoTimeline(deck, settings);
        QCOMPARE(videoDurationMs(timeline), 3000);
    }

    void framesComeFromTheRenderer()
    {
        const Presentation deck = colourDeck();
        VideoSettings settings;
        settings.secondsPerSlide = 1;
        const QList<VideoSegment> timeline = videoTimeline(deck, settings);
        const QSize size(160, 90);
        // Slide 1: its build comes at half its second.
        QCOMPARE(centre(videoFrame(deck, timeline, 100, size)), QColor(200, 0, 0));
        QCOMPARE(centre(videoFrame(deck, timeline, 900, size)), QColor(Qt::white));
        QCOMPARE(corner(videoFrame(deck, timeline, 1200, size)), QColor(0, 160, 0));
        // Slide 4 fades in over half a second from slide 2.
        const QColor half = corner(videoFrame(deck, timeline, 1750, size));
        QVERIFY2(half.green() > 40 && half.blue() > 60, qPrintable(half.name()));
        QCOMPARE(corner(videoFrame(deck, timeline, 2400, size)), QColor(0, 0, 200));
    }

    void exportsAnMp4FfprobeReads()
    {
        if (ffmpegPath().isEmpty() || QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty()) {
            QSKIP("ffmpeg / ffprobe are not installed");
        }
        Presentation deck = colourDeck();
        // A sound playing with slide 2.
        Shape sound;
        sound.id = QStringLiteral("snd");
        sound.kind = ShapeKind::Image;
        sound.rect = QRectF(10, 10, 48, 48);
        sound.media.kind = MediaKind::Audio;
        sound.media.data = builtInSound(QStringLiteral("Beep"));
        sound.media.mime = QStringLiteral("audio/wav");
        sound.media.autoPlay = true;
        sound.media.fadeOutMs = 100;
        deck.slides[1].shapes.append(sound);
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("deck.mp4"));
        VideoSettings settings;
        settings.size = QSize(64, 36);
        settings.secondsPerSlide = 1;
        int last = 0;
        int total = 0;
        QString error;
        QVERIFY2(exportVideo(deck, settings, file, [&](int frame, int frames) { last = frame; total = frames; }, nullptr, &error),
                 qPrintable(error));
        QCOMPARE(total, 75);
        QCOMPARE(last, 75);
        QVERIFY(QFileInfo::exists(file));
        QVERIFY(!QFileInfo::exists(file + QStringLiteral(".part")));
        const QString frames = probe(file, {QStringLiteral("-count_frames"), QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                                            QStringLiteral("-show_entries"), QStringLiteral("stream=nb_read_frames,width,height"),
                                            QStringLiteral("-of"), QStringLiteral("default=nw=1")});
        QVERIFY2(frames.contains(QStringLiteral("nb_read_frames=75")), qPrintable(frames));
        QVERIFY2(frames.contains(QStringLiteral("width=64")) && frames.contains(QStringLiteral("height=36")), qPrintable(frames));
        const QString format = probe(file, {QStringLiteral("-show_entries"), QStringLiteral("format=duration:stream=codec_type"),
                                            QStringLiteral("-of"), QStringLiteral("default=nw=1")});
        const QString duration = format.section(QStringLiteral("duration="), 1).section(QLatin1Char('\n'), 0, 0);
        QVERIFY2(qAbs(duration.toDouble() - 2.5) < 0.1, qPrintable(format));
        QVERIFY2(format.contains(QStringLiteral("codec_type=audio")), qPrintable(format));
        deck.slides[1].shapes.last().media.narration = true;
        deck.showSettings.narration = false;
        QVERIFY2(exportVideo(deck, settings, file, {}, nullptr, &error), qPrintable(error));
        const QString muted = probe(file, {QStringLiteral("-show_entries"), QStringLiteral("stream=codec_type"),
                                           QStringLiteral("-of"), QStringLiteral("default=nw=1")});
        QVERIFY2(!muted.contains(QStringLiteral("codec_type=audio")), qPrintable(muted));
    }

    void cancelLeavesNoFile()
    {
        if (ffmpegPath().isEmpty()) {
            QSKIP("ffmpeg is not installed");
        }
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("cancelled.mp4"));
        VideoSettings settings;
        settings.size = QSize(64, 36);
        settings.secondsPerSlide = 5;
        QFile existing(file);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        existing.write("previous export");
        existing.close();
        std::atomic_bool cancel{false};
        QString error;
        const bool done = exportVideo(colourDeck(), settings, file, [&cancel](int frame, int) { cancel = frame >= 10; }, &cancel, &error);
        QVERIFY(!done);
        QCOMPARE(error, QStringLiteral("Cancelled"));
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("previous export"));
        QVERIFY(!QFileInfo::exists(file + QStringLiteral(".part")));
        QCOMPARE(QDir(dir.path()).entryList({QStringLiteral("*.part-*")}, QDir::Files).size(), 0);
    }

    void missingFfmpegIsReported()
    {
        qputenv("QQO_FFMPEG", "/nonexistent/ffmpeg");
        const auto restore = qScopeGuard([] { qunsetenv("QQO_FFMPEG"); });
        QString reason;
        QVERIFY(ffmpegPath(&reason).isEmpty());
        QVERIFY(reason.contains(QStringLiteral("ffmpeg")));
        QTemporaryDir dir;
        QString error;
        QVERIFY(!exportVideo(colourDeck(), VideoSettings(), dir.filePath(QStringLiteral("x.mp4")), {}, nullptr, &error));
        QCOMPARE(error, reason);
    }
};

int main(int argc, char *argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    VideoExportTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_video_export.moc"
