// SPDX-License-Identifier: GPL-3.0-or-later
// Named-property map shared by .msg and .pst (mapi_named.h).

#include "mapi_named.h"

#include <QList>

namespace QindaOffice::Mail::Mapi {
namespace {

quint16 le16(const QByteArray &b, qsizetype at)
{
    const auto *p = reinterpret_cast<const uchar *>(b.constData() + at);
    return quint16(p[0] | (p[1] << 8));
}

quint32 le32(const QByteArray &b, qsizetype at)
{
    const auto *p = reinterpret_cast<const uchar *>(b.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

} // namespace

QUuid guidFromBytes(const QByteArray &bytes, qsizetype at)
{
    if (at < 0 || at + 16 > bytes.size()) return {};
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + at);
    return QUuid(le32(bytes, at), le16(bytes, at + 4), le16(bytes, at + 6), p[8], p[9], p[10], p[11], p[12], p[13],
                 p[14], p[15]);
}

QByteArray guidBytes(const QUuid &guid)
{
    QByteArray out(16, '\0');
    for (int i = 0; i < 4; ++i) out[i] = char((guid.data1 >> (8 * i)) & 0xFF);
    out[4] = char(guid.data2 & 0xFF);
    out[5] = char(guid.data2 >> 8);
    out[6] = char(guid.data3 & 0xFF);
    out[7] = char(guid.data3 >> 8);
    for (int i = 0; i < 8; ++i) out[8 + i] = char(guid.data4[i]);
    return out;
}

void NamedPropertyMap::parse(const QByteArray &guids, const QByteArray &entries, const QByteArray &strings)
{
    m_numeric.clear();
    m_names.clear();
    QList<QUuid> sets;
    for (qsizetype at = 0; at + 16 <= guids.size() && sets.size() < kMaxEntries; at += 16) sets.append(guidFromBytes(guids, at));
    const qsizetype count = qMin<qsizetype>(entries.size() / 8, kMaxEntries);
    for (qsizetype i = 0; i < count; ++i) {
        const qsizetype at = i * 8;
        const quint32 identifier = le32(entries, at);
        const quint16 kind = le16(entries, at + 4);
        const quint16 index = le16(entries, at + 6);
        const int guidIndex = kind >> 1;
        QUuid set;
        if (guidIndex == 1) set = kPsMapi;
        else if (guidIndex == 2) set = kPsPublicStrings;
        else if (guidIndex >= 3 && guidIndex - 3 < sets.size()) set = sets.at(guidIndex - 3);
        else continue;
        if (index > 0x7FFE) continue;
        const quint16 id = quint16(0x8000 + index);
        if ((kind & 1) == 0) {
            m_numeric.insert({set, identifier}, id);
            continue;
        }
        // A string name: its byte length, then UTF-16LE.
        if (qint64(identifier) + 4 > strings.size()) continue;
        const quint32 length = le32(strings, identifier);
        if (length % 2 != 0 || length > 1024 || qint64(identifier) + 4 + length > strings.size()) continue;
        const auto *units = reinterpret_cast<const char16_t *>(strings.constData() + identifier + 4);
        QString name;
        name.reserve(qsizetype(length / 2));
        for (quint32 u = 0; u < length / 2; ++u) {
            const uchar *p = reinterpret_cast<const uchar *>(units) + 2 * u;
            name.append(QChar(char16_t(p[0] | (p[1] << 8))));
        }
        m_names.insert({set, name}, id);
    }
}

std::optional<quint16> NamedPropertyMap::find(const QUuid &set, quint32 lid) const
{
    const auto it = m_numeric.constFind({set, lid});
    if (it == m_numeric.cend()) return std::nullopt;
    return it.value();
}

std::optional<quint16> NamedPropertyMap::find(const QUuid &set, const QString &name) const
{
    const auto it = m_names.constFind({set, name});
    if (it == m_names.cend()) return std::nullopt;
    return it.value();
}

} // namespace QindaOffice::Mail::Mapi
