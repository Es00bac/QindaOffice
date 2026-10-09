// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support (M76, header-only): the LTP encoders of the .pst builder
// (pst_builder.h) — a property list, the heap-on-node writer, B-trees on the
// heap, property contexts and table contexts, written from MS-PST 2.3
// directly (none of the reader's code is reused here, so a misreading of the
// specification shows up as a disagreement, not a silent agreement).

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QTimeZone>

#include <algorithm>
#include <bit>
#include <initializer_list>

namespace QindaOffice::Mail::Test::PstBuild {

inline constexpr int kMaxAllocation = 3580;   // MS-PST 2.3.1: larger values go to subnodes

inline QByteArray bytes(std::initializer_list<int> values)
{
    QByteArray out;
    for (const int value : values) out.append(char(value & 0xFF));
    return out;
}
inline QByteArray le16(quint16 v) { return bytes({v & 0xFF, v >> 8}); }
inline QByteArray le32(quint32 v) { return le16(quint16(v & 0xFFFF)) + le16(quint16(v >> 16)); }
inline QByteArray le64(quint64 v) { return le32(quint32(v & 0xFFFFFFFFu)) + le32(quint32(v >> 32)); }
inline void put16(QByteArray &b, qsizetype at, quint16 v) { b.replace(at, 2, le16(v)); }
inline void put32(QByteArray &b, qsizetype at, quint32 v) { b.replace(at, 4, le32(v)); }
inline void put64(QByteArray &b, qsizetype at, quint64 v) { b.replace(at, 8, le64(v)); }

inline QByteArray utf16(const QString &text)
{
    QByteArray out;
    for (const QChar c : text) out += le16(c.unicode());
    return out;
}

inline quint64 fileTime(const QDateTime &utc)
{
    return quint64(utc.toMSecsSinceEpoch() * 10000 + 116444736000000000LL);
}

// MS-OXCDATA type sizes inside a table row: inline fixed values, or a
// 4-byte HNID for everything stored elsewhere.
inline int cellSize(quint16 type)
{
    switch (type) {
    case 0x000B: return 1;
    case 0x0002: return 2;
    case 0x0003: case 0x0004: case 0x000A: return 4;
    case 0x0005: case 0x0006: case 0x0007: case 0x0014: case 0x0040: return 8;
    default: return 4;   // HNID
    }
}

inline bool inlineInContext(quint16 type)
{
    return type == 0x0002 || type == 0x0003 || type == 0x0004 || type == 0x000A || type == 0x000B;
}

struct Prop {
    quint16 id = 0;
    quint16 type = 0;
    QByteArray value;   // little-endian bytes of a fixed value, else the data
};

struct PropList {
    QList<Prop> props;

    PropList &raw(quint16 id, quint16 type, const QByteArray &value)
    {
        props.append({id, type, value});
        return *this;
    }
    PropList &longValue(quint16 id, quint32 v) { return raw(id, 0x0003, le32(v)); }
    PropList &boolValue(quint16 id, bool v) { return raw(id, 0x000B, QByteArray(1, v ? '\1' : '\0')); }
    PropList &doubleValue(quint16 id, double v) { return raw(id, 0x0005, le64(std::bit_cast<quint64>(v))); }
    PropList &time(quint16 id, const QDateTime &utc) { return raw(id, 0x0040, le64(fileTime(utc))); }
    PropList &binary(quint16 id, const QByteArray &bytes) { return raw(id, 0x0102, bytes); }
    PropList &unicode(quint16 id, const QString &text) { return raw(id, 0x001F, utf16(text)); }
    PropList &string8(quint16 id, const QByteArray &bytes) { return raw(id, 0x001E, bytes); }
    // A text property as a Unicode file writes it, or as an ANSI file
    // (PtypString8 in windows-1252 for the Latin-1 range used here).
    PropList &text(quint16 id, const QString &value, bool unicodeFile)
    {
        return unicodeFile ? unicode(id, value) : string8(id, value.toLatin1());
    }
    // MS-PST 2.3.3.4.2 variable multi-valued data: count, offsets, items.
    PropList &multiple(quint16 id, quint16 type, const QList<QByteArray> &items)
    {
        QByteArray out = le32(quint32(items.size()));
        quint32 offset = 4 + 4 * quint32(items.size());
        for (const QByteArray &item : items) {
            out += le32(offset);
            offset += quint32(item.size());
        }
        for (const QByteArray &item : items) out += item;
        return raw(id, type, out);
    }
    PropList &mvUnicode(quint16 id, const QStringList &values)
    {
        QList<QByteArray> items;
        for (const QString &value : values) items.append(utf16(value));
        return multiple(id, 0x101F, items);
    }
    PropList &mvBinary(quint16 id, const QList<QByteArray> &values) { return multiple(id, 0x1102, values); }
    // MS-PST 2.3.3.5: a PtypObject value is {subnode id, size}.
    PropList &object(quint16 id, quint32 nid, quint32 size) { return raw(id, 0x000D, le32(nid) + le32(size)); }
};

// A subnode an encoder created for a large value (or a table's row matrix).
struct ValueSubnode {
    quint32 nid = 0;
    QList<QByteArray> blocks;
};

inline QList<QByteArray> chunked(const QByteArray &data, int size)
{
    QList<QByteArray> out;
    for (qsizetype at = 0; at < data.size(); at += size) out.append(data.mid(at, size));
    if (out.isEmpty()) out.append(QByteArray());
    return out;
}

// MS-PST 2.3.1 heap-on-node: allocations packed into blocks, each block with
// its header (HNHDR, HNPAGEHDR, or HNBITMAPHDR on block 8, 136, …) and its
// page map at the end.
class HeapWriter {
public:
    explicit HeapWriter(int maxBlock) : m_maxBlock(maxBlock) {}

    static int headerSize(qsizetype block) { return block == 0 ? 12 : (block >= 8 && (block - 8) % 128 == 0) ? 66 : 2; }

    quint32 allocate(const QByteArray &bytes)
    {
        if (m_blocks.isEmpty() || !fits(m_blocks.size() - 1, bytes.size())) m_blocks.append(Block{});
        Block &block = m_blocks.last();
        block.allocations.append(bytes);
        block.used += int(bytes.size());
        return quint32((m_blocks.size() - 1) << 16) | quint32(block.allocations.size() << 5);
    }

    QList<QByteArray> finish(quint8 client, quint32 root) const
    {
        QList<QByteArray> out;
        const qsizetype count = qMax<qsizetype>(1, m_blocks.size());
        for (qsizetype i = 0; i < count; ++i) {
            const Block block = i < m_blocks.size() ? m_blocks.at(i) : Block{};
            QByteArray data(headerSize(i), '\0');
            QList<quint16> offsets{quint16(data.size())};
            for (const QByteArray &allocation : block.allocations) {
                data += allocation;
                offsets.append(quint16(data.size()));
            }
            if (data.size() % 2) data += '\0';
            const quint16 map = quint16(data.size());
            data += le16(quint16(block.allocations.size())) + le16(0);
            for (const quint16 offset : offsets) data += le16(offset);
            put16(data, 0, map);
            if (i == 0) {
                data[2] = char(0xEC);
                data[3] = char(client);
                put32(data, 4, root);
            }
            out.append(data);
        }
        return out;
    }

private:
    struct Block {
        QList<QByteArray> allocations;
        int used = 0;
    };
    bool fits(qsizetype index, qsizetype size) const
    {
        const Block &block = m_blocks.at(index);
        const qsizetype map = 4 + 2 * (block.allocations.size() + 2);
        return headerSize(index) + block.used + size + 1 + map <= m_maxBlock;
    }
    QList<Block> m_blocks;
    int m_maxBlock;
};

// MS-PST 2.3.2 B-tree on heap; `records` sorted by key. Returns the HID of
// its BTHHEADER.
inline quint32 writeBTree(HeapWriter &heap, int keySize, int dataSize, const QList<QPair<QByteArray, QByteArray>> &records)
{
    QList<QPair<QByteArray, quint32>> level;
    const int perLeaf = kMaxAllocation / (keySize + dataSize);
    for (qsizetype at = 0; at < records.size(); at += perLeaf) {
        QByteArray bytes;
        for (qsizetype i = at; i < qMin(records.size(), at + perLeaf); ++i) bytes += records.at(i).first + records.at(i).second;
        level.append({records.at(at).first, heap.allocate(bytes)});
    }
    int levels = 0;
    const int perIndex = kMaxAllocation / (keySize + 4);
    while (level.size() > 1) {
        QList<QPair<QByteArray, quint32>> next;
        for (qsizetype at = 0; at < level.size(); at += perIndex) {
            QByteArray bytes;
            for (qsizetype i = at; i < qMin(level.size(), at + perIndex); ++i) bytes += level.at(i).first + le32(level.at(i).second);
            next.append({level.at(at).first, heap.allocate(bytes)});
        }
        level = next;
        ++levels;
    }
    QByteArray header = bytes({0xB5, keySize, dataSize, levels});
    header += le32(level.isEmpty() ? 0 : level.first().second);
    return heap.allocate(header);
}

// The HNID of a variable value: a heap allocation, or a new subnode.
inline quint32 storeValue(HeapWriter &heap, const QByteArray &value, int maxBlock, quint32 *nextIndex,
                          QList<ValueSubnode> *subnodes)
{
    if (value.isEmpty()) return 0;
    if (value.size() <= kMaxAllocation) return heap.allocate(value);
    const quint32 nid = ((*nextIndex)++ << 5) | 0x1F;
    subnodes->append({nid, chunked(value, maxBlock)});
    return nid;
}

// MS-PST 2.3.3 property context.
inline QList<QByteArray> writePropertyContext(const PropList &list, int maxBlock, quint32 *nextIndex,
                                              QList<ValueSubnode> *subnodes)
{
    HeapWriter heap(maxBlock);
    QList<Prop> props = list.props;
    std::stable_sort(props.begin(), props.end(), [](const Prop &a, const Prop &b) { return a.id < b.id; });
    QList<QPair<QByteArray, QByteArray>> records;
    for (const Prop &p : std::as_const(props)) {
        quint32 value = 0;
        if (inlineInContext(p.type)) {
            for (qsizetype i = 0; i < qMin<qsizetype>(4, p.value.size()); ++i) value |= quint32(quint8(p.value.at(i))) << (8 * i);
        } else {
            value = storeValue(heap, p.value, maxBlock, nextIndex, subnodes);
        }
        records.append({le16(p.id), le16(p.type) + le32(value)});
    }
    return heap.finish(0xBC, writeBTree(heap, 2, 6, records));
}

struct TableRow {
    quint32 id = 0;
    PropList cells;
};

// MS-PST 2.3.4 table context. Columns: PidTagLtpRowId and PidTagLtpRowVer,
// then `columns` (tags) grouped 8/4-byte, 2-byte, 1-byte; the cell
// existence bitmap last. A matrix over 3580 bytes goes to a subnode whose
// blocks hold whole rows only.
inline QList<QByteArray> writeTableContext(const QList<quint32> &columns, const QList<TableRow> &rows, bool unicodeFile,
                                           int maxBlock, quint32 *nextIndex, QList<ValueSubnode> *subnodes)
{
    QList<quint32> tags{0x67F20003u, 0x67F30003u};
    tags += columns;
    struct Desc {
        quint32 tag;
        int ib, cb, bit;
    };
    // Row layout: the 8/4-byte group (row id at 0, row version at 4 first),
    // then 2-byte, then 1-byte cells, then the cell existence bitmap.
    QList<Desc> descs;
    int offset = 0;
    const auto place = [&](const auto &wanted) {
        for (qsizetype i = 0; i < tags.size(); ++i) {
            const int size = cellSize(quint16(tags.at(i) & 0xFFFF));
            if (!wanted(size)) continue;
            descs.append({tags.at(i), offset, size, int(i)});
            offset += size;
        }
        return offset;
    };
    const int end4 = place([](int size) { return size >= 4; });
    const int end2 = place([](int size) { return size == 2; });
    const int end1 = place([](int size) { return size == 1; });
    const int rowSize = end1 + int((tags.size() + 7) / 8);
    // The descriptors themselves are listed by tag, as Outlook writes them.
    std::stable_sort(descs.begin(), descs.end(), [](const Desc &a, const Desc &b) { return a.tag < b.tag; });
    HeapWriter heap(maxBlock);
    QByteArray matrix;
    QList<QPair<quint32, quint32>> order;   // (row id, row index)
    for (qsizetype r = 0; r < rows.size(); ++r) {
        QByteArray row(rowSize, '\0');
        for (const Desc &d : std::as_const(descs)) {
            QByteArray value;
            if (d.tag == 0x67F20003u) {
                value = le32(rows.at(r).id);
            } else if (d.tag == 0x67F30003u) {
                value = le32(quint32(r + 1));
            } else {
                bool present = false;
                for (const Prop &p : rows.at(r).cells.props) {
                    if ((quint32(p.id) << 16 | p.type) != d.tag) continue;
                    present = true;
                    value = d.cb == 4 && !inlineInContext(p.type) ? le32(storeValue(heap, p.value, maxBlock, nextIndex, subnodes))
                                                                : p.value.leftJustified(d.cb, '\0', true);
                }
                if (!present) continue;
            }
            row.replace(d.ib, d.cb, value);
            row[end1 + d.bit / 8] = char(quint8(row.at(end1 + d.bit / 8)) | (0x80 >> (d.bit % 8)));
        }
        matrix += row;
        order.append({rows.at(r).id, quint32(r)});
    }
    std::stable_sort(order.begin(), order.end());
    QList<QPair<QByteArray, QByteArray>> index;
    for (const auto &[id, row] : std::as_const(order)) index.append({le32(id), unicodeFile ? le32(row) : le16(quint16(row))});
    quint32 rowsHnid = 0;
    if (!matrix.isEmpty() && matrix.size() <= kMaxAllocation) {
        rowsHnid = heap.allocate(matrix);
    } else if (!matrix.isEmpty()) {
        rowsHnid = ((*nextIndex)++ << 5) | 0x1F;
        const int perBlock = maxBlock / rowSize;
        subnodes->append({rowsHnid, chunked(matrix, perBlock * rowSize)});
    }
    const quint32 rowIndex = writeBTree(heap, 4, unicodeFile ? 4 : 2, index);
    QByteArray info = bytes({0x7C, int(tags.size())});
    info += le16(quint16(end4)) + le16(quint16(end2)) + le16(quint16(end1)) + le16(quint16(rowSize));
    info += le32(rowIndex) + le32(rowsHnid) + le32(0);
    for (const Desc &d : std::as_const(descs)) info += le32(d.tag) + le16(quint16(d.ib)) + char(d.cb) + char(d.bit);
    return heap.finish(0x7C, heap.allocate(info));
}

} // namespace QindaOffice::Mail::Test::PstBuild
