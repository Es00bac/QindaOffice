// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Export ▸ Create a Video (M91, D-099): the deck as an MP4 that the
// system's ffmpeg (/usr/bin/ffmpeg, installed through Portage — never
// bundled; QQO_FFMPEG names another for the tests) encodes from frames the
// slide renderer draws — the renderer the show and the editor draw with —
// piped to it as raw RGB.
//
// The video follows the show's own rules: every slide not hidden, in deck
// order, each for its rehearsed timing (Slide::advanceAfterMs) when
// `useTimings` and it has one, else `secondsPerSlide`; a slide's builds
// (and exits) play evenly spread over its time, animated as in the show; a
// Fade transition cross-fades over its first half second and every other
// transition but Cut is played as that fade (documented, D-099). A slide's
// sounds (audio media embedded in it) are mixed in where they start in the
// show — with the slide, or at their click — trimmed, faded, and stopped
// when the slide ends; videos show their poster frame and add no sound.
//
// Frames repeat while nothing moves (a still slide is drawn once). The
// export runs on the calling thread (the app runs it on a worker): it
// reports progress, stops when `cancel` is set, and removes its partial
// file on cancel or failure.

#include "presentation.h"

#include <QImage>
#include <QSize>
#include <QString>

#include <atomic>
#include <functional>

namespace QindaOffice::Show {

struct VideoSettings {
    QSize size{1920, 1080};
    int fps = 30;
    int secondsPerSlide = 5;
    bool useTimings = true;
    int transitionMs = 500;
};

// One slide's time in the video.
struct VideoSegment {
    int slide = 0;         // deck position
    qint64 startMs = 0;
    qint64 durationMs = 0;
    bool fadeIn = false;   // cross-faded from the slide before
};

[[nodiscard]] QList<VideoSegment> videoTimeline(const Presentation &deck, const VideoSettings &settings);
[[nodiscard]] qint64 videoDurationMs(const QList<VideoSegment> &timeline);
// The frame at `ms` of the video, `size` pixels (the slide letterboxed on
// black), a cross-fade taking `transitionMs`.
[[nodiscard]] QImage videoFrame(const Presentation &deck, const QList<VideoSegment> &timeline, qint64 ms, const QSize &size,
                                int transitionMs = 500);

// The ffmpeg to use: QQO_FFMPEG when set, else /usr/bin/ffmpeg; empty when
// it is not an executable file. `reason` says why not.
[[nodiscard]] QString ffmpegPath(QString *reason = nullptr);
// The H.264 encoder ffmpeg has (libx264, else libopenh264), else MPEG-4
// part 2 ("mpeg4"), which every build has.
[[nodiscard]] QString videoEncoder(const QString &ffmpeg);

using VideoProgress = std::function<void(int frame, int frames)>;
// Writes the MP4 at `path`. False (with `error`) when ffmpeg is missing or
// fails, or when `cancel` became true — the partial file is removed then.
bool exportVideo(const Presentation &deck, const VideoSettings &settings, const QString &path, const VideoProgress &progress,
                 const std::atomic_bool *cancel, QString *error);

} // namespace QindaOffice::Show
