// SPDX-License-Identifier: GPL-3.0-or-later
// MS-PST node database layer (pst_ndb.h).

#include "pst_ndb.h"

#include "pst_crypt.h"

#include <QFile>
#include <QIODevice>

namespace QindaOffice::Mail::Pst {
namespace {

constexpr qint64 kPageSize = 512;
constexpr qint64 kBlockAlign = 64;
constexpr qint64 kMaxBlockSize = 8192;
constexpr quint8 kPtypeBbt = 0x80;
constexpr quint8 kPtypeNbt = 0x81;
constexpr quint64 kBidReserved = 1;   // MS-PST 2.2.2.2: readers ignore bit 0
constexpr quint64 kBidInternal = 2;

// Format-dependent sizes (MS-PST 2.2.2.7, 2.2.2.8).
struct Sizes {
    int bid, pageTrailer, pageEntries, btEntry, bbtEntry, nbtEntry, blockTrailer, slEntry, siEntry, subHeader;
};
constexpr Sizes kUnicode{8, 16, 488, 24, 24, 32, 16, 24, 16, 8};
constexpr Sizes kAnsi{4, 12, 496, 12, 12, 16, 12, 12, 8, 4};

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

quint64 le64(const QByteArray &b, qsizetype at)
{
    return quint64(le32(b, at)) | (quint64(le32(b, at + 4)) << 32);
}

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

QString hex(quint64 value)
{
    return QStringLiteral("0x%1").arg(value, 0, 16);
}

} // namespace

NodeDatabase::NodeDatabase()
    : m_pages(512)
{
}

NodeDatabase::~NodeDatabase() = default;

int NodeDatabase::maxBlockData() const
{
    return int(kMaxBlockSize) - (unicode() ? kUnicode : kAnsi).blockTrailer;
}

void NodeDatabase::close()
{
    m_device = nullptr;
    m_file.reset();
    m_pages.clear();
    m_size = 0;
}

bool NodeDatabase::open(const QString &path, QString *error)
{
    close();
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) return fail(error, QStringLiteral("Cannot read %1: %2").arg(path, file->errorString()));
    m_file = std::move(file);
    m_device = m_file.get();
    m_size = m_device->size();
    if (!readHeader(error)) {
        close();
        return false;
    }
    return true;
}

bool NodeDatabase::open(QIODevice *device, QString *error)
{
    close();
    if (device == nullptr || !device->isReadable() || device->isSequential())
        return fail(error, QStringLiteral("The data file cannot be read"));
    m_device = device;
    m_size = device->size();
    if (!readHeader(error)) {
        close();
        return false;
    }
    return true;
}

QByteArray NodeDatabase::readAt(qint64 offset, qint64 size)
{
    if (offset < 0 || size < 0 || offset > m_size || size > m_size - offset || !m_device->seek(offset)) return {};
    return m_device->read(size);
}

quint64 NodeDatabase::field(const QByteArray &bytes, qsizetype at) const
{
    return unicode() ? le64(bytes, at) : le32(bytes, at);
}

bool NodeDatabase::crcMatches(const QByteArray &bytes, qsizetype size, quint32 stored) const
{
    // AGENT-NOTE: Outlook 97–2002 wrote ANSI files without block and page
    // CRCs (the field is zero); a zero CRC in an ANSI file is "not computed".
    if (!unicode() && stored == 0) return true;
    return crc32(bytes.constData(), size) == stored;
}

bool NodeDatabase::readHeader(QString *error)
{
    const QByteArray head = readAt(0, qMin<qint64>(m_size, 564));
    if (head.size() < 512) return fail(error, QStringLiteral("The file is too small to be an Outlook data file"));
    if (!head.startsWith("!BDN")) return fail(error, QStringLiteral("Not an Outlook data file (bad signature)"));
    const quint16 client = le16(head, 8);
    if (client != 0x4D53 && client != 0x4F53) return fail(error, QStringLiteral("Not an Outlook data file (unknown client signature)"));
    m_ost = client == 0x4F53;
    const quint16 version = le16(head, 10);
    if (version == 36)
        return fail(error, QStringLiteral("This file uses the 4 KB pages of Outlook 2013's offline cache (.ost), "
                                          "which QindaMail does not read; export a .pst from Outlook instead"));
    if (version == 14 || version == 15) m_format = Format::Ansi;
    else if (version >= 23) m_format = Format::Unicode;
    else return fail(error, QStringLiteral("Unsupported Outlook data file version %1").arg(version));
    if (unicode() && head.size() < 564) return fail(error, QStringLiteral("The file header is truncated"));
    if (crc32(head.constData() + 8, 471) != le32(head, 4)
        || (unicode() && crc32(head.constData() + 8, 516) != le32(head, 524)))
        return fail(error, QStringLiteral("The file header is damaged (checksum mismatch)"));

    quint64 eof = 0;
    quint8 crypt = 0;
    if (unicode()) {
        eof = le64(head, 184);
        m_nbtBid = le64(head, 216);
        m_nbtIb = le64(head, 224);
        m_bbtBid = le64(head, 232);
        m_bbtIb = le64(head, 240);
        crypt = quint8(head.at(513));
    } else {
        eof = le32(head, 168);
        m_nbtBid = le32(head, 184);
        m_nbtIb = le32(head, 188);
        m_bbtBid = le32(head, 192);
        m_bbtIb = le32(head, 196);
        crypt = quint8(head.at(461));
    }
    if (crypt == 0x10)
        return fail(error, QStringLiteral("The file is protected by Windows Information Protection and cannot be read here"));
    if (crypt > 2) return fail(error, QStringLiteral("The file uses an unknown encoding (%1)").arg(crypt));
    m_encoding = Encoding(crypt);
    if (eof > quint64(m_size))
        return fail(error, QStringLiteral("The file is truncated: it should be %1 bytes but is %2").arg(eof).arg(m_size));
    for (const quint64 ib : {m_nbtIb, m_bbtIb}) {
        if (ib % kPageSize != 0 || ib < kPageSize || ib > quint64(m_size) - kPageSize)
            return fail(error, QStringLiteral("The file's index lies outside the file"));
    }
    return true;
}

bool NodeDatabase::readPage(quint64 ib, quint64 bid, quint8 ptype, QByteArray *page, QString *error)
{
    if (ib % kPageSize != 0 || ib < kPageSize || m_size < kPageSize || ib > quint64(m_size) - kPageSize)
        return fail(error, QStringLiteral("An index page lies outside the file (offset %1)").arg(hex(ib)));
    const Sizes &s = unicode() ? kUnicode : kAnsi;
    if (const QByteArray *cached = m_pages.object(ib)) {
        *page = *cached;
    } else {
        *page = readAt(qint64(ib), kPageSize);
        if (page->size() != kPageSize) return fail(error, QStringLiteral("An index page cannot be read (offset %1)").arg(hex(ib)));
        const qsizetype t = kPageSize - s.pageTrailer;
        const quint32 stored = unicode() ? le32(*page, t + 4) : le32(*page, t + 8);
        if (!crcMatches(*page, t, stored))
            return fail(error, QStringLiteral("An index page is damaged (checksum mismatch at offset %1)").arg(hex(ib)));
        m_pages.insert(ib, new QByteArray(*page));
    }
    const qsizetype t = kPageSize - s.pageTrailer;
    const quint8 type = quint8(page->at(t));
    const quint64 pageBid = unicode() ? le64(*page, t + 8) : le32(*page, t + 4);
    if (type != ptype || quint8(page->at(t + 1)) != ptype)
        return fail(error, QStringLiteral("An index page has the wrong type (offset %1)").arg(hex(ib)));
    if ((pageBid & ~kBidReserved) != (bid & ~kBidReserved))
        return fail(error, QStringLiteral("An index page is not the one its parent names (offset %1)").arg(hex(ib)));
    return true;
}

std::optional<QByteArray> NodeDatabase::lookup(bool nbt, quint64 key, QString *error)
{
    const Sizes &s = unicode() ? kUnicode : kAnsi;
    quint64 ib = nbt ? m_nbtIb : m_bbtIb;
    quint64 bid = nbt ? m_nbtBid : m_bbtBid;
    int expectedLevel = -1;
    QSet<quint64> visited;
    for (int depth = 0; depth <= kMaxTreeDepth; ++depth) {
        if (visited.contains(ib)) {
            fail(error, QStringLiteral("The %1 index loops back on itself").arg(nbt ? QStringLiteral("node") : QStringLiteral("block")));
            return std::nullopt;
        }
        visited.insert(ib);
        QByteArray page;
        if (!readPage(ib, bid, nbt ? kPtypeNbt : kPtypeBbt, &page, error)) return std::nullopt;
        const int count = quint8(page.at(s.pageEntries));
        const int entrySize = quint8(page.at(s.pageEntries + 2));
        const int level = quint8(page.at(s.pageEntries + 3));
        const int want = level > 0 ? s.btEntry : (nbt ? s.nbtEntry : s.bbtEntry);
        if ((expectedLevel >= 0 && level != expectedLevel) || level > kMaxTreeDepth) {
            fail(error, QStringLiteral("The index has inconsistent levels (offset %1)").arg(hex(ib)));
            return std::nullopt;
        }
        if (entrySize != want || count * entrySize > s.pageEntries) {
            fail(error, QStringLiteral("An index page has a bad entry size or count (offset %1)").arg(hex(ib)));
            return std::nullopt;
        }
        int chosen = -1;
        quint64 previous = 0;
        for (int i = 0; i < count; ++i) {
            const quint64 entryKey = field(page, qsizetype(i) * entrySize) & (nbt ? ~quint64(0) : ~kBidReserved);
            if (i > 0 && entryKey <= previous) {
                fail(error, QStringLiteral("An index page is out of order (offset %1)").arg(hex(ib)));
                return std::nullopt;
            }
            previous = entryKey;
            if (level == 0 && entryKey == key) return page.mid(qsizetype(i) * entrySize, entrySize);
            if (level > 0 && entryKey <= key) chosen = i;
        }
        if (level == 0 || chosen < 0) return std::nullopt;   // not present
        const qsizetype at = qsizetype(chosen) * entrySize;
        bid = field(page, at + s.bid);
        ib = field(page, at + 2 * s.bid);
        expectedLevel = level - 1;
    }
    fail(error, QStringLiteral("The index is deeper than %1 levels").arg(kMaxTreeDepth));
    return std::nullopt;
}

std::optional<NodeEntry> NodeDatabase::findNode(quint32 nid, QString *error)
{
    if (error) error->clear();
    const std::optional<QByteArray> entry = lookup(true, nid, error);
    if (!entry) return std::nullopt;
    const Sizes &s = unicode() ? kUnicode : kAnsi;
    NodeEntry node;
    node.nid = nid;
    node.bidData = field(*entry, s.bid);
    node.bidSub = field(*entry, 2 * s.bid);
    node.nidParent = le32(*entry, 3 * s.bid);
    return node;
}

bool NodeDatabase::readBlock(quint64 bid, QByteArray *out, QString *error)
{
    const Sizes &s = unicode() ? kUnicode : kAnsi;
    QString why;
    const std::optional<QByteArray> entry = lookup(false, bid & ~kBidReserved, &why);
    if (!entry) return fail(error, why.isEmpty() ? QStringLiteral("A data block is missing (%1)").arg(hex(bid)) : why);
    const quint64 ib = field(*entry, s.bid);
    const qint64 cb = le16(*entry, 2 * s.bid);
    if (cb > kMaxBlockSize - s.blockTrailer)
        return fail(error, QStringLiteral("A data block has a bad size (%1)").arg(hex(bid)));
    const qint64 total = (cb + s.blockTrailer + kBlockAlign - 1) / kBlockAlign * kBlockAlign;
    if (ib % kBlockAlign != 0 || ib > quint64(m_size) || quint64(total) > quint64(m_size) - ib)
        return fail(error, QStringLiteral("A data block lies outside the file (%1)").arg(hex(bid)));
    const QByteArray block = readAt(qint64(ib), total);
    if (block.size() != total) return fail(error, QStringLiteral("A data block cannot be read (%1)").arg(hex(bid)));
    const qsizetype t = total - s.blockTrailer;
    const quint64 trailerBid = unicode() ? le64(block, t + 8) : le32(block, t + 4);
    const quint32 stored = unicode() ? le32(block, t + 4) : le32(block, t + 8);
    if (le16(block, t) != cb)
        return fail(error, QStringLiteral("A data block's size does not match its index entry (%1)").arg(hex(bid)));
    if ((trailerBid & ~kBidReserved) != (bid & ~kBidReserved))
        return fail(error, QStringLiteral("A data block is not the one its index names (%1)").arg(hex(bid)));
    if (!crcMatches(block, cb, stored))
        return fail(error, QStringLiteral("A data block is damaged (checksum mismatch, %1)").arg(hex(bid)));
    *out = block.left(cb);
    return true;
}

bool NodeDatabase::readData(quint64 bid, QList<QByteArray> *blocks, QString *error, qint64 limit)
{
    blocks->clear();
    if (bid == 0) return true;
    qint64 total = 0;
    QSet<quint64> visited;
    if (collect(bid, -1, blocks, &total, limit, &visited, error)) return true;
    blocks->clear();
    return false;
}

bool NodeDatabase::readStream(quint64 bid, QByteArray *out, QString *error, qint64 limit)
{
    QList<QByteArray> blocks;
    out->clear();
    if (!readData(bid, &blocks, error, limit)) return false;
    qsizetype size = 0;
    for (const QByteArray &block : std::as_const(blocks)) size += block.size();
    out->reserve(size);
    for (const QByteArray &block : std::as_const(blocks)) out->append(block);
    return true;
}

bool NodeDatabase::collect(quint64 bid, int expectedLevel, QList<QByteArray> *blocks, qint64 *total, qint64 limit,
                           QSet<quint64> *visited, QString *error)
{
    const quint64 key = bid & ~kBidReserved;
    if (visited->contains(key)) return fail(error, QStringLiteral("A data tree loops back on itself (%1)").arg(hex(bid)));
    visited->insert(key);
    QByteArray raw;
    if (!readBlock(bid, &raw, error)) return false;
    if ((bid & kBidInternal) == 0) {
        if (expectedLevel > 0) return fail(error, QStringLiteral("A data tree is missing a level (%1)").arg(hex(bid)));
        if (raw.size() > limit - *total)
            return fail(error, QStringLiteral("A value is larger than %1 MiB").arg(limit / (1024 * 1024)));
        if (m_encoding == Encoding::Permute) permute(raw.data(), raw.size(), false);
        else if (m_encoding == Encoding::Cyclic) cyclic(raw.data(), raw.size(), quint32(key));
        *total += raw.size();
        blocks->append(raw);
        return true;
    }
    const Sizes &s = unicode() ? kUnicode : kAnsi;
    if (raw.size() < 8) return fail(error, QStringLiteral("A data tree block is truncated (%1)").arg(hex(bid)));
    const int type = quint8(raw.at(0));
    const int level = quint8(raw.at(1));
    const int count = le16(raw, 2);
    const qint64 declared = le32(raw, 4);
    if (type != 0x01 || (level != 1 && level != 2) || (expectedLevel >= 0 && level != expectedLevel))
        return fail(error, QStringLiteral("A data tree block is malformed (%1)").arg(hex(bid)));
    if (8 + qint64(count) * s.bid > raw.size())
        return fail(error, QStringLiteral("A data tree block lists more blocks than it holds (%1)").arg(hex(bid)));
    if (declared > limit - *total)
        return fail(error, QStringLiteral("A value is larger than %1 MiB").arg(limit / (1024 * 1024)));
    const qint64 before = *total;
    for (int i = 0; i < count; ++i) {
        if (!collect(field(raw, 8 + qsizetype(i) * s.bid), level - 1, blocks, total, limit, visited, error)) return false;
    }
    if (*total - before != declared)
        return fail(error, QStringLiteral("A data tree's size does not add up (%1)").arg(hex(bid)));
    return true;
}

bool NodeDatabase::readSubnodes(quint64 bid, SubnodeMap *out, QString *error)
{
    out->clear();
    if (bid == 0) return true;
    QSet<quint64> visited;
    if (collectSubnodes(bid, -1, out, &visited, error)) return true;
    out->clear();
    return false;
}

bool NodeDatabase::collectSubnodes(quint64 bid, int expectedLevel, SubnodeMap *out, QSet<quint64> *visited,
                                   QString *error)
{
    const quint64 key = bid & ~kBidReserved;
    if ((bid & kBidInternal) == 0)
        return fail(error, QStringLiteral("A subnode index points at a data block (%1)").arg(hex(bid)));
    if (visited->contains(key)) return fail(error, QStringLiteral("A subnode index loops back on itself (%1)").arg(hex(bid)));
    visited->insert(key);
    QByteArray raw;
    if (!readBlock(bid, &raw, error)) return false;
    const Sizes &s = unicode() ? kUnicode : kAnsi;
    if (raw.size() < s.subHeader) return fail(error, QStringLiteral("A subnode index is truncated (%1)").arg(hex(bid)));
    const int type = quint8(raw.at(0));
    const int level = quint8(raw.at(1));
    const int count = le16(raw, 2);
    if (type != 0x02 || level > 1 || (expectedLevel >= 0 && level != expectedLevel))
        return fail(error, QStringLiteral("A subnode index is malformed (%1)").arg(hex(bid)));
    const int entrySize = level == 0 ? s.slEntry : s.siEntry;
    if (s.subHeader + qint64(count) * entrySize > raw.size())
        return fail(error, QStringLiteral("A subnode index lists more entries than it holds (%1)").arg(hex(bid)));
    for (int i = 0; i < count; ++i) {
        const qsizetype at = s.subHeader + qsizetype(i) * entrySize;
        if (level == 1) {
            if (!collectSubnodes(field(raw, at + s.bid), 0, out, visited, error)) return false;
            continue;
        }
        if (out->size() >= kMaxSubnodes) return fail(error, QStringLiteral("A message has too many parts"));
        Subnode node;
        node.nid = le32(raw, at);
        node.bidData = field(raw, at + s.bid);
        node.bidSub = field(raw, at + 2 * s.bid);
        if (!out->contains(node.nid)) out->insert(node.nid, node);
    }
    return true;
}

} // namespace QindaOffice::Mail::Pst
