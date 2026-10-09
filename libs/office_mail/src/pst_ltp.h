// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The lists, tables and properties (LTP) layer of an Outlook data file
// (M76, D-085; MS-PST 2.3): a node's data blocks as a heap-on-node (HN), the
// B-tree-on-heap (BTH) inside it, and the two clients the messaging layer
// reads — property contexts (one object's properties) and table contexts
// (folder hierarchies and contents, recipients, attachments).
//
// AGENT-GUARD: every heap id, allocation, record, column and row is checked
// against the bytes that hold it; B-trees on heaps are walked with their
// declared depth (≤ 8) and a visited set; values that live in subnodes go
// through NodeDatabase with its size cap. Corrupt structures return false
// with an error and are never read out of bounds.
//
// Threading: like NodeDatabase, one thread at a time.

#include "pst_ndb.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

namespace QindaOffice::Mail::Pst {

inline constexpr quint8 kHeapTableContext = 0x7C;
inline constexpr quint8 kHeapPropertyContext = 0xBC;
inline constexpr quint16 kPidLtpRowId = 0x67F2;

class Heap {
public:
    bool load(const QList<QByteArray> &blocks, QString *error);
    [[nodiscard]] quint8 clientSignature() const { return m_client; }
    [[nodiscard]] quint32 userRoot() const { return m_root; }
    // The bytes of heap allocation `hid`; false for a bad or null id.
    bool allocation(quint32 hid, QByteArray *out, QString *error) const;

private:
    struct Page {
        QByteArray data;
        QList<quint16> offsets;   // rgibAlloc: allocation i spans [i-1, i)
    };
    QList<Page> m_pages;
    quint8 m_client = 0;
    quint32 m_root = 0;
};

// A property value as stored: a small fixed value's little-endian bytes,
// or the bytes its HNID refers to (heap allocation or subnode).
struct PropertyValue {
    quint16 type = 0;
    QByteArray bytes;
};

// Shared by both contexts: a heap id or a subnode id → its bytes.
bool readHnid(NodeDatabase *db, const Heap &heap, const SubnodeMap &subnodes, quint32 hnid, QByteArray *out,
              QString *error, qint64 limit);

// MS-PST 2.3.3 property context.
class PropertyContext {
public:
    bool load(NodeDatabase *db, const QList<QByteArray> &blocks, const SubnodeMap &subnodes, QString *error);
    [[nodiscard]] quint16 typeOf(quint16 id) const { return m_entries.value(id).type; }
    [[nodiscard]] QList<quint16> ids() const { return m_entries.keys(); }
    // False with an empty error when absent; with an error when unreadable.
    bool value(quint16 id, PropertyValue *out, QString *error, qint64 limit = NodeDatabase::kMaxNodeBytes) const;
    [[nodiscard]] const SubnodeMap &subnodes() const { return m_subnodes; }

private:
    struct Entry {
        quint16 type = 0;
        quint32 raw = 0;
    };
    NodeDatabase *m_db = nullptr;
    Heap m_heap;
    SubnodeMap m_subnodes;
    QHash<quint16, Entry> m_entries;
};

// MS-PST 2.3.4 table context.
class TableContext {
public:
    static constexpr int kMaxRows = 2000000;

    bool load(NodeDatabase *db, const QList<QByteArray> &blocks, const SubnodeMap &subnodes, QString *error);
    [[nodiscard]] int rowCount() const { return m_rows; }
    [[nodiscard]] quint32 rowId(int row) const;
    [[nodiscard]] quint16 columnType(quint16 id) const;
    // The column exists and this row's cell-existence bit is set.
    [[nodiscard]] bool cellExists(int row, quint16 id) const;
    // False with an empty error when the cell is absent.
    bool cell(int row, quint16 id, PropertyValue *out, QString *error, qint64 limit = NodeDatabase::kMaxNodeBytes) const;

private:
    struct Column {
        quint32 tag = 0;
        quint16 ib = 0;
        quint8 cb = 0;
        quint8 bit = 0;
    };
    [[nodiscard]] const char *rowData(int row) const;

    NodeDatabase *m_db = nullptr;
    Heap m_heap;
    SubnodeMap m_subnodes;
    QHash<quint16, Column> m_columns;
    QList<QByteArray> m_matrix;     // the row matrix: one heap allocation or the subnode's blocks
    QList<int> m_firstRow;          // first row of each matrix block
    int m_rowSize = 0;
    int m_cebStart = 0;
    int m_rows = 0;
};

// True for types whose value is not stored inline (strings, binary, objects,
// GUIDs and every multi-valued type).
[[nodiscard]] bool isVariableType(quint16 type);

} // namespace QindaOffice::Mail::Pst
