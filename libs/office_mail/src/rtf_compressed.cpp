// SPDX-License-Identifier: GPL-3.0-or-later
// MS-OXRTFCP compressed-RTF decompression (rtf_compressed.h).

#include "rtf_compressed.h"

#include <array>
#include <cstring>

namespace QindaOffice::Mail {
namespace {

constexpr quint32 kTypeCompressed = 0x75465A4C;     // "LZFu" read as little-endian
constexpr quint32 kTypeUncompressed = 0x414C454D;   // "MELA"
constexpr int kHeaderSize = 16;
constexpr int kDictionarySize = 4096;
constexpr int kDictionaryMask = kDictionarySize - 1;

// MS-OXRTFCP 2.1.2.1: the dictionary's initial contents; the write offset
// starts right after it (207).
constexpr char kPrefix[] =
    "{\\rtf1\\ansi\\mac\\deff0\\deftab720{\\fonttbl;}{\\f0\\fnil \\froman \\fswiss "
    "\\fmodern \\fscript \\fdecor MS Sans SerifSymbolArialTimes New RomanCourier"
    "{\\colortbl\\red0\\green0\\blue0\r\n\\par \\pard\\plain\\f0\\fs20\\b\\i\\u\\tab\\tx";
constexpr int kPrefixSize = int(sizeof(kPrefix)) - 1;
static_assert(kPrefixSize == 207, "MS-OXRTFCP dictionary prefix must be 207 bytes");

constexpr std::array<quint32, 256> makeCrcTable()
{
    std::array<quint32, 256> table{};
    for (quint32 i = 0; i < 256; ++i) {
        quint32 c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        table[i] = c;
    }
    return table;
}
constexpr std::array<quint32, 256> kCrcTable = makeCrcTable();

quint32 le32(const QByteArray &bytes, int at)
{
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

bool fail(QByteArray *rtf, QString *error, const QString &message)
{
    rtf->clear();
    if (error) *error = message;
    return false;
}

} // namespace

quint32 rtfCompressedCrc(const QByteArray &data)
{
    quint32 crc = 0;
    for (const char byte : data) crc = kCrcTable[(crc ^ quint8(byte)) & 0xFFu] ^ (crc >> 8);
    return crc;
}

QByteArray rtfCompressionPrefix()
{
    return QByteArray(kPrefix, kPrefixSize);
}

bool decompressRtf(const QByteArray &compressed, QByteArray *rtf, QString *error)
{
    rtf->clear();
    if (compressed.size() < kHeaderSize)
        return fail(rtf, error, QStringLiteral("Compressed RTF is shorter than its 16-byte header"));
    // COMPSIZE counts the bytes after the COMPSIZE field itself.
    const qint64 compSize = le32(compressed, 0);
    const qint64 rawSize = le32(compressed, 4);
    const quint32 compType = le32(compressed, 8);
    const quint32 crc = le32(compressed, 12);
    if (compSize < kHeaderSize - 4)
        return fail(rtf, error, QStringLiteral("Compressed RTF declares an impossible size (%1)").arg(compSize));
    if (compSize + 4 > compressed.size())
        return fail(rtf, error, QStringLiteral("Compressed RTF is truncated (%1 of %2 bytes)")
                                    .arg(compressed.size()).arg(compSize + 4));
    if (rawSize > kMaxRtfRawSize)
        return fail(rtf, error, QStringLiteral("Compressed RTF declares %1 bytes of RTF, more than the 64 MiB limit")
                                    .arg(rawSize));
    const QByteArray data = compressed.mid(kHeaderSize, int(compSize + 4 - kHeaderSize));

    if (compType == kTypeUncompressed) {
        // AGENT-NOTE: MS-OXRTFCP 2.2.1.1 — MELA stores the RTF as is; the CRC
        // field is 0 by spec but is not checked (some writers leave junk).
        if (rawSize > data.size())
            return fail(rtf, error, QStringLiteral("Uncompressed RTF is truncated (%1 of %2 bytes)")
                                        .arg(data.size()).arg(rawSize));
        *rtf = data.left(int(rawSize));
        return true;
    }
    if (compType != kTypeCompressed)
        return fail(rtf, error, QStringLiteral("Unknown compressed-RTF type 0x%1")
                                    .arg(compType, 8, 16, QLatin1Char('0')));
    const quint32 actualCrc = rtfCompressedCrc(data);
    if (actualCrc != crc)
        return fail(rtf, error, QStringLiteral("Compressed RTF fails its CRC check (stored 0x%1, computed 0x%2)")
                                    .arg(crc, 8, 16, QLatin1Char('0'))
                                    .arg(actualCrc, 8, 16, QLatin1Char('0')));

    std::array<char, kDictionarySize> dictionary{};
    std::memcpy(dictionary.data(), kPrefix, kPrefixSize);
    int writeOffset = kPrefixSize;
    QByteArray out;
    out.reserve(int(rawSize));
    const auto *in = reinterpret_cast<const uchar *>(data.constData());
    const qsizetype end = data.size();
    qsizetype pos = 0;

    // AGENT-GUARD: every byte appended goes through put(), which enforces
    // RAWSIZE; a reference can therefore never inflate past the declared
    // size (at most 17 bytes per 2 input bytes, bounded by kMaxRtfRawSize).
    bool overflow = false;
    auto put = [&](char c) {
        if (out.size() >= rawSize) {
            overflow = true;
            return;
        }
        out.append(c);
        dictionary[size_t(writeOffset)] = c;
        writeOffset = (writeOffset + 1) & kDictionaryMask;
    };

    while (pos < end) {
        const uchar control = in[pos++];
        for (int bit = 0; bit < 8; ++bit) {
            if (pos >= end) break;
            if (control & (1u << bit)) {
                if (pos + 2 > end)
                    return fail(rtf, error, QStringLiteral("Compressed RTF ends inside a dictionary reference"));
                const int token = (int(in[pos]) << 8) | int(in[pos + 1]);
                pos += 2;
                const int offset = token >> 4;
                const int length = (token & 0xF) + 2;
                // MS-OXRTFCP 2.2.2: a reference to the current write offset
                // is the end marker.
                if (offset == writeOffset) {
                    *rtf = out;
                    return true;
                }
                for (int i = 0; i < length && !overflow; ++i)
                    put(dictionary[size_t((offset + i) & kDictionaryMask)]);
            } else {
                put(char(in[pos++]));
            }
            if (overflow)
                return fail(rtf, error, QStringLiteral("Compressed RTF expands beyond its declared %1 bytes")
                                            .arg(rawSize));
        }
    }
    // AGENT-NOTE: the reference compressor always writes the end marker;
    // accept a stream without one only when it produced exactly RAWSIZE.
    if (out.size() != rawSize)
        return fail(rtf, error, QStringLiteral("Compressed RTF ends before its end marker (%1 of %2 bytes)")
                                    .arg(out.size()).arg(rawSize));
    *rtf = out;
    return true;
}

} // namespace QindaOffice::Mail
