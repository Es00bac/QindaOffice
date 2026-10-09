// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The node database (NDB) layer of an Outlook data file (M76, D-085;
// MS-PST 2.2): the header, the node and block B-trees, blocks with their
// trailers, the two block encodings, data trees (XBLOCK / XXBLOCK) and
// subnode trees (SLBLOCK / SIBLOCK). The LTP layer (pst_ltp.h) builds heaps,
// property and table contexts on top; pst_reader.h is the messaging layer.
//
// Formats: ANSI (wVer 14/15, 32-bit offsets and identifiers) and Unicode
// (wVer 23 and later, 64-bit), 512-byte pages and 8 KiB blocks. An .ost file
// in the same format reads like a .pst; Outlook 2013's 4 KiB-page .ost
// (wVer 36) and files protected by Windows Information Protection are
// refused with a sentence that says so.
//
// AGENT-GUARD: the file is UNTRUSTED and may be many gigabytes. Nothing is
// loaded whole: pages and blocks are read on demand (seek + read) with a
// small page cache. Every offset is checked against the file size, every
// page and block against its trailer (type, identifier, size, CRC), every
// entry count against the space that holds it; B-tree descents, data trees
// and subnode trees track what they visited, so a loop is an error, not a
// hang; one node's data is capped (kMaxNodeBytes). Failures return false
// with a human-readable error; nothing asserts, throws or reads out of
// bounds.
//
// Threading: one thread at a time (the device is seeked).

#include <QByteArray>
#include <QCache>
#include <QHash>
#include <QList>
#include <QSet>
#include <QString>

#include <memory>
#include <optional>

class QFile;
class QIODevice;

namespace QindaOffice::Mail::Pst {

enum class Format { Ansi, Unicode };
enum class Encoding : quint8 { None = 0, Permute = 1, Cyclic = 2 };

// MS-PST 2.2.2.1 node identifiers: the low five bits are the type.
inline constexpr quint32 kNidTypeMask = 0x1F;
inline constexpr quint32 kNidTypeHid = 0x00;
inline constexpr quint32 kNidTypeNormalFolder = 0x02;
inline constexpr quint32 kNidTypeNormalMessage = 0x04;
inline constexpr quint32 kNidTypeAttachment = 0x05;
inline constexpr quint32 kNidTypeHierarchyTable = 0x0D;
inline constexpr quint32 kNidTypeContentsTable = 0x0E;
inline constexpr quint32 kNidMessageStore = 0x21;
inline constexpr quint32 kNidNameToIdMap = 0x61;
inline constexpr quint32 kNidRootFolder = 0x122;
inline constexpr quint32 kNidAttachmentTable = 0x671;
inline constexpr quint32 kNidRecipientTable = 0x692;

struct NodeEntry {
    quint32 nid = 0;
    quint64 bidData = 0;
    quint64 bidSub = 0;   // 0 = no subnodes
    quint32 nidParent = 0;
};

struct Subnode {
    quint32 nid = 0;
    quint64 bidData = 0;
    quint64 bidSub = 0;
};
using SubnodeMap = QHash<quint32, Subnode>;

class NodeDatabase {
public:
    static constexpr qint64 kMaxNodeBytes = 64LL * 1024 * 1024;
    static constexpr int kMaxSubnodes = 1 << 20;
    static constexpr int kMaxTreeDepth = 16;

    NodeDatabase();
    ~NodeDatabase();
    NodeDatabase(const NodeDatabase &) = delete;
    NodeDatabase &operator=(const NodeDatabase &) = delete;

    // Opens a file, or a readable random-access device the caller keeps
    // alive (tests use a QBuffer). Validates the header (signature, version,
    // CRCs, encoding, the size it records against the real size, the two
    // B-tree roots). Replaces any previous state.
    bool open(const QString &path, QString *error);
    bool open(QIODevice *device, QString *error);
    [[nodiscard]] bool isOpen() const { return m_device != nullptr; }
    [[nodiscard]] Format format() const { return m_format; }
    [[nodiscard]] Encoding encoding() const { return m_encoding; }
    [[nodiscard]] bool isOst() const { return m_ost; }
    [[nodiscard]] qint64 size() const { return m_size; }
    [[nodiscard]] bool unicode() const { return m_format == Format::Unicode; }
    // The largest data block payload: 8176 (Unicode) or 8180 (ANSI) bytes.
    [[nodiscard]] int maxBlockData() const;

    // The node-B-tree entry of `nid`: nullopt with an empty error when the
    // node does not exist, nullopt with an error when the index is corrupt.
    std::optional<NodeEntry> findNode(quint32 nid, QString *error);
    // A data tree as its external blocks, decoded, in order (heaps need the
    // block boundaries). `limit` caps the total.
    bool readData(quint64 bid, QList<QByteArray> *blocks, QString *error, qint64 limit = kMaxNodeBytes);
    bool readStream(quint64 bid, QByteArray *out, QString *error, qint64 limit = kMaxNodeBytes);
    // A subnode tree flattened to nid → entry (bid 0: no subnodes).
    bool readSubnodes(quint64 bid, SubnodeMap *out, QString *error);

private:
    bool readHeader(QString *error);
    QByteArray readAt(qint64 offset, qint64 size);
    bool readPage(quint64 ib, quint64 bid, quint8 ptype, QByteArray *page, QString *error);
    std::optional<QByteArray> lookup(bool nbt, quint64 key, QString *error);
    bool readBlock(quint64 bid, QByteArray *out, QString *error);
    bool collect(quint64 bid, int expectedLevel, QList<QByteArray> *blocks, qint64 *total, qint64 limit,
                 QSet<quint64> *visited, QString *error);
    bool collectSubnodes(quint64 bid, int expectedLevel, SubnodeMap *out, QSet<quint64> *visited, QString *error);
    [[nodiscard]] quint64 field(const QByteArray &bytes, qsizetype at) const;
    [[nodiscard]] bool crcMatches(const QByteArray &bytes, qsizetype size, quint32 stored) const;
    void close();

    std::unique_ptr<QFile> m_file;
    QIODevice *m_device = nullptr;
    Format m_format = Format::Unicode;
    Encoding m_encoding = Encoding::None;
    bool m_ost = false;
    qint64 m_size = 0;
    quint64 m_nbtBid = 0, m_nbtIb = 0, m_bbtBid = 0, m_bbtIb = 0;
    QCache<quint64, QByteArray> m_pages;   // verified B-tree pages by offset
};

} // namespace QindaOffice::Mail::Pst
