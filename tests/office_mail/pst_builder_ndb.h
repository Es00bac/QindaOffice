// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Test support (M76, header-only): the NDB writer of the .pst builder
// (pst_builder.h) — blocks with trailers and the file's encoding, data
// trees (XBLOCK/XXBLOCK), subnode trees (SLBLOCK/SIBLOCK), the node and
// block B-trees and the header, in the Unicode or the ANSI format
// (MS-PST 2.2). The allocation maps are not written: the header says so
// (fAMapValid = INVALID_AMAP), which Outlook answers by rebuilding them.
// PstLayout says where everything went, so a test can corrupt precise bytes.

#include "pst_builder_ltp.h"
#include "pst_crypt.h"

#include <QByteArray>
#include <QList>

#include <algorithm>

namespace QindaOffice::Mail::Test::PstBuild {

struct NdbNode {
    quint32 nid = 0;
    quint32 parent = 0;            // the node B-tree's nidParent (top-level nodes only)
    QList<QByteArray> blocks;      // the node's data, one entry per data block
    QList<NdbNode> subnodes;
};

struct PstLayout {
    struct Page {
        qint64 ib = 0;
        quint64 bid = 0;
        int level = 0;
        bool nbt = false;
    };
    struct Block {
        quint64 bid = 0;
        qint64 ib = 0;
        int cb = 0;
    };
    bool unicode = true;
    QList<Page> pages;
    QList<Block> blocks;
    qint64 pageTrailer() const { return unicode ? 16 : 12; }
    qint64 entriesSize() const { return unicode ? 488 : 496; }
    int bidSize() const { return unicode ? 8 : 4; }
};

class NdbWriter {
public:
    NdbWriter(bool unicode, quint8 encoding) : m_unicode(unicode), m_encoding(encoding)
    {
        m_file = QByteArray(0x4400, '\0');
    }
    int maxBlock() const { return m_unicode ? 8176 : 8180; }

    void add(const NdbNode &node)
    {
        m_nbt.append({node.nid, writeData(node.blocks), writeSubnodes(node.subnodes), node.parent});
    }

    QByteArray build(PstLayout *layout)
    {
        m_layout.unicode = m_unicode;
        std::sort(m_nbt.begin(), m_nbt.end(), [](const Nbt &a, const Nbt &b) { return a.nid < b.nid; });
        std::sort(m_bbt.begin(), m_bbt.end(), [](const Bbt &a, const Bbt &b) { return a.bid < b.bid; });
        QList<QByteArray> nbtLeaves;
        for (const Nbt &n : std::as_const(m_nbt)) {
            nbtLeaves.append(m_unicode ? le64(n.nid) + le64(n.bidData) + le64(n.bidSub) + le32(n.parent) + le32(0)
                                       : le32(n.nid) + le32(quint32(n.bidData)) + le32(quint32(n.bidSub)) + le32(n.parent));
        }
        QList<QByteArray> bbtLeaves;
        for (const Bbt &b : std::as_const(m_bbt)) {
            bbtLeaves.append(m_unicode ? le64(b.bid) + le64(quint64(b.ib)) + le16(b.cb) + le16(2) + le32(0)
                                       : le32(quint32(b.bid)) + le32(quint32(b.ib)) + le16(b.cb) + le16(2));
        }
        const QPair<quint64, qint64> nbt = writeTree(true, nbtLeaves);
        const QPair<quint64, qint64> bbt = writeTree(false, bbtLeaves);
        writeHeader(nbt, bbt);
        if (layout) *layout = m_layout;
        return m_file;
    }

private:
    struct Nbt {
        quint32 nid;
        quint64 bidData, bidSub;
        quint32 parent;
    };
    struct Bbt {
        quint64 bid;
        qint64 ib;
        quint16 cb;
    };

    QByteArray bid(quint64 value) const { return m_unicode ? le64(value) : le32(quint32(value)); }

    quint64 writeBlock(const QByteArray &data, bool internal)
    {
        const quint64 id = (++m_nextBlock << 2) | (internal ? 2 : 0);
        QByteArray stored = data;
        if (!internal && m_encoding == 1) Pst::permute(stored.data(), stored.size(), true);
        if (!internal && m_encoding == 2) Pst::cyclic(stored.data(), stored.size(), quint32(id));
        const int trailer = m_unicode ? 16 : 12;
        const qint64 total = (stored.size() + trailer + 63) / 64 * 64;
        const qint64 ib = m_file.size();
        QByteArray block = stored.leftJustified(total - trailer, '\0');
        const quint32 crc = Pst::crc32(stored);
        block += le16(quint16(stored.size())) + le16(Pst::signature(quint64(ib), id));
        block += m_unicode ? le32(crc) + le64(id) : le32(quint32(id)) + le32(crc);
        m_file += block;
        m_bbt.append({id, ib, quint16(stored.size())});
        m_layout.blocks.append({id, ib, int(stored.size())});
        return id;
    }

    // A data tree over `blocks`: one data block, an XBLOCK, or an XXBLOCK.
    quint64 writeData(const QList<QByteArray> &blocks)
    {
        if (blocks.isEmpty()) return 0;
        if (blocks.size() == 1) return writeBlock(blocks.first(), false);
        const int capacity = (maxBlock() - 8) / (m_unicode ? 8 : 4);
        QList<QPair<quint64, quint32>> leaves;   // (bid, bytes)
        for (const QByteArray &block : blocks) leaves.append({writeBlock(block, false), quint32(block.size())});
        const auto index = [this](int level, const QList<QPair<quint64, quint32>> &children) {
            quint32 total = 0;
            QByteArray ids;
            for (const auto &[child, size] : children) {
                total += size;
                ids += bid(child);
            }
            return QPair<quint64, quint32>(writeBlock(bytes({0x01, level}) + le16(quint16(children.size()))
                                                          + le32(total) + ids, true), total);
        };
        if (leaves.size() <= capacity) return index(1, leaves).first;
        QList<QPair<quint64, quint32>> middles;
        for (qsizetype at = 0; at < leaves.size(); at += capacity) middles.append(index(1, leaves.mid(at, capacity)));
        return index(2, middles).first;
    }

    // A subnode tree: one SLBLOCK, or an SIBLOCK over several.
    quint64 writeSubnodes(QList<NdbNode> nodes)
    {
        if (nodes.isEmpty()) return 0;
        std::sort(nodes.begin(), nodes.end(), [](const NdbNode &a, const NdbNode &b) { return a.nid < b.nid; });
        QList<QPair<quint32, QByteArray>> entries;
        for (const NdbNode &node : std::as_const(nodes)) {
            const quint64 data = writeData(node.blocks);
            const quint64 sub = writeSubnodes(node.subnodes);
            entries.append({node.nid, (m_unicode ? le64(node.nid) : le32(node.nid)) + bid(data) + bid(sub)});
        }
        const QByteArray pad = m_unicode ? QByteArray(4, '\0') : QByteArray();
        const int header = m_unicode ? 8 : 4;
        const int capacity = (maxBlock() - header) / (m_unicode ? 24 : 12);
        const auto leaf = [&](qsizetype at, qsizetype count) {
            QByteArray block = bytes({0x02, 0x00}) + le16(quint16(count)) + pad;
            for (qsizetype i = at; i < at + count; ++i) block += entries.at(i).second;
            return writeBlock(block, true);
        };
        if (entries.size() <= capacity) return leaf(0, entries.size());
        QByteArray index = bytes({0x02, 0x01}) + le16(0) + pad;
        int count = 0;
        for (qsizetype at = 0; at < entries.size(); at += capacity, ++count) {
            const quint32 first = entries.at(at).first;
            index += (m_unicode ? le64(first) : le32(first)) + bid(leaf(at, qMin<qsizetype>(capacity, entries.size() - at)));
        }
        put16(index, 2, quint16(count));
        return writeBlock(index, true);
    }

    QPair<quint64, qint64> writePage(bool nbt, int level, const QList<QByteArray> &entries, int entrySize)
    {
        while (m_file.size() % 512 != 0) m_file += '\0';
        const qint64 ib = m_file.size();
        const quint64 id = (++m_nextPage << 2);
        const qint64 area = m_unicode ? 488 : 496;
        QByteArray page;
        for (const QByteArray &entry : entries) page += entry;
        page = page.leftJustified(area, '\0');
        page += char(entries.size());
        page += char(area / entrySize);
        page += char(entrySize);
        page += char(level);
        if (m_unicode) page += QByteArray(4, '\0');
        const quint8 type = nbt ? 0x81 : 0x80;
        const quint32 crc = Pst::crc32(page);
        page += char(type);
        page += char(type);
        page += le16(Pst::signature(quint64(ib), id));
        page += m_unicode ? le32(crc) + le64(id) : le32(quint32(id)) + le32(crc);
        m_file += page;
        m_layout.pages.append({ib, id, level, nbt});
        return {id, ib};
    }

    // Leaves, then index levels until one page is the root.
    QPair<quint64, qint64> writeTree(bool nbt, const QList<QByteArray> &leaves)
    {
        const int leafSize = m_unicode ? (nbt ? 32 : 24) : (nbt ? 16 : 12);
        const int indexSize = m_unicode ? 24 : 12;
        const int area = m_unicode ? 488 : 496;
        QList<QPair<QByteArray, QPair<quint64, qint64>>> level;   // (first key, page)
        const int perLeaf = area / leafSize;
        for (qsizetype at = 0; at < qMax<qsizetype>(1, leaves.size()); at += perLeaf) {
            const QList<QByteArray> chunk = leaves.mid(at, perLeaf);
            level.append({chunk.isEmpty() ? QByteArray(m_unicode ? 8 : 4, '\0') : chunk.first().left(m_unicode ? 8 : 4),
                          writePage(nbt, 0, chunk, leafSize)});
        }
        int depth = 0;
        const int perIndex = area / indexSize;
        while (level.size() > 1) {
            ++depth;
            QList<QPair<QByteArray, QPair<quint64, qint64>>> next;
            for (qsizetype at = 0; at < level.size(); at += perIndex) {
                QList<QByteArray> entries;
                for (qsizetype i = at; i < qMin(level.size(), at + perIndex); ++i)
                    entries.append(level.at(i).first + bid(level.at(i).second.first) + bid(quint64(level.at(i).second.second)));
                next.append({level.at(at).first, writePage(nbt, depth, entries, indexSize)});
            }
            level = next;
        }
        return level.first().second;
    }

    void writeHeader(QPair<quint64, qint64> nbt, QPair<quint64, qint64> bbt)
    {
        QByteArray h(m_unicode ? 564 : 512, '\0');
        h.replace(0, 4, "!BDN");
        put16(h, 8, 0x4D53);                 // "SM"
        put16(h, 10, m_unicode ? 23 : 14);
        put16(h, 12, 19);
        h[14] = 1;
        h[15] = 1;
        const quint64 nextBlock = (m_nextBlock + 1) << 2, nextPage = (m_nextPage + 1) << 2;
        const quint64 eof = quint64(m_file.size());
        if (m_unicode) {
            put64(h, 32, nextPage);
            put64(h, 184, eof);
            put64(h, 216, nbt.first);
            put64(h, 224, quint64(nbt.second));
            put64(h, 232, bbt.first);
            put64(h, 240, quint64(bbt.second));
            h[512] = char(0x80);
            h[513] = char(m_encoding);
            put64(h, 516, nextBlock);
            put32(h, 4, Pst::crc32(h.constData() + 8, 471));
            put32(h, 524, Pst::crc32(h.constData() + 8, 516));
        } else {
            put32(h, 24, quint32(nextBlock));
            put32(h, 28, quint32(nextPage));
            put32(h, 168, quint32(eof));
            put32(h, 184, quint32(nbt.first));
            put32(h, 188, quint32(nbt.second));
            put32(h, 192, quint32(bbt.first));
            put32(h, 196, quint32(bbt.second));
            h[460] = char(0x80);
            h[461] = char(m_encoding);
            put32(h, 4, Pst::crc32(h.constData() + 8, 471));
        }
        m_file.replace(0, h.size(), h);
    }

    bool m_unicode;
    quint8 m_encoding;
    QByteArray m_file;
    QList<Nbt> m_nbt;
    QList<Bbt> m_bbt;
    quint64 m_nextBlock = 0;
    quint64 m_nextPage = 0;
    PstLayout m_layout;
};

} // namespace QindaOffice::Mail::Test::PstBuild
