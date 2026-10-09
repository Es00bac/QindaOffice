// SPDX-License-Identifier: GPL-3.0-or-later
// The messaging layer of an Outlook data file (pst_reader.h).

#include "pst_reader.h"

#include "mapi_mail.h"
#include "msg_properties.h"
#include "pst_objects.h"
#include "rtf_codepage.h"

namespace QindaOffice::Mail::Pst {
namespace {

// MS-PST 2.4.3 message store properties.
constexpr quint16 kPidDisplayName = 0x3001;
constexpr quint16 kPidRecordKey = 0x0FF9;
constexpr quint16 kPidIpmSubTreeEntryId = 0x35E0;
constexpr quint16 kPidContainerClass = 0x3613;
// MS-PST 2.4.7 name-to-id map streams.
constexpr quint16 kPidNameidStreamGuid = 0x0002;
constexpr quint16 kPidNameidStreamEntry = 0x0003;
constexpr quint16 kPidNameidStreamString = 0x0004;

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

quint32 le32(const QByteArray &b, qsizetype at)
{
    const auto *p = reinterpret_cast<const uchar *>(b.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

QString text(const PropertyContext &properties, quint16 id)
{
    PropertyValue value;
    QString ignored;
    if (!properties.value(id, &value, &ignored)) return {};
    if (value.type == Mapi::kPtUnicode) return MsgDetail::utf16LeToString(value.bytes);
    if (value.type == Mapi::kPtString8) {
        QByteArray bytes = value.bytes;
        const qsizetype nul = bytes.indexOf('\0');
        if (nul >= 0) bytes.truncate(nul);
        return decodeCodePage(bytes, 1252);
    }
    return {};
}

QByteArray binary(const PropertyContext &properties, quint16 id)
{
    PropertyValue value;
    QString ignored;
    if (!properties.value(id, &value, &ignored) || value.type != Mapi::kPtBinary) return {};
    return value.bytes;
}

quint32 tableNid(quint32 folder, quint32 type)
{
    return (folder & ~kNidTypeMask) | type;
}

} // namespace

PstFile::PstFile() = default;
PstFile::~PstFile() = default;

bool PstFile::open(const QString &path, QString *error)
{
    return m_db.open(path, error) && loadStore(error);
}

bool PstFile::open(QIODevice *device, QString *error)
{
    return m_db.open(device, error) && loadStore(error);
}

bool PstFile::loadProperties(const Subnode &node, PropertyContext *out, QString *error)
{
    QList<QByteArray> blocks;
    SubnodeMap subnodes;
    if (!m_db.readData(node.bidData, &blocks, error) || !m_db.readSubnodes(node.bidSub, &subnodes, error)) return false;
    return out->load(&m_db, blocks, subnodes, error);
}

bool PstFile::loadNode(quint32 nid, PropertyContext *out, QString *error)
{
    QString why;
    const std::optional<NodeEntry> node = m_db.findNode(nid, &why);
    if (!node) return fail(error, why.isEmpty() ? QStringLiteral("An item of the file is missing (0x%1)").arg(nid, 0, 16) : why);
    return loadProperties({nid, node->bidData, node->bidSub}, out, error);
}

bool PstFile::loadStore(QString *error)
{
    m_named = {};
    m_displayName.clear();
    m_recordKey.clear();
    m_ipmSubtree = kNidRootFolder;
    PropertyContext store;
    QString why;
    if (!loadNode(kNidMessageStore, &store, &why))
        return fail(error, QStringLiteral("The file has no readable message store: %1").arg(why));
    m_displayName = text(store, kPidDisplayName);
    m_recordKey = binary(store, kPidRecordKey);
    // MS-PST 2.4.3: an EntryID is rgbFlags (4), the store's uid (16), a NID (4).
    const QByteArray ipm = binary(store, kPidIpmSubTreeEntryId);
    if (ipm.size() >= 24) {
        const quint32 nid = le32(ipm, 20);
        QString ignored;
        if ((nid & kNidTypeMask) == kNidTypeNormalFolder && m_db.findNode(nid, &ignored)) m_ipmSubtree = nid;
    }
    // Named properties are optional: a file without the map reads, with the
    // fields that need it left empty.
    PropertyContext names;
    if (loadNode(kNidNameToIdMap, &names, &why))
        m_named.parse(binary(names, kPidNameidStreamGuid), binary(names, kPidNameidStreamEntry),
                      binary(names, kPidNameidStreamString));
    return true;
}

bool PstFile::loadTableRows(quint32 nid, QList<quint32> *rows, QString *error)
{
    rows->clear();
    QString why;
    const std::optional<NodeEntry> node = m_db.findNode(nid, &why);
    if (!node) return why.isEmpty() || fail(error, why);
    QList<QByteArray> blocks;
    SubnodeMap subnodes;
    TableContext table;
    if (!m_db.readData(node->bidData, &blocks, error) || !m_db.readSubnodes(node->bidSub, &subnodes, error)
        || !table.load(&m_db, blocks, subnodes, error))
        return false;
    rows->reserve(table.rowCount());
    for (int row = 0; row < table.rowCount(); ++row) rows->append(table.rowId(row));
    return true;
}

bool PstFile::readFolder(quint32 nid, Folder *out, QString *error)
{
    *out = Folder();
    out->nid = nid;
    PropertyContext properties;
    if (!loadNode(nid, &properties, error)) return false;
    out->name = text(properties, kPidDisplayName);
    out->containerClass = text(properties, kPidContainerClass);
    QList<quint32> rows;
    if (!loadTableRows(tableNid(nid, kNidTypeHierarchyTable), &rows, error)) return false;
    for (const quint32 row : std::as_const(rows)) {
        if ((row & kNidTypeMask) == kNidTypeNormalFolder) out->subfolders.append(row);
    }
    if (!loadTableRows(tableNid(nid, kNidTypeContentsTable), &rows, error)) return false;
    for (const quint32 row : std::as_const(rows)) {
        if ((row & kNidTypeMask) == kNidTypeNormalMessage) out->items.append(row);
    }
    return true;
}

std::unique_ptr<Mapi::Message> PstFile::openMessage(quint32 nid, QString *error)
{
    PropertyContext properties;
    if (!loadNode(nid, &properties, error)) return nullptr;
    auto budget = std::make_shared<ReadBudget>();
    budget->remaining = Mapi::kMaxContentBytes;
    return std::make_unique<PstMessage>(this, std::move(properties), std::move(budget), 0);
}

} // namespace QindaOffice::Mail::Pst
