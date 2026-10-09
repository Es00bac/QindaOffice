// SPDX-License-Identifier: GPL-3.0-or-later
// File fields: what a file is from its first bytes, the summary a read
// gives, and the texts the datasheet shows (base_file_field.h).
#include "base_file_field.h"

#include <QRegularExpression>

#include <cstddef>

namespace QindaOffice::Base {

namespace {

struct Kind {
    const char *label;
    const char *extension;
    const char *mime;
    bool image;
};

FileInfo make(const Kind &kind, qint64 size)
{
    return FileInfo{size, QString::fromUtf8(kind.label), QString::fromLatin1(kind.extension),
                    QString::fromLatin1(kind.mime), kind.image};
}

// A literal's bytes, embedded NULs included (N counts the terminator).
template<std::size_t N>
bool startsWith(const QByteArray &head, const char (&signature)[N], qsizetype offset = 0)
{
    const auto length = qsizetype(N) - 1;
    return head.size() >= offset + length
           && QByteArrayView(head).sliced(offset, length) == QByteArrayView(signature, length);
}

quint32 le32(const QByteArray &head, int offset)
{
    if (head.size() < offset + 4) {
        return 0;
    }
    const auto b = [&](int i) { return quint32(quint8(head.at(offset + i))); };
    return b(0) | (b(1) << 8) | (b(2) << 16) | (b(3) << 24);
}

bool looksLikeText(const QByteArray &head)
{
    if (head.isEmpty()) {
        return false;
    }
    for (const char c : head) {
        const auto u = quint8(c);
        if (u < 0x09 || (u > 0x0D && u < 0x20) || u == 0x7F) {
            return false; // a control byte: binary (UTF-8 continuation bytes are >= 0x80 and pass)
        }
    }
    return true;
}

} // namespace

FileInfo sniffFile(const QByteArray &head, qint64 size)
{
    static constexpr Kind png{"PNG image", "png", "image/png", true};
    static constexpr Kind jpeg{"JPEG image", "jpg", "image/jpeg", true};
    static constexpr Kind gif{"GIF image", "gif", "image/gif", true};
    static constexpr Kind bmp{"BMP image", "bmp", "image/bmp", true};
    static constexpr Kind webp{"WebP image", "webp", "image/webp", true};
    static constexpr Kind tiff{"TIFF image", "tif", "image/tiff", true};
    static constexpr Kind ico{"Icon", "ico", "image/x-icon", true};
    static constexpr Kind svg{"SVG image", "svg", "image/svg+xml", true};
    static constexpr Kind heic{"HEIC image", "heic", "image/heic", false};
    static constexpr Kind pdf{"PDF document", "pdf", "application/pdf", false};
    static constexpr Kind zip{"ZIP archive or Office document", "zip", "application/zip", false};
    static constexpr Kind ole{"Office 97–2003 file", "bin", "application/x-ole-storage", false};
    static constexpr Kind accessOle{"Access OLE object", "bin", "application/octet-stream", false};
    static constexpr Kind rtf{"RTF document", "rtf", "application/rtf", false};
    static constexpr Kind mp3{"MP3 audio", "mp3", "audio/mpeg", false};
    static constexpr Kind wav{"WAV audio", "wav", "audio/wav", false};
    static constexpr Kind mp4{"MP4 video", "mp4", "video/mp4", false};
    static constexpr Kind ogg{"Ogg media", "ogg", "application/ogg", false};
    static constexpr Kind gzip{"GZIP archive", "gz", "application/gzip", false};
    static constexpr Kind sevenZip{"7-Zip archive", "7z", "application/x-7z-compressed", false};
    static constexpr Kind text{"Text", "txt", "text/plain", false};
    static constexpr Kind other{"File", "bin", "application/octet-stream", false};

    if (startsWith(head, "\x89PNG\r\n\x1a\n")) {
        return make(png, size);
    }
    if (startsWith(head, "\xFF\xD8\xFF")) {
        return make(jpeg, size);
    }
    if (startsWith(head, "GIF87a") || startsWith(head, "GIF89a")) {
        return make(gif, size);
    }
    if (startsWith(head, "BM")) {
        // "BM" alone is weak: the DIB header that follows has one of a few sizes.
        static const QList<quint32> dib{12, 40, 52, 56, 64, 108, 124};
        if (dib.contains(le32(head, 14))) {
            return make(bmp, size);
        }
    }
    if (startsWith(head, "RIFF") && startsWith(head, "WEBP", 8)) {
        return make(webp, size);
    }
    if (startsWith(head, "RIFF") && startsWith(head, "WAVE", 8)) {
        return make(wav, size);
    }
    if (startsWith(head, "II*\0") || startsWith(head, "MM\0*")) {
        return make(tiff, size);
    }
    if (startsWith(head, "\0\0\1\0") && head.size() >= 6 && head.at(5) == '\0') {
        return make(ico, size);
    }
    if (startsWith(head, "%PDF-")) {
        return make(pdf, size);
    }
    if (startsWith(head, "PK\x03\x04")) {
        return make(zip, size);
    }
    if (startsWith(head, "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1")) {
        return make(ole, size);
    }
    if (startsWith(head, "\x15\x1C")) {
        return make(accessOle, size); // the header Access wraps embedded objects in
    }
    if (startsWith(head, "{\\rtf")) {
        return make(rtf, size);
    }
    if (startsWith(head, "ID3") || startsWith(head, "\xFF\xFB")) {
        return make(mp3, size);
    }
    if (startsWith(head, "ftyp", 4)) {
        const QByteArray brand = head.mid(8, 4);
        return make(brand == "heic" || brand == "heix" || brand == "mif1" ? heic : mp4, size);
    }
    if (startsWith(head, "OggS")) {
        return make(ogg, size);
    }
    if (startsWith(head, "\x1F\x8B")) {
        return make(gzip, size);
    }
    if (startsWith(head, "7z\xBC\xAF\x27\x1C")) {
        return make(sevenZip, size);
    }
    const QByteArray trimmed = head.trimmed();
    if (trimmed.startsWith("<svg") || (trimmed.startsWith("<?xml") && head.contains("<svg"))) {
        return make(svg, size);
    }
    return make(looksLikeText(head) ? text : other, size);
}

QString fileSummary(qint64 size, const QByteArray &head)
{
    return QString::number(size) + QLatin1Char(':') + QString::fromLatin1(head.left(kFileSummaryHead).toHex());
}

std::optional<FileInfo> fileInfo(const QVariant &stored)
{
    if (!stored.isValid() || stored.isNull()) {
        return std::nullopt;
    }
    if (stored.metaType().id() == QMetaType::QByteArray) {
        const QByteArray bytes = stored.toByteArray();
        return sniffFile(bytes.left(kFileSummaryHead), bytes.size());
    }
    static const QRegularExpression summary(QStringLiteral("^(\\d+):([0-9a-fA-F]*)$"));
    const QRegularExpressionMatch m = summary.match(stored.toString());
    if (!m.hasMatch()) {
        return std::nullopt;
    }
    return sniffFile(QByteArray::fromHex(m.captured(2).toLatin1()), m.captured(1).toLongLong());
}

QString fileDisplayText(const FileInfo &info)
{
    return QStringLiteral("%1 · %2").arg(info.kind, formatByteSize(info.size));
}

QString formatByteSize(qint64 bytes)
{
    if (bytes < 1024) {
        return bytes == 1 ? QStringLiteral("1 byte") : QStringLiteral("%1 bytes").arg(bytes);
    }
    const double kb = double(bytes) / 1024.0;
    if (kb < 1024.0) {
        return QStringLiteral("%1 KB").arg(kb < 10 ? QString::number(kb, 'f', 1) : QString::number(qRound64(kb)));
    }
    const double mb = kb / 1024.0;
    return QStringLiteral("%1 MB").arg(mb < 10 ? QString::number(mb, 'f', 1) : QString::number(qRound64(mb)));
}

QString suggestedFileName(const QString &table, qint64 rowid, const QString &field, const FileInfo &info)
{
    QString name = QStringLiteral("%1-%2-%3").arg(table).arg(rowid).arg(field);
    static const QRegularExpression unsafe(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    name.replace(unsafe, QStringLiteral("_"));
    return name + QLatin1Char('.') + (info.extension.isEmpty() ? QStringLiteral("bin") : info.extension);
}

} // namespace QindaOffice::Base
