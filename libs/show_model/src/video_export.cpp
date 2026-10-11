// SPDX-License-Identifier: GPL-3.0-or-later
// Export ▸ Create a Video (video_export.h).

#include "video_export.h"

#include "slide_renderer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QPainter>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <cmath>

namespace QindaOffice::Show {

namespace {

int buildCapOf(const Slide &slide)
{
    int highest = 0;
    for (const Shape &shape : slide.shapes) {
        if (!shape.media.isMedia()) {
            highest = qMax(highest, qMax(shape.buildStep, shape.exit.step));
        }
    }
    return highest;
}

// The longest effect starting at `step` (at least 1 ms).
int stepDurationMs(const Slide &slide, int step)
{
    int longest = 1;
    for (const Shape &shape : slide.shapes) {
        if (shape.buildStep == step && shape.animation != ShapeAnimation::Appear) {
            longest = qMax(longest, shape.animationMs);
        }
        if (shape.exit.step == step && shape.exit.animation != ShapeAnimation::Appear) {
            longest = qMax(longest, shape.exit.durationMs);
        }
    }
    return longest;
}

// Where in its segment a slide stands at `local` ms: the build step and how
// far its effects have played.
struct Moment {
    int build = 0;
    qreal progress = 1.0;
    qreal fade = 1.0;
    [[nodiscard]] bool operator==(const Moment &) const = default;
};

Moment momentAt(const Slide &slide, const VideoSegment &segment, qint64 local, int transitionMs)
{
    Moment m;
    const int cap = buildCapOf(slide);
    const qint64 step = segment.durationMs / (cap + 1);
    if (cap > 0 && step > 0) {
        m.build = int(qMin<qint64>(cap, local / step));
        if (m.build > 0) {
            const qint64 since = local - m.build * step;
            m.progress = qMin(1.0, qreal(since) / stepDurationMs(slide, m.build));
        }
    }
    if (segment.fadeIn && transitionMs > 0) {
        m.fade = qMin(1.0, qreal(local) / transitionMs);
    }
    return m;
}

void paintSlide(QPainter &painter, const Presentation &deck, int slide, int build, qreal progress, const QSize &size)
{
    const QSizeF page = deck.slideSize;
    const qreal scale = qMin(size.width() / page.width(), size.height() / page.height());
    painter.save();
    painter.translate((size.width() - page.width() * scale) / 2.0, (size.height() - page.height() * scale) / 2.0);
    painter.scale(scale, scale);
    renderSlide(&painter, deck, deck.slides.at(slide), build, progress);
    painter.restore();
}

int segmentAt(const QList<VideoSegment> &timeline, qint64 ms)
{
    for (qsizetype i = timeline.size() - 1; i >= 0; --i) {
        if (ms >= timeline.at(i).startMs) {
            return int(i);
        }
    }
    return 0;
}

// The filter chain that places one sound of the video: trimmed, its
// volume, its fades, delayed to where it starts, stopped with its slide.
QString soundFilter(int input, const MediaInfo &media, qint64 startMs, qint64 limitMs)
{
    qint64 length = limitMs;
    if (media.durationMs > 0 && !media.loop) {
        length = qMin(length, qint64(qMax(0, media.durationMs - media.trimStartMs - media.trimEndMs)));
    }
    const auto seconds = [](qint64 ms) { return QString::number(ms / 1000.0, 'f', 3); };
    QString chain = QStringLiteral("[%1:a]atrim=start=%2:duration=%3,asetpts=PTS-STARTPTS,aresample=48000,"
                                   "aformat=sample_rates=48000:channel_layouts=stereo,volume=%4")
                        .arg(input)
                        .arg(seconds(media.trimStartMs), seconds(length), QString::number(media.volume / 100.0, 'f', 2));
    if (media.fadeInMs > 0) {
        chain += QStringLiteral(",afade=t=in:st=0:d=%1").arg(seconds(qMin<qint64>(media.fadeInMs, length)));
    }
    if (media.fadeOutMs > 0 && length > media.fadeOutMs) {
        chain += QStringLiteral(",afade=t=out:st=%1:d=%2").arg(seconds(length - media.fadeOutMs), seconds(media.fadeOutMs));
    }
    return chain + QStringLiteral(",adelay=delays=%1:all=1[a%2]").arg(startMs).arg(input);
}

} // namespace

QList<VideoSegment> videoTimeline(const Presentation &deck, const VideoSettings &settings)
{
    QList<VideoSegment> out;
    const qint64 frame = 1000 / qMax(1, settings.fps);
    qint64 t = 0;
    for (qsizetype i = 0; i < deck.slides.size(); ++i) {
        const Slide &slide = deck.slides.at(i);
        if (slide.hidden) {
            continue;
        }
        qint64 duration = settings.useTimings && slide.advanceAfterMs >= 0 ? slide.advanceAfterMs
                                                                           : qint64(qMax(1, settings.secondsPerSlide)) * 1000;
        duration = qMax(duration, frame);
        out.append({int(i), t, duration, !out.isEmpty() && slide.transition != u"cut"});
        t += duration;
    }
    return out;
}

qint64 videoDurationMs(const QList<VideoSegment> &timeline)
{
    return timeline.isEmpty() ? 0 : timeline.last().startMs + timeline.last().durationMs;
}

QImage videoFrame(const Presentation &deck, const QList<VideoSegment> &timeline, qint64 ms, const QSize &size, int transitionMs)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(Qt::black);
    if (timeline.isEmpty()) {
        return image;
    }
    const int index = segmentAt(timeline, ms);
    const VideoSegment &segment = timeline.at(index);
    const Slide &slide = deck.slides.at(segment.slide);
    const Moment moment = momentAt(slide, segment, ms - segment.startMs, transitionMs);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (moment.fade < 1.0 && index > 0) {
        const VideoSegment &before = timeline.at(index - 1);
        paintSlide(painter, deck, before.slide, buildCapOf(deck.slides.at(before.slide)), 1.0, size);
        painter.setOpacity(moment.fade);
    }
    paintSlide(painter, deck, segment.slide, moment.build, moment.progress, size);
    painter.end();
    return image;
}

QString ffmpegPath(QString *reason)
{
    const QString path = qEnvironmentVariableIsSet("QQO_FFMPEG") ? qEnvironmentVariable("QQO_FFMPEG") : QStringLiteral("/usr/bin/ffmpeg");
    const QFileInfo info(path);
    if (info.isFile() && info.isExecutable()) {
        return info.absoluteFilePath();
    }
    if (reason != nullptr) {
        *reason = QObject::tr("Making a video needs ffmpeg (%1), which is not installed — install media-video/ffmpeg").arg(path);
    }
    return {};
}

QString videoEncoder(const QString &ffmpeg)
{
    QProcess probe;
    probe.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
    probe.waitForFinished(15000);
    const QString encoders = QString::fromUtf8(probe.readAllStandardOutput());
    for (const char *name : {" libx264 ", " libopenh264 "}) {
        if (encoders.contains(QLatin1String(name))) {
            return QString::fromLatin1(name).trimmed();
        }
    }
    return QStringLiteral("mpeg4");
}

bool exportVideo(const Presentation &deck, const VideoSettings &settings, const QString &path, const VideoProgress &progress,
                 const std::atomic_bool *cancel, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    QString reason;
    const QString ffmpeg = ffmpegPath(&reason);
    if (ffmpeg.isEmpty()) {
        return fail(reason);
    }
    const QList<VideoSegment> timeline = videoTimeline(deck, settings);
    if (timeline.isEmpty()) {
        return fail(QObject::tr("The presentation has no slides to show"));
    }
    const QSize size(qMax(2, settings.size.width() / 2 * 2), qMax(2, settings.size.height() / 2 * 2));
    const int fps = qBound(1, settings.fps, 60);
    const qint64 duration = videoDurationMs(timeline);
    const int frames = int(std::ceil(duration * fps / 1000.0));

    // The slides' sounds, as files ffmpeg reads.
    QTemporaryDir sounds;
    QStringList inputs;
    QStringList filters;
    int soundCount = 0;
    for (const VideoSegment &segment : timeline) {
        const Slide &slide = deck.slides.at(segment.slide);
        const int cap = buildCapOf(slide);
        for (const Shape &shape : slide.shapes) {
            const MediaInfo &media = shape.media;
            if (media.kind != MediaKind::Audio || media.data.isEmpty() || !sounds.isValid()
                || (media.narration && !deck.showSettings.narration)) {
                continue;
            }
            const qint64 offset = media.autoPlay ? 0 : segment.durationMs / (cap + 1) * qMin(cap, shape.buildStep);
            const qint64 limit = segment.durationMs - offset;
            if (limit <= 0) {
                continue;
            }
            const QString extension = mediaExtensionForMime(media.mime);
            const QString file = sounds.filePath(QStringLiteral("sound%1.%2").arg(soundCount).arg(extension.isEmpty() ? QStringLiteral("bin") : extension));
            QFile out(file);
            if (!out.open(QIODevice::WriteOnly) || out.write(media.data) != media.data.size()) {
                continue;
            }
            out.close();
            const int input = ++soundCount; // input 0 is the frames
            if (media.loop) {
                inputs += QStringLiteral("-stream_loop");
                inputs += QStringLiteral("-1");
            }
            inputs += {QStringLiteral("-i"), file};
            filters.append(soundFilter(input, media, segment.startMs + offset, limit));
        }
    }
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"),
                     QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("bgra"),
                     QStringLiteral("-s"), QStringLiteral("%1x%2").arg(size.width()).arg(size.height()),
                     QStringLiteral("-r"), QString::number(fps), QStringLiteral("-i"), QStringLiteral("pipe:0")};
    args += inputs;
    if (!filters.isEmpty()) {
        QString graph = filters.join(QLatin1Char(';'));
        QStringList names; // each chain's output label, "[aN]"
        for (const QString &filter : std::as_const(filters)) {
            names.append(QLatin1Char('[') + filter.section(QLatin1Char('['), -1));
        }
        graph += QLatin1Char(';') + names.join(QString())
            + QStringLiteral("amix=inputs=%1:normalize=0:dropout_transition=0[aout]").arg(filters.size());
        args += {QStringLiteral("-filter_complex"), graph, QStringLiteral("-map"), QStringLiteral("0:v"), QStringLiteral("-map"),
                 QStringLiteral("[aout]"), QStringLiteral("-c:a"), QStringLiteral("aac"), QStringLiteral("-b:a"), QStringLiteral("160k")};
    }
    const QString encoder = videoEncoder(ffmpeg);
    args += {QStringLiteral("-c:v"), encoder};
    if (encoder == u"libx264") {
        args += {QStringLiteral("-preset"), QStringLiteral("veryfast"), QStringLiteral("-crf"), QStringLiteral("20")};
    } else if (encoder == u"mpeg4") {
        args += {QStringLiteral("-q:v"), QStringLiteral("3")};
    }
    // Separate ordinary document windows may export to the same destination.
    // Their intermediate movies must never share (or truncate) one .part file.
    QTemporaryFile partialFile(path + QStringLiteral(".part-XXXXXX"));
    if (!partialFile.open()) {
        return fail(QObject::tr("Cannot write %1").arg(QDir::toNativeSeparators(path)));
    }
    const QString partial = partialFile.fileName();
    partialFile.close();
    args += {QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), QStringLiteral("-t"), QString::number(duration / 1000.0, 'f', 3),
             QStringLiteral("-movflags"), QStringLiteral("+faststart"), QStringLiteral("-f"), QStringLiteral("mp4"), partial};

    QProcess process;
    process.start(ffmpeg, args);
    const auto abandon = [&process, &partial, &fail](const QString &message) {
        process.kill();
        process.waitForFinished(5000);
        QFile::remove(partial);
        return fail(message);
    };
    if (!process.waitForStarted(15000)) {
        return abandon(QObject::tr("ffmpeg did not start: %1").arg(process.errorString()));
    }
    const qint64 frameBytes = qint64(size.width()) * size.height() * 4;
    QImage last;
    Moment lastMoment;
    int lastSegment = -1;
    for (int i = 0; i < frames; ++i) {
        if (cancel != nullptr && cancel->load()) {
            return abandon(QObject::tr("Cancelled"));
        }
        const qint64 ms = qint64(i) * 1000 / fps;
        const int index = segmentAt(timeline, ms);
        const VideoSegment &segment = timeline.at(index);
        const Moment moment = momentAt(deck.slides.at(segment.slide), segment, ms - segment.startMs, settings.transitionMs);
        // A frame like the last one (a still slide) is not drawn again.
        if (last.isNull() || index != lastSegment || !(moment == lastMoment) || moment.progress < 1.0 || moment.fade < 1.0) {
            last = videoFrame(deck, timeline, ms, size, settings.transitionMs);
            lastSegment = index;
            lastMoment = moment;
        }
        if (process.write(reinterpret_cast<const char *>(last.constBits()), frameBytes) != frameBytes) {
            return abandon(QObject::tr("ffmpeg stopped reading: %1").arg(QString::fromUtf8(process.readAllStandardError())));
        }
        while (process.bytesToWrite() > 4 * frameBytes) {
            if (!process.waitForBytesWritten(1000) && process.state() != QProcess::Running) {
                return abandon(QObject::tr("ffmpeg stopped: %1").arg(QString::fromUtf8(process.readAllStandardError())));
            }
            if (cancel != nullptr && cancel->load()) {
                return abandon(QObject::tr("Cancelled"));
            }
        }
        if (progress) {
            progress(i + 1, frames);
        }
    }
    process.closeWriteChannel();
    while (!process.waitForFinished(250)) {
        if (process.state() != QProcess::Running) {
            break;
        }
        if (cancel != nullptr && cancel->load()) {
            return abandon(QObject::tr("Cancelled"));
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return abandon(QObject::tr("ffmpeg could not make the video: %1").arg(QString::fromUtf8(process.readAllStandardError()).trimmed()));
    }
    // AGENT-GUARD: keep an existing export until its replacement commits.
    // A failed rename after removing the original would destroy the user's file.
    QFile encoded(partial);
    QSaveFile destination(path);
    if (!encoded.open(QIODevice::ReadOnly) || !destination.open(QIODevice::WriteOnly)) {
        QFile::remove(partial);
        return fail(QObject::tr("Cannot write %1").arg(QDir::toNativeSeparators(path)));
    }
    while (!encoded.atEnd()) {
        const QByteArray chunk = encoded.read(1024 * 1024);
        if (chunk.isEmpty() || destination.write(chunk) != chunk.size()) {
            QFile::remove(partial);
            return fail(QObject::tr("Cannot write %1").arg(QDir::toNativeSeparators(path)));
        }
        if (cancel != nullptr && cancel->load()) {
            QFile::remove(partial);
            return fail(QObject::tr("Cancelled"));
        }
    }
    const bool committed = destination.commit();
    QFile::remove(partial);
    return committed || fail(QObject::tr("Cannot write %1").arg(QDir::toNativeSeparators(path)));
}

} // namespace QindaOffice::Show
