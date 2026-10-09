// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MS-CFB (Compound File Binary format) reader: the container of Outlook .msg
// files (msg_reader.cpp). Read-only, whole file in memory, versions 3
// (512-byte sectors) and 4 (4096-byte sectors).
//
// AGENT-GUARD: the input is UNTRUSTED (a mail attachment or a file the user
// picked). Every sector number, offset, size and directory link is checked;
// FAT, DIFAT and mini-FAT chains are walked with a visited set (a chain can
// never be longer than the sectors that exist), the directory tree is walked
// iteratively with a visited set, and sizes are capped (kMax* below).
// Corrupt input makes open()/read() return false with a human-readable
// error; nothing here may assert, throw or read out of bounds.
//
// Threading: open() mutates; the const accessors may run concurrently once
// open() has returned.

#include <QByteArray>
#include <QList>
#include <QString>

namespace QindaOffice::Mail {

class CfbReader {
public:
    static constexpr qint64 kMaxFileSize = 128LL * 1024 * 1024;
    static constexpr qint64 kMaxStreamSize = 64LL * 1024 * 1024;
    static constexpr int kMaxEntries = 65536;

    struct Entry {
        enum Type { Storage, Stream, Root };
        int id = -1;
        QString name;
        Type type = Stream;
        qint64 size = 0;           // stream size; v3: the low 32 bits only (MS-CFB 2.6.3)
        int child = -1;            // directory ids; -1 = NOSTREAM
        int left = -1;
        int right = -1;
        quint32 startSector = 0;   // first sector (a mini sector when size < 4096)
    };

    // Parses the header, the DIFAT (header entries and DIFAT sectors), the
    // FAT, the directory, the mini stream and the mini FAT, and validates
    // the directory tree. Replaces any previous state; on failure the reader
    // is closed and `*error` (when non-null) says why.
    bool open(const QByteArray &file, QString *error);
    bool isOpen() const { return m_open; }
    int majorVersion() const { return m_major; }

    // The root storage's id (0) when open, else -1.
    int root() const { return m_open ? 0 : -1; }
    // The entries of a storage (or the root) in directory-tree order; empty
    // for a stream or an invalid id.
    QList<int> children(int storageId) const;
    // A child by name, compared the MS-CFB way (simple upper-casing), or -1.
    int child(int storageId, const QString &name) const;
    // nullptr for an unallocated or out-of-range id. Valid until the next open().
    const Entry *entry(int id) const;
    // The stream's bytes (from the mini stream when size < 4096). False with
    // `*error` for a non-stream id, a size over kMaxStreamSize, or a chain
    // that is broken, loops, leaves the file or is too short for the size.
    bool read(int streamId, QByteArray *out, QString *error) const;

    // MS-CFB name comparison form: each UTF-16 unit upper-cased (simple case
    // mapping), as the directory's red-black tree orders names.
    static QString upperName(const QString &name);

private:
    bool fail(const QString &message, QString *error);
    bool loadFat(QString *error);
    bool loadDirectory(quint32 firstSector, QString *error);
    bool loadMiniStream(quint32 miniFatStart, QString *error);
    bool buildTree(QString *error);
    qint64 sectorOffset(quint32 sector, qint64 bytes) const;
    bool fatChain(quint32 start, qint64 need, QList<quint32> *chain, const QString &what,
                  QString *error) const;
    bool readRegular(quint32 start, qint64 size, QByteArray *out, const QString &what,
                     QString *error) const;
    bool readMini(quint32 start, qint64 size, QByteArray *out, const QString &what,
                  QString *error) const;

    QByteArray m_file;
    bool m_open = false;
    int m_major = 0;
    int m_shift = 9;
    qint64 m_sectorSize = 512;
    qint64 m_sectorCount = 0;          // sectors after the header, a partial last one included
    quint32 m_fatSectorCount = 0;
    quint32 m_difatStart = 0;
    QList<quint32> m_fat;              // entries for sectors that exist in the file
    QList<quint32> m_miniFat;          // entries for mini sectors inside the mini stream
    QByteArray m_miniStream;
    QList<Entry> m_entries;            // by directory id; unallocated entries have id -1
    QList<QString> m_upperNames;
    QList<QList<int>> m_children;      // per storage id, in tree order
};

} // namespace QindaOffice::Mail
