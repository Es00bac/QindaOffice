// SPDX-License-Identifier: GPL-3.0-or-later
// MS-OXMSG object storages as property bags (msg_properties.h).

#include "msg_properties.h"

#include "rtf_codepage.h"

#include <QTimeZone>

#include <algorithm>
#include <limits>

namespace QindaOffice::Mail::MsgDetail {
namespace {

constexpr int kEntrySize = 16;

quint32 le32(const QByteArray &bytes, qsizetype at)
{
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint64 le64(const QByteArray &bytes, qsizetype at)
{
    return quint64(le32(bytes, at)) | (quint64(le32(bytes, at + 4)) << 32);
}

quint32 tagOf(quint16 id, quint16 type)
{
    return (quint32(id) << 16) | type;
}

int headerSize(MsgObjectKind kind)
{
    switch (kind) {
    case MsgObjectKind::TopLevel: return 32;
    case MsgObjectKind::Embedded: return 24;
    case MsgObjectKind::Recipient:
    case MsgObjectKind::Attachment: return 8;
    }
    return 8;
}

} // namespace

bool MsgPropertyBag::load(const CfbReader &cfb, int storageId, MsgObjectKind kind, MsgReadBudget *budget,
                          QString *error)
{
    m_cfb = &cfb;
    m_budget = budget;
    const QString substg = QStringLiteral("__SUBSTG1.0_");
    for (const int id : cfb.children(storageId)) {
        const CfbReader::Entry *entry = cfb.entry(id);
        if (!entry) continue;
        const QString upper = CfbReader::upperName(entry->name);
        if (upper.startsWith(substg)) m_hasProperties = true;
        m_children.insert(upper, id);
    }
    const int propertiesId = m_children.value(QStringLiteral("__PROPERTIES_VERSION1.0"), -1);
    const CfbReader::Entry *properties = cfb.entry(propertiesId);
    if (!properties || properties->type != CfbReader::Entry::Stream) return true;
    if (properties->size > kMaxPropertyStreamSize) {
        if (error) *error = QStringLiteral("The property stream is larger than 1 MiB");
        return false;
    }
    QByteArray bytes;
    if (!readStream(propertiesId, &bytes, error)) return false;
    m_hasProperties = true;
    // MS-OXMSG 2.4.2: 16-byte entries — tag, flags, 8-byte value (inline for
    // fixed-size types, the size for variable-size ones). A partial trailing
    // entry is ignored.
    for (qsizetype at = headerSize(kind); at + kEntrySize <= bytes.size(); at += kEntrySize)
        m_fixed.insert(le32(bytes, at), le64(bytes, at + 8));
    return true;
}

std::optional<quint32> MsgPropertyBag::longValue(quint16 id) const
{
    const auto it = m_fixed.constFind(tagOf(id, kPtLong));
    if (it == m_fixed.cend()) return std::nullopt;
    return quint32(it.value() & 0xFFFFFFFFu);
}

bool MsgPropertyBag::boolValue(quint16 id) const
{
    // MS-OXMSG 2.4.2.1: the boolean sits in the low byte; the rest is padding.
    return (m_fixed.value(tagOf(id, kPtBoolean), 0) & 0xFFu) != 0;
}

QDateTime MsgPropertyBag::timeValue(quint16 id) const
{
    const auto it = m_fixed.constFind(tagOf(id, kPtSysTime));
    return it == m_fixed.cend() ? QDateTime() : fileTimeToDateTime(it.value());
}

QString MsgPropertyBag::string(quint16 id)
{
    QByteArray bytes;
    const int unicode = streamId(id, kPtUnicode);
    if (unicode >= 0) return readOrRecord(unicode, &bytes) ? utf16LeToString(bytes) : QString();
    const int ansi = streamId(id, kPtString8);
    if (ansi < 0 || !readOrRecord(ansi, &bytes)) return {};
    const qsizetype nul = bytes.indexOf('\0');
    if (nul >= 0) bytes.truncate(nul);
    return decodeCodePage(bytes, m_codePage);
}

QByteArray MsgPropertyBag::binary(quint16 id)
{
    QByteArray bytes;
    const int stream = streamId(id, kPtBinary);
    if (stream >= 0) readOrRecord(stream, &bytes);
    return bytes;
}

QByteArray MsgPropertyBag::optionalBinary(quint16 id)
{
    QByteArray bytes;
    const int stream = streamId(id, kPtBinary);
    if (stream >= 0 && !readStream(stream, &bytes, nullptr)) bytes.clear();
    return bytes;
}

int MsgPropertyBag::childStorage(const QString &name) const
{
    const int id = m_children.value(CfbReader::upperName(name), -1);
    const CfbReader::Entry *entry = m_cfb ? m_cfb->entry(id) : nullptr;
    return entry && entry->type == CfbReader::Entry::Storage ? id : -1;
}

QList<int> MsgPropertyBag::numberedStorages(const QString &prefix) const
{
    if (!m_cfb) return {};
    const QString upperPrefix = CfbReader::upperName(prefix);
    QList<std::pair<quint64, int>> found;
    for (auto it = m_children.cbegin(); it != m_children.cend(); ++it) {
        if (!it.key().startsWith(upperPrefix)) continue;
        const CfbReader::Entry *entry = m_cfb->entry(it.value());
        if (!entry || entry->type != CfbReader::Entry::Storage) continue;
        bool ok = false;
        const quint64 number = it.key().mid(upperPrefix.size()).toULongLong(&ok, 16);
        found.append({ok ? number : std::numeric_limits<quint64>::max(), it.value()});
    }
    std::sort(found.begin(), found.end());
    QList<int> ids;
    ids.reserve(found.size());
    for (const auto &pair : std::as_const(found)) ids.append(pair.second);
    return ids;
}

int MsgPropertyBag::streamId(quint16 id, quint16 type) const
{
    const QString name = QStringLiteral("__SUBSTG1.0_%1").arg(tagOf(id, type), 8, 16, QLatin1Char('0')).toUpper();
    const int entryId = m_children.value(name, -1);
    const CfbReader::Entry *entry = m_cfb ? m_cfb->entry(entryId) : nullptr;
    return entry && entry->type == CfbReader::Entry::Stream ? entryId : -1;
}

bool MsgPropertyBag::readStream(int entryId, QByteArray *out, QString *error)
{
    const CfbReader::Entry *entry = m_cfb->entry(entryId);
    if (entry && m_budget && entry->size > m_budget->remaining) {
        if (error) *error = QStringLiteral("The message holds more content than the reader allows");
        return false;
    }
    if (!m_cfb->read(entryId, out, error)) return false;
    if (m_budget) m_budget->remaining -= out->size();
    return true;
}

bool MsgPropertyBag::readOrRecord(int entryId, QByteArray *out)
{
    QString why;
    if (readStream(entryId, out, &why)) return true;
    if (m_error.isEmpty()) m_error = why;
    out->clear();
    return false;
}

QString utf16LeToString(const QByteArray &bytes)
{
    const qsizetype units = bytes.size() / 2;
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData());
    QString text(units, Qt::Uninitialized);
    QChar *out = text.data();
    qsizetype length = 0;
    for (qsizetype i = 0; i < units; ++i) {
        const char16_t c = char16_t(p[2 * i] | (p[2 * i + 1] << 8));
        if (c == 0) break;
        out[length++] = QChar(c);
    }
    text.truncate(length);
    return text;
}

QDateTime fileTimeToDateTime(quint64 fileTime)
{
    // 1601-01-01 → 1970-01-01 in 100 ns units.
    constexpr qint64 kUnixEpochFileTime = 116444736000000000LL;
    if (fileTime == 0 || fileTime > quint64(std::numeric_limits<qint64>::max())) return {};
    const qint64 msecs = (qint64(fileTime) - kUnixEpochFileTime) / 10000;
    const QDateTime time = QDateTime::fromMSecsSinceEpoch(msecs, QTimeZone(QTimeZone::UTC));
    // AGENT-NOTE: MAPI writes 4501-01-01 for "no date" (e.g. an unset due date).
    if (!time.isValid() || time.date().year() >= 4500) return {};
    return time;
}

} // namespace QindaOffice::Mail::MsgDetail
