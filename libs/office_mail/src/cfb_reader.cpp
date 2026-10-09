// SPDX-License-Identifier: GPL-3.0-or-later
// MS-CFB compound file reader (cfb_reader.h).

#include "cfb_reader.h"

#include <QBitArray>

#include <climits>
#include <cstring>
#include <limits>

namespace QindaOffice::Mail {
namespace {

// MS-CFB 2.1: special sector numbers and directory ids.
constexpr quint32 kMaxRegSect = 0xFFFFFFFA;
constexpr quint32 kEndOfChain = 0xFFFFFFFE;
constexpr quint32 kFreeSect = 0xFFFFFFFF;
constexpr quint32 kNoStream = 0xFFFFFFFF;

constexpr int kHeaderSize = 512;
constexpr int kHeaderDifatEntries = 109;
constexpr qint64 kDirEntrySize = 128;
constexpr qint64 kMiniSectorSize = 64;
constexpr qint64 kMiniStreamCutoff = 4096;
constexpr uchar kSignature[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};

quint16 le16(const QByteArray &bytes, qint64 at)
{
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + at);
    return quint16(p[0] | (p[1] << 8));
}

quint32 le32(const QByteArray &bytes, qint64 at)
{
    const auto *p = reinterpret_cast<const uchar *>(bytes.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

bool setError(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

// A directory link: NOSTREAM → -1; anything else is kept (clamped to
// INT_MAX) and range-checked when the tree is walked.
int directoryLink(quint32 value)
{
    if (value == kNoStream) return -1;
    return value > quint32(INT_MAX) ? INT_MAX : int(value);
}

} // namespace

QString CfbReader::upperName(const QString &name)
{
    QString upper = name;
    for (QChar &c : upper) {
        const char32_t mapped = QChar::toUpper(char32_t(c.unicode()));
        if (mapped <= 0xFFFF) c = QChar(char16_t(mapped));
    }
    return upper;
}

bool CfbReader::fail(const QString &message, QString *error)
{
    *this = CfbReader();
    return setError(error, message);
}

bool CfbReader::open(const QByteArray &file, QString *error)
{
    *this = CfbReader();
    if (file.size() > kMaxFileSize) return fail(QStringLiteral("The compound file is larger than 128 MiB"), error);
    if (file.size() < kHeaderSize)
        return fail(QStringLiteral("The compound file header is truncated (%1 bytes)").arg(file.size()), error);
    if (std::memcmp(file.constData(), kSignature, sizeof kSignature) != 0)
        return fail(QStringLiteral("Not a compound file (bad signature)"), error);
    const quint16 major = le16(file, 0x1A);
    const quint16 sectorShift = le16(file, 0x1E);
    if (le16(file, 0x1C) != 0xFFFE) return fail(QStringLiteral("The compound file has a bad byte-order mark"), error);
    if (!((major == 3 && sectorShift == 9) || (major == 4 && sectorShift == 12)))
        return fail(QStringLiteral("Unsupported compound file version %1 with sector shift %2")
                        .arg(major).arg(sectorShift), error);
    if (le16(file, 0x20) != 6) return fail(QStringLiteral("The compound file has an unsupported mini sector size"), error);
    if (le32(file, 0x38) != kMiniStreamCutoff)
        return fail(QStringLiteral("The compound file has an unsupported mini stream cutoff"), error);

    m_file = file;
    m_major = major;
    m_shift = sectorShift;
    m_sectorSize = qint64(1) << sectorShift;
    if (file.size() < m_sectorSize) return fail(QStringLiteral("The compound file header sector is truncated"), error);
    // AGENT-NOTE: a partial last sector is counted; reads check the bytes
    // they actually need, so a short final stream sector still reads.
    m_sectorCount = (file.size() - m_sectorSize + m_sectorSize - 1) >> m_shift;
    m_fatSectorCount = le32(file, 0x2C);
    m_difatStart = le32(file, 0x44);

    QString why;
    if (!loadFat(&why) || !loadDirectory(le32(file, 0x30), &why) || !loadMiniStream(le32(file, 0x3C), &why)
        || !buildTree(&why))
        return fail(why, error);
    m_open = true;
    return true;
}

bool CfbReader::loadFat(QString *why)
{
    if (m_fatSectorCount > m_sectorCount)
        return setError(why, QStringLiteral("The FAT claims %1 sectors but the file holds %2")
                                 .arg(m_fatSectorCount).arg(m_sectorCount));
    QList<quint32> fatSectors;
    fatSectors.reserve(qsizetype(m_fatSectorCount));
    for (int i = 0; i < kHeaderDifatEntries && quint32(fatSectors.size()) < m_fatSectorCount; ++i)
        fatSectors.append(le32(m_file, 0x4C + 4 * i));

    // MS-CFB 2.5: each DIFAT sector holds (sector size / 4 - 1) FAT sector
    // numbers followed by the next DIFAT sector.
    const qint64 perDifat = m_sectorSize / 4 - 1;
    QBitArray seen(static_cast<qsizetype>(m_sectorCount));
    quint32 next = m_difatStart;
    while (quint32(fatSectors.size()) < m_fatSectorCount) {
        if (next == kEndOfChain || next == kFreeSect)
            return setError(why, QStringLiteral("The DIFAT lists only %1 of %2 FAT sectors")
                                     .arg(fatSectors.size()).arg(m_fatSectorCount));
        if (next >= m_sectorCount) return setError(why, QStringLiteral("DIFAT sector %1 is out of range").arg(next));
        if (seen.testBit(int(next))) return setError(why, QStringLiteral("The DIFAT chain loops at sector %1").arg(next));
        seen.setBit(int(next));
        const qint64 at = sectorOffset(next, m_sectorSize);
        if (at < 0) return setError(why, QStringLiteral("DIFAT sector %1 is truncated").arg(next));
        for (qint64 i = 0; i < perDifat && quint32(fatSectors.size()) < m_fatSectorCount; ++i)
            fatSectors.append(le32(m_file, at + 4 * i));
        next = le32(m_file, at + 4 * perDifat);
    }

    // AGENT-GUARD: FAT entries are kept only for sectors that exist, so a
    // FAT that claims more cannot inflate memory, and any chain step past
    // the file fails the range check in fatChain().
    const qint64 perFat = m_sectorSize / 4;
    m_fat.reserve(qsizetype(qMin<qint64>(qint64(m_fatSectorCount) * perFat, m_sectorCount)));
    for (const quint32 sector : std::as_const(fatSectors)) {
        if (sector >= m_sectorCount) return setError(why, QStringLiteral("FAT sector %1 is out of range").arg(sector));
        const qint64 at = sectorOffset(sector, m_sectorSize);
        if (at < 0) return setError(why, QStringLiteral("FAT sector %1 is truncated").arg(sector));
        for (qint64 i = 0; i < perFat && m_fat.size() < m_sectorCount; ++i) m_fat.append(le32(m_file, at + 4 * i));
    }
    return true;
}

bool CfbReader::loadDirectory(quint32 firstSector, QString *why)
{
    QList<quint32> chain;
    if (!fatChain(firstSector, -1, &chain, QStringLiteral("The directory"), why)) return false;
    const qint64 perSector = m_sectorSize / kDirEntrySize;
    if (chain.isEmpty()) return setError(why, QStringLiteral("The compound file has no directory"));
    if (chain.size() * perSector > kMaxEntries)
        return setError(why, QStringLiteral("The directory has more than %1 entries").arg(kMaxEntries));
    m_entries.reserve(qsizetype(chain.size() * perSector));
    m_upperNames.reserve(qsizetype(chain.size() * perSector));
    for (const quint32 sector : std::as_const(chain)) {
        const qint64 at = sectorOffset(sector, m_sectorSize);
        if (at < 0) return setError(why, QStringLiteral("Directory sector %1 is truncated").arg(sector));
        for (qint64 k = 0; k < perSector; ++k) {
            const qint64 e = at + k * kDirEntrySize;
            Entry entry;
            const quint8 type = quint8(m_file.at(e + 66));
            // MS-CFB 2.6.1: 0 unallocated, 1 storage, 2 stream, 5 root.
            if (type == 1 || type == 2 || type == 5) {
                entry.id = int(m_entries.size());
                entry.type = type == 1 ? Entry::Storage : type == 2 ? Entry::Stream : Entry::Root;
                const int nameBytes = qMin(int(le16(m_file, e + 64)), 64);
                const int units = nameBytes >= 2 ? nameBytes / 2 - 1 : 0;
                for (int u = 0; u < units; ++u) {
                    const char16_t c = le16(m_file, e + 2 * u);
                    if (c == 0) break;
                    entry.name.append(QChar(c));
                }
                entry.left = directoryLink(le32(m_file, e + 68));
                entry.right = directoryLink(le32(m_file, e + 72));
                entry.child = directoryLink(le32(m_file, e + 76));
                entry.startSector = le32(m_file, e + 116);
                const quint64 low = le32(m_file, e + 120);
                const quint64 high = le32(m_file, e + 124);
                // MS-CFB 2.6.3: version 3 writers may leave garbage in the high dword.
                const quint64 raw = m_major == 3 ? low : ((high << 32) | low);
                entry.size = raw > quint64(std::numeric_limits<qint64>::max())
                    ? std::numeric_limits<qint64>::max() : qint64(raw);
            }
            m_upperNames.append(entry.id >= 0 ? upperName(entry.name) : QString());
            m_entries.append(entry);
        }
    }
    if (m_entries.first().id != 0 || m_entries.first().type != Entry::Root)
        return setError(why, QStringLiteral("The compound file has no root entry"));
    return true;
}

bool CfbReader::loadMiniStream(quint32 miniFatStart, QString *why)
{
    // MS-CFB 2.6.2: the root entry's start sector and size describe the mini stream.
    const Entry &root = m_entries.first();
    if (root.size > kMaxStreamSize) return setError(why, QStringLiteral("The mini stream is larger than 64 MiB"));
    if (root.size > 0
        && !readRegular(root.startSector, root.size, &m_miniStream, QStringLiteral("The mini stream"), why))
        return false;
    if (miniFatStart == kEndOfChain || miniFatStart == kFreeSect) return true;
    QList<quint32> chain;
    if (!fatChain(miniFatStart, -1, &chain, QStringLiteral("The mini FAT"), why)) return false;
    // Entries past the end of the mini stream could only point outside it.
    const qint64 usable = (m_miniStream.size() + kMiniSectorSize - 1) / kMiniSectorSize;
    const qint64 perSector = m_sectorSize / 4;
    for (const quint32 sector : std::as_const(chain)) {
        if (m_miniFat.size() >= usable) break;
        const qint64 at = sectorOffset(sector, m_sectorSize);
        if (at < 0) return setError(why, QStringLiteral("Mini FAT sector %1 is truncated").arg(sector));
        for (qint64 i = 0; i < perSector && m_miniFat.size() < usable; ++i) m_miniFat.append(le32(m_file, at + 4 * i));
    }
    return true;
}

bool CfbReader::buildTree(QString *why)
{
    // AGENT-GUARD: iterative in-order walk of every storage's sibling tree.
    // `seen` spans the whole directory, so a cycle, a node shared by two
    // parents or a child pointing back up is an error, and the walk is
    // bounded by the number of entries.
    const qsizetype count = m_entries.size();
    m_children.resize(count);
    QBitArray seen(static_cast<qsizetype>(count));
    seen.setBit(0);
    QList<int> storages{0};
    QList<int> stack;
    while (!storages.isEmpty()) {
        const int storage = storages.takeLast();
        QList<int> order;
        stack.clear();
        int current = m_entries.at(storage).child;
        while (current != -1 || !stack.isEmpty()) {
            while (current != -1) {
                if (current < 0 || current >= count || m_entries.at(current).id < 0)
                    return setError(why, QStringLiteral("The directory links to a missing entry (%1)").arg(current));
                if (seen.testBit(current))
                    return setError(why, QStringLiteral("The directory tree loops at entry %1").arg(current));
                if (m_entries.at(current).type == Entry::Root)
                    return setError(why, QStringLiteral("The root entry appears inside the directory tree"));
                seen.setBit(current);
                stack.append(current);
                current = m_entries.at(current).left;
            }
            current = stack.takeLast();
            order.append(current);
            if (m_entries.at(current).type == Entry::Storage) storages.append(current);
            current = m_entries.at(current).right;
        }
        m_children[storage] = order;
    }
    return true;
}

qint64 CfbReader::sectorOffset(quint32 sector, qint64 bytes) const
{
    if (sector > kMaxRegSect) return -1;
    const qint64 at = (qint64(sector) + 1) << m_shift;
    return at + bytes <= m_file.size() ? at : -1;
}

bool CfbReader::fatChain(quint32 start, qint64 need, QList<quint32> *chain, const QString &what,
                         QString *error) const
{
    // `need` < 0: follow to ENDOFCHAIN; otherwise collect exactly `need`
    // sectors (a longer chain is tolerated, a shorter one is an error).
    chain->clear();
    QBitArray seen(static_cast<qsizetype>(m_fat.size()));
    quint32 sector = start;
    while (need < 0 || chain->size() < need) {
        if (sector == kEndOfChain) {
            if (need < 0) return true;
            return setError(error, QStringLiteral("%1 is truncated: its sector chain ends after %2 of %3 sectors")
                                       .arg(what).arg(chain->size()).arg(need));
        }
        if (sector > kMaxRegSect || sector >= quint32(m_fat.size()))
            return setError(error, QStringLiteral("%1 refers to sector %2, which is out of range").arg(what).arg(sector));
        if (seen.testBit(int(sector)))
            return setError(error, QStringLiteral("%1 has a sector chain that loops at sector %2").arg(what).arg(sector));
        seen.setBit(int(sector));
        chain->append(sector);
        sector = m_fat.at(sector);
    }
    return true;
}

bool CfbReader::readRegular(quint32 start, qint64 size, QByteArray *out, const QString &what,
                            QString *error) const
{
    out->clear();
    QList<quint32> chain;
    if (!fatChain(start, (size + m_sectorSize - 1) >> m_shift, &chain, what, error)) return false;
    // The chain proved that many sectors exist, so this allocation is
    // bounded by the file size.
    out->resize(qsizetype(size));
    qint64 done = 0;
    for (const quint32 sector : std::as_const(chain)) {
        const qint64 bytes = qMin(m_sectorSize, size - done);
        const qint64 at = sectorOffset(sector, bytes);
        if (at < 0) {
            out->clear();
            return setError(error, QStringLiteral("%1 is truncated at sector %2").arg(what).arg(sector));
        }
        std::memcpy(out->data() + done, m_file.constData() + at, size_t(bytes));
        done += bytes;
    }
    return true;
}

bool CfbReader::readMini(quint32 start, qint64 size, QByteArray *out, const QString &what,
                         QString *error) const
{
    out->clear();
    const qint64 need = (size + kMiniSectorSize - 1) / kMiniSectorSize;
    if (need > m_miniFat.size())
        return setError(error, QStringLiteral("%1 does not fit in the mini stream").arg(what));
    QBitArray seen(static_cast<qsizetype>(m_miniFat.size()));
    QByteArray data(qsizetype(size), Qt::Uninitialized);
    quint32 sector = start;
    qint64 done = 0;
    for (qint64 i = 0; i < need; ++i) {
        if (sector == kEndOfChain)
            return setError(error, QStringLiteral("%1 is truncated: its mini sector chain ends early").arg(what));
        const qint64 at = qint64(sector) * kMiniSectorSize;
        const qint64 bytes = qMin(kMiniSectorSize, size - done);
        if (sector > kMaxRegSect || sector >= quint32(m_miniFat.size()) || at + bytes > m_miniStream.size())
            return setError(error, QStringLiteral("%1 refers to mini sector %2, outside the mini stream")
                                       .arg(what).arg(sector));
        if (seen.testBit(int(sector)))
            return setError(error, QStringLiteral("%1 has a mini sector chain that loops at %2").arg(what).arg(sector));
        seen.setBit(int(sector));
        std::memcpy(data.data() + done, m_miniStream.constData() + at, size_t(bytes));
        done += bytes;
        sector = m_miniFat.at(sector);
    }
    *out = data;
    return true;
}

bool CfbReader::read(int streamId, QByteArray *out, QString *error) const
{
    out->clear();
    const Entry *e = entry(streamId);
    if (!e || e->type != Entry::Stream)
        return setError(error, QStringLiteral("Directory entry %1 is not a stream").arg(streamId));
    const QString what = QStringLiteral("Stream \"%1\"").arg(e->name);
    if (e->size > kMaxStreamSize) return setError(error, QStringLiteral("%1 is larger than 64 MiB").arg(what));
    if (e->size == 0) return true;
    if (e->size < kMiniStreamCutoff) return readMini(e->startSector, e->size, out, what, error);
    return readRegular(e->startSector, e->size, out, what, error);
}

QList<int> CfbReader::children(int storageId) const
{
    if (!m_open || storageId < 0 || storageId >= m_children.size()) return {};
    return m_children.at(storageId);
}

int CfbReader::child(int storageId, const QString &name) const
{
    const QString upper = upperName(name);
    for (const int id : children(storageId)) {
        if (m_upperNames.at(id) == upper) return id;
    }
    return -1;
}

const CfbReader::Entry *CfbReader::entry(int id) const
{
    if (!m_open || id < 0 || id >= m_entries.size() || m_entries.at(id).id < 0) return nullptr;
    return &m_entries.at(id);
}

} // namespace QindaOffice::Mail
