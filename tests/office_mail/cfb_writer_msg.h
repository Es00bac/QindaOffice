// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support (header-only) for tst_mail_msg: an Outlook .msg builder on
// top of cfb_writer.h (MS-OXMSG property streams with the right header
// size per object, "__substg1.0_PPPPTTTT" streams, recipient and
// attachment storages, embedded message storages), plus compressed-RTF
// blob builders: MELA (stored) and a small greedy LZFu compressor.

#include "cfb_writer.h"
#include "rtf_compressed.h"

#include <QDateTime>
#include <QPair>

#include <memory>
#include <tuple>

namespace QindaOffice::Mail::Test {

inline constexpr quint16 kMsgPtLong = 0x0003;
inline constexpr quint16 kMsgPtBoolean = 0x000B;
inline constexpr quint16 kMsgPtString8 = 0x001E;
inline constexpr quint16 kMsgPtUnicode = 0x001F;
inline constexpr quint16 kMsgPtSysTime = 0x0040;
inline constexpr quint16 kMsgPtBinary = 0x0102;

inline QByteArray msgUtf16Le(const QString &text)
{
    QByteArray bytes;
    bytes.reserve((text.size() + 1) * 2);
    for (const QChar c : text) {
        bytes.append(char(c.unicode() & 0xFF));
        bytes.append(char(c.unicode() >> 8));
    }
    bytes.append(2, '\0');   // terminator, as Outlook writes it
    return bytes;
}

inline QString msgHex8(quint32 value)
{
    return QString::number(value, 16).rightJustified(8, QLatin1Char('0')).toUpper();
}

inline QString msgStreamName(quint16 id, quint16 type)
{
    return QStringLiteral("__substg1.0_") + msgHex8((quint32(id) << 16) | type);
}

// The property-bag part shared by messages, recipients and attachments.
struct MsgProps {
    QList<QPair<quint32, quint64>> fixed;      // tag → 8-byte value
    QList<QPair<quint32, QByteArray>> streams; // tag → stream bytes
    // M76: multi-valued values, one "__substg1.0_<tag>-<index>" stream each.
    QList<std::tuple<quint32, quint32, QByteArray>> indexed;

    MsgProps &unicode(quint16 id, const QString &text) { return variable(id, kMsgPtUnicode, msgUtf16Le(text)); }
    MsgProps &string8(quint16 id, const QByteArray &bytes) { return variable(id, kMsgPtString8, bytes + '\0'); }
    MsgProps &binary(quint16 id, const QByteArray &bytes) { return variable(id, kMsgPtBinary, bytes); }
    MsgProps &longValue(quint16 id, quint32 value)
    {
        fixed.append({(quint32(id) << 16) | kMsgPtLong, value});
        return *this;
    }
    MsgProps &boolValue(quint16 id, bool value)
    {
        fixed.append({(quint32(id) << 16) | kMsgPtBoolean, value ? 1u : 0u});
        return *this;
    }
    MsgProps &time(quint16 id, const QDateTime &when)
    {
        const quint64 fileTime = quint64(when.toMSecsSinceEpoch() * 10000 + 116444736000000000LL);
        fixed.append({(quint32(id) << 16) | kMsgPtSysTime, fileTime});
        return *this;
    }
    // MS-OXMSG 2.1.4.2.3: a length stream (8 bytes per binary value) and one
    // stream per value.
    MsgProps &mvBinary(quint16 id, const QList<QByteArray> &values)
    {
        const quint32 tag = (quint32(id) << 16) | 0x1102;
        QByteArray lengths;
        for (qsizetype i = 0; i < values.size(); ++i) {
            QByteArray entry(8, '\0');
            cfbPut32(entry, 0, quint32(values.at(i).size()));
            lengths += entry;
            indexed.append({tag, quint32(i), values.at(i)});
        }
        return variable(id, 0x1102, lengths);
    }
    MsgProps &variable(quint16 id, quint16 type, const QByteArray &bytes)
    {
        const quint32 tag = (quint32(id) << 16) | type;
        streams.append({tag, bytes});
        fixed.append({tag, quint64(bytes.size())});   // variable entries carry their size
        return *this;
    }

    // "__properties_version1.0" + the substg streams. `headerSize`: 32 for
    // the top-level message, 24 for an embedded one, 8 otherwise.
    void appendNodes(QList<CfbNode> *out, int headerSize, quint32 recipients, quint32 attachments) const
    {
        QByteArray properties(headerSize, '\0');
        if (headerSize >= 24) {
            cfbPut32(properties, 8, recipients);    // next recipient id
            cfbPut32(properties, 12, attachments);  // next attachment id
            cfbPut32(properties, 16, recipients);
            cfbPut32(properties, 20, attachments);
        }
        for (const auto &entry : fixed) {
            QByteArray record(16, '\0');
            cfbPut32(record, 0, entry.first);
            cfbPut32(record, 4, 0x6);   // PROPATTR_READABLE | PROPATTR_WRITABLE
            cfbPut64(record, 8, entry.second);
            properties += record;
        }
        out->append(CfbNode::stream(QStringLiteral("__properties_version1.0"), properties));
        for (const auto &stream : streams)
            out->append(CfbNode::stream(QStringLiteral("__substg1.0_") + msgHex8(stream.first), stream.second));
        for (const auto &[tag, index, bytes] : indexed)
            out->append(CfbNode::stream(QStringLiteral("__substg1.0_") + msgHex8(tag) + QLatin1Char('-') + msgHex8(index), bytes));
    }
};

struct MsgBuilder;

struct MsgAttachmentBuilder : MsgProps {
    std::shared_ptr<MsgBuilder> embedded;   // attach method 5: "__substg1.0_3701000D"
};

struct MsgBuilder : MsgProps {
    QList<MsgProps> recipients;
    QList<MsgAttachmentBuilder> attachments;
    QList<CfbNode> extraNodes;
    // M76: the named-property streams of "__nameid_version1.0" (GUIDs,
    // NAMEID entries, names), empty unless a test needs named properties.
    QByteArray nameGuids, nameEntries, nameStrings;

    QList<CfbNode> nodes(bool topLevel) const
    {
        QList<CfbNode> out;
        appendNodes(&out, topLevel ? 32 : 24, quint32(recipients.size()), quint32(attachments.size()));
        for (qsizetype i = 0; i < recipients.size(); ++i) {
            QList<CfbNode> children;
            recipients.at(i).appendNodes(&children, 8, 0, 0);
            out.append(CfbNode::folder(QStringLiteral("__recip_version1.0_#") + msgHex8(quint32(i)), children));
        }
        for (qsizetype i = 0; i < attachments.size(); ++i) {
            QList<CfbNode> children;
            attachments.at(i).appendNodes(&children, 8, 0, 0);
            if (attachments.at(i).embedded)
                children.append(CfbNode::folder(QStringLiteral("__substg1.0_3701000D"), attachments.at(i).embedded->nodes(false)));
            out.append(CfbNode::folder(QStringLiteral("__attach_version1.0_#") + msgHex8(quint32(i)), children));
        }
        if (topLevel) {
            out.append(CfbNode::folder(QStringLiteral("__nameid_version1.0"),
                                       {CfbNode::stream(QStringLiteral("__substg1.0_00020102"), nameGuids),
                                        CfbNode::stream(QStringLiteral("__substg1.0_00030102"), nameEntries),
                                        CfbNode::stream(QStringLiteral("__substg1.0_00040102"), nameStrings)}));
        }
        out += extraNodes;
        return out;
    }

    QByteArray build(const CfbBuildOptions &options = {}, CfbLayout *layout = nullptr) const
    {
        return buildCfb(CfbNode::folder(QStringLiteral("Root Entry"), nodes(true)), options, layout);
    }
};

inline QByteArray rtfBlobHeader(qsizetype payloadSize, qsizetype rawSize, quint32 type, quint32 crc)
{
    QByteArray header(16, '\0');
    cfbPut32(header, 0, quint32(payloadSize + 12));
    cfbPut32(header, 4, quint32(rawSize));
    cfbPut32(header, 8, type);
    cfbPut32(header, 12, crc);
    return header;
}

// MS-OXRTFCP "MELA": the RTF stored uncompressed.
inline QByteArray storeRtfMela(const QByteArray &raw)
{
    return rtfBlobHeader(raw.size(), raw.size(), 0x414C454D, 0) + raw;
}

// MS-OXRTFCP "LZFu", greedy longest match over the whole 4096-byte
// dictionary. It simulates the decoder exactly: a reference may overlap the
// bytes it is writing, never points at the write offset (that is the end
// marker) and never reads dictionary bytes that were not written yet.
inline QByteArray compressRtfLzfu(const QByteArray &raw)
{
    const QByteArray prefix = rtfCompressionPrefix();
    QByteArray dictionary(4096, '\0');
    std::memcpy(dictionary.data(), prefix.constData(), size_t(prefix.size()));
    int write = int(prefix.size());
    qint64 written = prefix.size();
    QByteArray data;
    qsizetype controlAt = 0;
    int bit = 8;
    auto token = [&](bool reference) {
        if (bit == 8) {
            controlAt = data.size();
            data.append('\0');
            bit = 0;
        }
        if (reference) data[controlAt] = char(uchar(data[controlAt]) | (1u << bit));
        ++bit;
    };
    auto put = [&](char c) {
        dictionary[write] = c;
        write = (write + 1) & 0xFFF;
        ++written;
    };
    qsizetype i = 0;
    while (i < raw.size()) {
        const int maxLength = int(qMin<qsizetype>(17, raw.size() - i));
        int bestLength = 0;
        int bestOffset = 0;
        for (int offset = 0; offset < 4096 && maxLength >= 2; ++offset) {
            if (offset == write) continue;
            int length = 0;
            while (length < maxLength) {
                const int source = (offset + length) & 0xFFF;
                const int distance = (source - write) & 0xFFF;
                char c;
                if (distance < length) {
                    c = raw[i + distance];
                } else {
                    if (written < 4096 && source >= write) break;
                    c = dictionary[source];
                }
                if (c != raw[i + length]) break;
                ++length;
            }
            if (length > bestLength) {
                bestLength = length;
                bestOffset = offset;
                if (length == maxLength) break;
            }
        }
        if (bestLength >= 2) {
            token(true);
            const int value = (bestOffset << 4) | (bestLength - 2);
            data.append(char(value >> 8));
            data.append(char(value & 0xFF));
            for (int k = 0; k < bestLength; ++k) put(raw[i + k]);
            i += bestLength;
        } else {
            token(false);
            data.append(raw[i]);
            put(raw[i]);
            ++i;
        }
    }
    token(true);   // end marker: a reference to the write offset
    data.append(char((write << 4) >> 8));
    data.append(char((write << 4) & 0xFF));
    return rtfBlobHeader(data.size(), raw.size(), 0x75465A4C, rtfCompressedCrc(data)) + data;
}

} // namespace QindaOffice::Mail::Test
