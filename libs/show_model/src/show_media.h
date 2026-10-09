// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Audio and video on slides (M75, D-084). A media shape is a picture (its
// poster frame: Shape::imagePng) that carries a sound or a film, as
// PowerPoint keeps one: a p:pic whose a:videoFile / a:audioFile and
// p14:media name the media part, and whose playback lives in p:timing
// (Start On Click / Automatically, Loop until Stopped, Hide While Not
// Playing, the volume). The bytes are the file's own, any format; the
// player plays them through Qt Multimedia when it is there (QindaShow),
// and shows the poster otherwise.
//
// media_probe: what show_model can tell about a file without decoding it —
// the kind and media type from its first bytes or its name, and its
// duration from the container (ISO BMFF mvhd, RIFF WAVE, ASF, MPEG audio
// frames), which PowerPoint writes into the play command's timing.

#include <QByteArray>
#include <QImage>
#include <QString>

namespace QindaOffice::Show {

enum class MediaKind { None, Video, Audio };

struct MediaInfo {
    MediaKind kind = MediaKind::None;
    QByteArray data;       // the embedded file; empty when linked
    QString mime;          // "video/mp4", "audio/mpeg", …
    QString link;          // a linked file's target (r:link, external)
    QString name;          // the file name it was inserted from (its extension)
    int durationMs = 0;    // 0 = unknown
    // Playback (PowerPoint's Playback tab).
    bool autoPlay = false;        // Start: Automatically (else On Click)
    bool loop = false;            // Loop until Stopped
    bool hideWhenStopped = false; // Hide While Not Playing
    int volume = 80;              // percent; PowerPoint's default is 80 %
    bool operator==(const MediaInfo &) const = default;
    [[nodiscard]] bool isMedia() const { return kind != MediaKind::None; }
};

// ---- probing ----
struct MediaProbe {
    MediaKind kind = MediaKind::None;
    QString mime;
    QString extension; // as the part is named ("mp4", "m4a", …)
    int durationMs = 0;
};
// From the bytes, falling back to the file name's extension.
[[nodiscard]] MediaProbe probeMedia(const QByteArray &bytes, const QString &fileName = QString());
// The media type PowerPoint's content types give an extension, and back.
[[nodiscard]] QString mediaMimeForExtension(const QString &extension);
[[nodiscard]] QString mediaExtensionForMime(const QString &mime);

// ---- posters ----
// The poster a newly inserted video or sound shows until it plays: a dark
// frame with a play mark and the file's name, or a speaker for a sound;
// `size` in pixels. PNG bytes.
[[nodiscard]] QByteArray mediaPoster(MediaKind kind, const QString &name, const QSize &size);

} // namespace QindaOffice::Show
