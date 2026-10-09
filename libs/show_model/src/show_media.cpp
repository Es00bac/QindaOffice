// SPDX-License-Identifier: GPL-3.0-or-later
// Media probing (kind, type, duration from the container) and the posters
// of newly inserted media (show_media.h).

#include "show_media.h"

#include <QBuffer>
#include <QFileInfo>
#include <QHash>
#include <QPainter>
#include <QPainterPath>

#include <climits>

namespace QindaOffice::Show {

namespace {

quint32 be32(const QByteArray &b, qsizetype at)
{
    if (at < 0 || at + 4 > b.size()) {
        return 0;
    }
    const auto *p = reinterpret_cast<const uchar *>(b.constData() + at);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint64 be64(const QByteArray &b, qsizetype at)
{
    return (quint64(be32(b, at)) << 32) | be32(b, at + 4);
}

quint32 le32(const QByteArray &b, qsizetype at)
{
    if (at < 0 || at + 4 > b.size()) {
        return 0;
    }
    const auto *p = reinterpret_cast<const uchar *>(b.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint64 le64(const QByteArray &b, qsizetype at)
{
    return quint64(le32(b, at)) | (quint64(le32(b, at + 4)) << 32);
}

// ISO base media (MP4, MOV, M4A): the movie header's duration and whether
// a track is a video track.
void probeIsoMedia(const QByteArray &b, MediaProbe &probe)
{
    const QByteArray brand = b.mid(8, 4);
    const qsizetype moov = b.indexOf("moov");
    const qsizetype mvhd = moov >= 0 ? b.indexOf("mvhd", moov) : -1;
    if (mvhd > 0) {
        const bool wide = uchar(b.at(mvhd + 4)) == 1;
        const quint32 scale = be32(b, mvhd + (wide ? 24 : 16));
        const quint64 duration = wide ? be64(b, mvhd + 28) : be32(b, mvhd + 20);
        if (scale > 0) {
            probe.durationMs = int(qMin<quint64>(duration * 1000 / scale, quint64(INT_MAX)));
        }
    }
    bool video = brand == "qt  " || brand == "M4V ";
    for (qsizetype at = moov >= 0 ? b.indexOf("hdlr", moov) : -1; at > 0 && !video; at = b.indexOf("hdlr", at + 4)) {
        video = b.mid(at + 12, 4) == "vide";
    }
    if (brand == "M4A " || brand == "M4B ") {
        video = false;
    }
    probe.kind = video ? MediaKind::Video : MediaKind::Audio;
    if (!video) {
        probe.mime = QStringLiteral("audio/mp4");
        probe.extension = QStringLiteral("m4a");
    } else if (brand == "qt  ") {
        probe.mime = QStringLiteral("video/quicktime");
        probe.extension = QStringLiteral("mov");
    } else {
        probe.mime = QStringLiteral("video/mp4");
        probe.extension = QStringLiteral("mp4");
    }
}

void probeWave(const QByteArray &b, MediaProbe &probe)
{
    probe.kind = MediaKind::Audio;
    probe.mime = QStringLiteral("audio/wav");
    probe.extension = QStringLiteral("wav");
    quint32 byteRate = 0;
    for (qsizetype at = 12; at + 8 <= b.size();) {
        const QByteArray id = b.mid(at, 4);
        const quint32 size = le32(b, at + 4);
        if (id == "fmt ") {
            byteRate = le32(b, at + 16);
        } else if (id == "data" && byteRate > 0) {
            probe.durationMs = int(quint64(size) * 1000 / byteRate);
            return;
        }
        at += 8 + qsizetype(size) + (size & 1);
    }
}

void probeAvi(const QByteArray &b, MediaProbe &probe)
{
    probe.kind = MediaKind::Video;
    probe.mime = QStringLiteral("video/x-msvideo");
    probe.extension = QStringLiteral("avi");
    const qsizetype avih = b.indexOf("avih");
    if (avih > 0) {
        const quint64 perFrame = le32(b, avih + 8);
        const quint64 frames = le32(b, avih + 24);
        probe.durationMs = int(qMin<quint64>(perFrame * frames / 1000, quint64(INT_MAX)));
    }
}

void probeAsf(const QByteArray &b, MediaProbe &probe)
{
    static const QByteArray kFileProperties = QByteArray::fromHex("A1DCAB8C47A9CF118EE400C00C205365");
    static const QByteArray kVideoStream = QByteArray::fromHex("C0EF19BC4D5BCF11A8FD00805F5C442B");
    const bool video = b.contains(kVideoStream);
    probe.kind = video ? MediaKind::Video : MediaKind::Audio;
    probe.mime = video ? QStringLiteral("video/x-ms-wmv") : QStringLiteral("audio/x-ms-wma");
    probe.extension = video ? QStringLiteral("wmv") : QStringLiteral("wma");
    const qsizetype at = b.indexOf(kFileProperties);
    if (at >= 0) {
        // GUID, size, file id, file size, creation, packets, play duration
        // (100 ns), send duration, preroll (ms).
        const quint64 play = le64(b, at + 64);
        const quint64 preroll = le64(b, at + 80);
        const quint64 ms = play / 10000;
        probe.durationMs = int(qMin<quint64>(ms > preroll ? ms - preroll : ms, quint64(INT_MAX)));
    }
}

void probeMpegAudio(const QByteArray &b, MediaProbe &probe)
{
    probe.kind = MediaKind::Audio;
    probe.mime = QStringLiteral("audio/mpeg");
    probe.extension = QStringLiteral("mp3");
    qsizetype at = 0;
    if (b.startsWith("ID3") && b.size() > 10) {
        at = 10 + ((qsizetype(b.at(6)) & 0x7f) << 21 | (qsizetype(b.at(7)) & 0x7f) << 14 | (qsizetype(b.at(8)) & 0x7f) << 7
                   | (qsizetype(b.at(9)) & 0x7f));
    }
    while (at + 4 <= b.size() && !(uchar(b.at(at)) == 0xFF && (uchar(b.at(at + 1)) & 0xE0) == 0xE0)) {
        ++at;
    }
    if (at + 4 > b.size()) {
        return;
    }
    // Layer III bit rates (kbit/s) of MPEG-1 and of MPEG-2/2.5, and the
    // sample rates (MPEG-2 halves them, MPEG-2.5 quarters them).
    static const int kBitrates1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
    static const int kBitrates2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
    static const int kRates[] = {44100, 48000, 32000, 0};
    const int version = (uchar(b.at(at + 1)) >> 3) & 3; // 3 = MPEG-1
    const int index = (uchar(b.at(at + 2)) >> 4) & 15;
    const int bitrate = version == 3 ? kBitrates1[index] : kBitrates2[index];
    int rate = kRates[(uchar(b.at(at + 2)) >> 2) & 3];
    if (version != 3) {
        rate /= version == 2 ? 2 : 4;
    }
    const int samples = version == 3 ? 1152 : 576;
    const qsizetype xing = qMax(b.indexOf("Xing", at), b.indexOf("Info", at));
    if (xing > 0 && xing < at + 64 && (be32(b, xing + 4) & 1) != 0 && rate > 0) {
        const quint64 frames = be32(b, xing + 8);
        probe.durationMs = int(frames * samples * 1000 / quint64(rate));
    } else if (bitrate > 0) {
        probe.durationMs = int(quint64(b.size() - at) * 8 / quint64(bitrate));
    }
}

void probeFlac(const QByteArray &b, MediaProbe &probe)
{
    probe.kind = MediaKind::Audio;
    probe.mime = QStringLiteral("audio/flac");
    probe.extension = QStringLiteral("flac");
    if (b.size() >= 26) {
        const quint64 bits = be64(b, 18); // STREAMINFO: rate (20) channels (3) depth (5) samples (36)
        const quint64 rate = bits >> 44;
        const quint64 samples = bits & 0xFFFFFFFFFull;
        if (rate > 0) {
            probe.durationMs = int(samples * 1000 / rate);
        }
    }
}

} // namespace

QString mediaMimeForExtension(const QString &extension)
{
    static const QHash<QString, QString> types = {
        {QStringLiteral("mp4"), QStringLiteral("video/mp4")},         {QStringLiteral("m4v"), QStringLiteral("video/mp4")},
        {QStringLiteral("mov"), QStringLiteral("video/quicktime")},   {QStringLiteral("wmv"), QStringLiteral("video/x-ms-wmv")},
        {QStringLiteral("avi"), QStringLiteral("video/x-msvideo")},   {QStringLiteral("mpg"), QStringLiteral("video/mpeg")},
        {QStringLiteral("mpeg"), QStringLiteral("video/mpeg")},       {QStringLiteral("webm"), QStringLiteral("video/webm")},
        {QStringLiteral("mkv"), QStringLiteral("video/x-matroska")},  {QStringLiteral("mp3"), QStringLiteral("audio/mpeg")},
        {QStringLiteral("m4a"), QStringLiteral("audio/mp4")},         {QStringLiteral("wav"), QStringLiteral("audio/wav")},
        {QStringLiteral("wma"), QStringLiteral("audio/x-ms-wma")},    {QStringLiteral("aac"), QStringLiteral("audio/aac")},
        {QStringLiteral("flac"), QStringLiteral("audio/flac")},       {QStringLiteral("ogg"), QStringLiteral("audio/ogg")},
    };
    return types.value(extension.toLower());
}

QString mediaExtensionForMime(const QString &mime)
{
    static const QHash<QString, QString> extensions = {
        {QStringLiteral("video/mp4"), QStringLiteral("mp4")},        {QStringLiteral("video/quicktime"), QStringLiteral("mov")},
        {QStringLiteral("video/x-ms-wmv"), QStringLiteral("wmv")},   {QStringLiteral("video/x-msvideo"), QStringLiteral("avi")},
        {QStringLiteral("video/avi"), QStringLiteral("avi")},        {QStringLiteral("video/mpeg"), QStringLiteral("mpg")},
        {QStringLiteral("video/webm"), QStringLiteral("webm")},      {QStringLiteral("video/x-matroska"), QStringLiteral("mkv")},
        {QStringLiteral("audio/mpeg"), QStringLiteral("mp3")},       {QStringLiteral("audio/mp4"), QStringLiteral("m4a")},
        {QStringLiteral("audio/x-m4a"), QStringLiteral("m4a")},      {QStringLiteral("audio/wav"), QStringLiteral("wav")},
        {QStringLiteral("audio/x-wav"), QStringLiteral("wav")},      {QStringLiteral("audio/x-ms-wma"), QStringLiteral("wma")},
        {QStringLiteral("audio/aac"), QStringLiteral("aac")},        {QStringLiteral("audio/flac"), QStringLiteral("flac")},
        {QStringLiteral("audio/ogg"), QStringLiteral("ogg")},
    };
    return extensions.value(mime.toLower());
}

MediaProbe probeMedia(const QByteArray &b, const QString &fileName)
{
    MediaProbe probe;
    if (b.size() >= 12 && b.mid(4, 4) == "ftyp") {
        probeIsoMedia(b, probe);
    } else if (b.startsWith("RIFF") && b.mid(8, 4) == "WAVE") {
        probeWave(b, probe);
    } else if (b.startsWith("RIFF") && b.mid(8, 4) == "AVI ") {
        probeAvi(b, probe);
    } else if (b.startsWith(QByteArray::fromHex("3026B2758E66CF11A6D900AA0062CE6C"))) {
        probeAsf(b, probe);
    } else if (b.startsWith("fLaC")) {
        probeFlac(b, probe);
    } else if (b.startsWith("OggS")) {
        probe = {MediaKind::Audio, QStringLiteral("audio/ogg"), QStringLiteral("ogg"), 0};
    } else if (b.startsWith(QByteArray::fromHex("1A45DFA3"))) {
        probe = {MediaKind::Video, QStringLiteral("video/webm"), QStringLiteral("webm"), 0};
    } else if (b.startsWith(QByteArray::fromHex("000001BA")) || b.startsWith(QByteArray::fromHex("000001B3"))) {
        probe = {MediaKind::Video, QStringLiteral("video/mpeg"), QStringLiteral("mpg"), 0};
    } else if (b.startsWith("ID3") || (b.size() > 2 && uchar(b.at(0)) == 0xFF && (uchar(b.at(1)) & 0xE0) == 0xE0)) {
        probeMpegAudio(b, probe);
    }
    if (probe.kind == MediaKind::None && !fileName.isEmpty()) {
        // Unknown bytes: trust the name.
        const QString extension = QFileInfo(fileName).suffix().toLower();
        probe.mime = mediaMimeForExtension(extension);
        probe.extension = extension;
        probe.kind = probe.mime.startsWith(QLatin1String("video/")) ? MediaKind::Video
            : probe.mime.startsWith(QLatin1String("audio/"))         ? MediaKind::Audio
                                                                      : MediaKind::None;
    }
    return probe;
}

QByteArray mediaPoster(MediaKind kind, const QString &name, const QSize &size)
{
    QImage image(size.isValid() ? size : QSize(640, 360), QImage::Format_ARGB32_Premultiplied);
    const QRectF box(QPointF(0, 0), QSizeF(image.size()));
    const qreal unit = qMin(box.width(), box.height());
    image.fill(kind == MediaKind::Video ? QColor(0x20, 0x22, 0x26) : QColor(Qt::transparent));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    if (kind == MediaKind::Video) {
        // A play mark in a ring, the file's name under it.
        const QPointF centre = box.center() - QPointF(0, unit * 0.06);
        const qreal r = unit * 0.18;
        painter.setPen(QPen(QColor(0xE8, 0xE8, 0xE8), unit * 0.02));
        painter.setBrush(QColor(255, 255, 255, 40));
        painter.drawEllipse(centre, r, r);
        QPainterPath play;
        play.moveTo(centre + QPointF(-r * 0.35, -r * 0.5));
        play.lineTo(centre + QPointF(r * 0.55, 0));
        play.lineTo(centre + QPointF(-r * 0.35, r * 0.5));
        play.closeSubpath();
        painter.fillPath(play, QColor(0xF2, 0xF2, 0xF2));
        if (!name.isEmpty()) {
            QFont font;
            font.setPixelSize(qMax(10, int(unit * 0.07)));
            painter.setFont(font);
            painter.setPen(QColor(0xC8, 0xC8, 0xC8));
            const QRectF text(box.left() + unit * 0.05, centre.y() + r * 1.4, box.width() - unit * 0.1, unit * 0.12);
            painter.drawText(text, Qt::AlignHCenter | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(name, Qt::ElideMiddle, int(text.width())));
        }
    } else {
        // A speaker with two sound waves.
        const QColor ink(0x40, 0x40, 0x40);
        QPainterPath speaker;
        speaker.moveTo(unit * 0.16, unit * 0.38);
        speaker.lineTo(unit * 0.32, unit * 0.38);
        speaker.lineTo(unit * 0.52, unit * 0.20);
        speaker.lineTo(unit * 0.52, unit * 0.80);
        speaker.lineTo(unit * 0.32, unit * 0.62);
        speaker.lineTo(unit * 0.16, unit * 0.62);
        speaker.closeSubpath();
        painter.fillPath(speaker, ink);
        painter.setPen(QPen(ink, unit * 0.05, Qt::SolidLine, Qt::RoundCap));
        painter.setBrush(Qt::NoBrush);
        for (const qreal radius : {0.14, 0.26}) {
            const QRectF arc(unit * 0.52 - unit * radius, unit * 0.5 - unit * radius, unit * radius * 2, unit * radius * 2);
            painter.drawArc(arc, -50 * 16, 100 * 16);
        }
    }
    painter.end();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}

} // namespace QindaOffice::Show
