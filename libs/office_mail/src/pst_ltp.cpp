// SPDX-License-Identifier: GPL-3.0-or-later
// MS-PST heaps, B-trees on heaps, property and table contexts (pst_ltp.h).

#include "pst_ltp.h"

#include <QSet>

#include <algorithm>

namespace QindaOffice::Mail::Pst {
namespace {

constexpr quint8 kHeapSignature = 0xEC;
constexpr quint8 kBTreeSignature = 0xB5;
constexpr int kMaxBTreeLevels = 8;
constexpr int kMaxBTreeRecords = 4000000;
constexpr qint64 kMaxRowMatrixBytes = 128LL * 1024 * 1024;

quint16 le16(const char *p)
{
    const auto *u = reinterpret_cast<const uchar *>(p);
    return quint16(u[0] | (u[1] << 8));
}

quint32 le32(const char *p)
{
    const auto *u = reinterpret_cast<const uchar *>(p);
    return quint32(u[0]) | (quint32(u[1]) << 8) | (quint32(u[2]) << 16) | (quint32(u[3]) << 24);
}

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool walkBTree(const Heap &heap, quint32 hid, int level, int keySize, int dataSize, QList<QByteArray> *records,
               QSet<quint32> *visited, QString *error)
{
    if (visited->contains(hid)) return fail(error, QStringLiteral("An index inside an item loops back on itself"));
    visited->insert(hid);
    QByteArray bytes;
    if (!heap.allocation(hid, &bytes, error)) return false;
    const int size = level > 0 ? keySize + 4 : keySize + dataSize;
    if (bytes.size() % size != 0) return fail(error, QStringLiteral("An index inside an item has a partial record"));
    for (qsizetype at = 0; at < bytes.size(); at += size) {
        if (level > 0) {
            if (!walkBTree(heap, le32(bytes.constData() + at + keySize), level - 1, keySize, dataSize, records, visited, error))
                return false;
        } else {
            if (records->size() >= kMaxBTreeRecords) return fail(error, QStringLiteral("An index inside an item is too large"));
            records->append(bytes.mid(at, size));
        }
    }
    return true;
}

// Every leaf record of the B-tree on heap whose header is `hid`.
bool readBTree(const Heap &heap, quint32 hid, int keySize, int dataSize, QList<QByteArray> *records, QString *error)
{
    QByteArray header;
    if (!heap.allocation(hid, &header, error)) return false;
    if (header.size() < 8 || quint8(header.at(0)) != kBTreeSignature)
        return fail(error, QStringLiteral("An index inside an item is malformed"));
    const int cbKey = quint8(header.at(1));
    const int cbEnt = quint8(header.at(2));
    const int levels = quint8(header.at(3));
    const quint32 root = le32(header.constData() + 4);
    if (cbKey != keySize || cbEnt != dataSize || levels > kMaxBTreeLevels)
        return fail(error, QStringLiteral("An index inside an item has an unexpected layout"));
    if (root == 0) return true;
    QSet<quint32> visited;
    return walkBTree(heap, root, levels, cbKey, cbEnt, records, &visited, error);
}

} // namespace

bool isVariableType(quint16 type)
{
    if (type & 0x1000) return true;
    switch (type) {
    case 0x000D: case 0x001E: case 0x001F: case 0x0048: case 0x0102: case 0x00FB: case 0x00FD: case 0x00FE:
        return true;
    default:
        return false;
    }
}

bool Heap::load(const QList<QByteArray> &blocks, QString *error)
{
    m_pages.clear();
    if (blocks.isEmpty()) return fail(error, QStringLiteral("An item has no data"));
    if (blocks.size() > 0xFFFF) return fail(error, QStringLiteral("An item has too many data blocks"));
    for (qsizetype i = 0; i < blocks.size(); ++i) {
        const QByteArray &data = blocks.at(i);
        // HNHDR on block 0; HNBITMAPHDR on block 8 and every 128th after; HNPAGEHDR otherwise.
        const int header = i == 0 ? 12 : (i >= 8 && (i - 8) % 128 == 0) ? 66 : 2;
        if (data.size() < header + 4) return fail(error, QStringLiteral("An item's data block is truncated"));
        const int map = le16(data.constData());
        if (map < header || map + 4 > data.size()) return fail(error, QStringLiteral("An item's data block has a bad map offset"));
        const int count = le16(data.constData() + map);
        if (map + 4 + 2 * (count + 1) > data.size()) return fail(error, QStringLiteral("An item's data block map is truncated"));
        Page page;
        page.data = data;
        page.offsets.reserve(count + 1);
        for (int k = 0; k <= count; ++k) {
            const quint16 offset = le16(data.constData() + map + 4 + 2 * k);
            if (offset < header || offset > map || (k > 0 && offset < page.offsets.last()))
                return fail(error, QStringLiteral("An item's data block map is inconsistent"));
            page.offsets.append(offset);
        }
        if (i == 0) {
            if (quint8(data.at(2)) != kHeapSignature) return fail(error, QStringLiteral("An item's data is not a heap"));
            m_client = quint8(data.at(3));
            m_root = le32(data.constData() + 4);
        }
        m_pages.append(page);
    }
    return true;
}

bool Heap::allocation(quint32 hid, QByteArray *out, QString *error) const
{
    out->clear();
    const quint32 index = (hid >> 5) & 0x7FF;
    const quint32 block = hid >> 16;
    if ((hid & kNidTypeMask) != kNidTypeHid || index == 0 || block >= quint32(m_pages.size()))
        return fail(error, QStringLiteral("An item refers to data it does not hold"));
    const Page &page = m_pages.at(block);
    if (index >= quint32(page.offsets.size())) return fail(error, QStringLiteral("An item refers to data it does not hold"));
    const quint16 start = page.offsets.at(index - 1);
    *out = page.data.mid(start, page.offsets.at(index) - start);
    return true;
}

bool readHnid(NodeDatabase *db, const Heap &heap, const SubnodeMap &subnodes, quint32 hnid, QByteArray *out,
              QString *error, qint64 limit)
{
    out->clear();
    if (hnid == 0) return true;   // a null id: an empty value
    if ((hnid & kNidTypeMask) == kNidTypeHid) return heap.allocation(hnid, out, error);
    const auto it = subnodes.constFind(hnid);
    if (it == subnodes.cend()) return fail(error, QStringLiteral("An item refers to a part it does not have"));
    return db->readStream(it->bidData, out, error, limit);
}

bool PropertyContext::load(NodeDatabase *db, const QList<QByteArray> &blocks, const SubnodeMap &subnodes, QString *error)
{
    m_db = db;
    m_subnodes = subnodes;
    m_entries.clear();
    if (!m_heap.load(blocks, error)) return false;
    if (m_heap.clientSignature() != kHeapPropertyContext) return fail(error, QStringLiteral("An item's properties are malformed"));
    QList<QByteArray> records;
    if (!readBTree(m_heap, m_heap.userRoot(), 2, 6, &records, error)) return false;
    for (const QByteArray &record : std::as_const(records)) {
        const quint16 id = le16(record.constData());
        if (!m_entries.contains(id)) m_entries.insert(id, {le16(record.constData() + 2), le32(record.constData() + 4)});
    }
    return true;
}

bool PropertyContext::value(quint16 id, PropertyValue *out, QString *error, qint64 limit) const
{
    if (error) error->clear();
    const auto it = m_entries.constFind(id);
    if (it == m_entries.cend()) return false;
    out->type = it->type;
    switch (it->type) {
    case 0x0002: case 0x0003: case 0x0004: case 0x000A: case 0x000B: {
        // PtypInteger16 and the 4-byte types live in the record itself.
        const int size = it->type == 0x0002 ? 2 : 4;
        out->bytes.resize(size);
        for (int i = 0; i < size; ++i) out->bytes[i] = char((it->raw >> (8 * i)) & 0xFF);
        return true;
    }
    default:
        return readHnid(m_db, m_heap, m_subnodes, it->raw, &out->bytes, error, limit);
    }
}

bool TableContext::load(NodeDatabase *db, const QList<QByteArray> &blocks, const SubnodeMap &subnodes, QString *error)
{
    m_db = db;
    m_subnodes = subnodes;
    m_columns.clear();
    m_matrix.clear();
    m_firstRow.clear();
    m_rows = 0;
    if (!m_heap.load(blocks, error)) return false;
    if (m_heap.clientSignature() != kHeapTableContext) return fail(error, QStringLiteral("A table is malformed"));
    QByteArray info;
    if (!m_heap.allocation(m_heap.userRoot(), &info, error)) return false;
    if (info.size() < 22 || quint8(info.at(0)) != kHeapTableContext) return fail(error, QStringLiteral("A table header is malformed"));
    const int columns = quint8(info.at(1));
    const int end4 = le16(info.constData() + 2), end2 = le16(info.constData() + 4);
    const int end1 = le16(info.constData() + 6), endBitmap = le16(info.constData() + 8);
    const quint32 rows = le32(info.constData() + 14);
    if (22 + columns * 8 > info.size() || end4 > end2 || end2 > end1 || end1 > endBitmap
        || endBitmap - end1 < (columns + 7) / 8 || endBitmap < 4)
        return fail(error, QStringLiteral("A table header is inconsistent"));
    m_rowSize = endBitmap;
    m_cebStart = end1;
    for (int c = 0; c < columns; ++c) {
        const char *p = info.constData() + 22 + 8 * c;
        Column column{le32(p), le16(p + 4), quint8(p[6]), quint8(p[7])};
        if (column.ib + column.cb > end1 || column.bit >= (endBitmap - end1) * 8)
            return fail(error, QStringLiteral("A table column lies outside its row"));
        const quint16 id = quint16(column.tag >> 16);
        if (!m_columns.contains(id)) m_columns.insert(id, column);
    }
    if (rows == 0) return true;
    if ((rows & kNidTypeMask) == kNidTypeHid) {
        QByteArray matrix;
        if (!m_heap.allocation(rows, &matrix, error)) return false;
        m_matrix.append(matrix);
    } else {
        const auto it = m_subnodes.constFind(rows);
        if (it == m_subnodes.cend()) return fail(error, QStringLiteral("A table's rows are missing"));
        if (!db->readData(it->bidData, &m_matrix, error, kMaxRowMatrixBytes)) return false;
    }
    for (const QByteArray &block : std::as_const(m_matrix)) {
        m_firstRow.append(m_rows);
        const qint64 next = qint64(m_rows) + block.size() / m_rowSize;
        if (next > kMaxRows) return fail(error, QStringLiteral("A table has more than %1 rows").arg(kMaxRows));
        m_rows = int(next);
    }
    return true;
}

const char *TableContext::rowData(int row) const
{
    const auto it = std::upper_bound(m_firstRow.cbegin(), m_firstRow.cend(), row);
    const qsizetype block = (it - m_firstRow.cbegin()) - 1;
    return m_matrix.at(block).constData() + qsizetype(row - m_firstRow.at(block)) * m_rowSize;
}

quint32 TableContext::rowId(int row) const
{
    if (row < 0 || row >= m_rows) return 0;
    const auto it = m_columns.constFind(kPidLtpRowId);
    const int at = it != m_columns.cend() && it->cb == 4 ? it->ib : 0;
    return le32(rowData(row) + at);
}

quint16 TableContext::columnType(quint16 id) const
{
    const auto it = m_columns.constFind(id);
    return it == m_columns.cend() ? 0 : quint16(it->tag & 0xFFFF);
}

bool TableContext::cellExists(int row, quint16 id) const
{
    const auto it = m_columns.constFind(id);
    if (it == m_columns.cend() || row < 0 || row >= m_rows) return false;
    return (quint8(rowData(row)[m_cebStart + it->bit / 8]) & (0x80 >> (it->bit % 8))) != 0;
}

bool TableContext::cell(int row, quint16 id, PropertyValue *out, QString *error, qint64 limit) const
{
    if (error) error->clear();
    if (!cellExists(row, id)) return false;
    const auto it = m_columns.constFind(id);
    const char *data = rowData(row);
    out->type = quint16(it->tag & 0xFFFF);
    if (isVariableType(out->type)) {
        if (it->cb != 4) return fail(error, QStringLiteral("A table cell is malformed"));
        return readHnid(m_db, m_heap, m_subnodes, le32(data + it->ib), &out->bytes, error, limit);
    }
    out->bytes = QByteArray(data + it->ib, it->cb);
    return true;
}

} // namespace QindaOffice::Mail::Pst
