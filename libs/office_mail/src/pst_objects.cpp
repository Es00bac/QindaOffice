// SPDX-License-Identifier: GPL-3.0-or-later
// Mapi:: property bags over a .pst (pst_objects.h).

#include "pst_objects.h"

#include "mapi_mail.h"
#include "msg_properties.h"
#include "pst_reader.h"
#include "rtf_codepage.h"

#include <bit>
#include <cstring>

namespace QindaOffice::Mail::Pst {
namespace {

constexpr int kMaxMultiValues = 4096;

quint32 le32(const QByteArray &b, qsizetype at)
{
    const auto *p = reinterpret_cast<const uchar *>(b.constData() + at);
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

quint64 le64(const QByteArray &b, qsizetype at)
{
    return quint64(le32(b, at)) | (quint64(le32(b, at + 4)) << 32);
}

// MS-PST 2.3.3.4.2: ulCount, ulCount offsets from the start, the items.
QList<QByteArray> splitMultiValue(const QByteArray &bytes)
{
    if (bytes.size() < 4) return {};
    const quint32 count = le32(bytes, 0);
    if (count == 0 || count > kMaxMultiValues || 4 + qint64(count) * 4 > bytes.size()) return {};
    QList<QByteArray> items;
    items.reserve(qsizetype(count));
    quint32 previous = 4 + count * 4;
    for (quint32 i = 0; i < count; ++i) {
        const quint32 start = le32(bytes, 4 + qsizetype(i) * 4);
        const quint32 end = i + 1 < count ? le32(bytes, 4 + qsizetype(i + 1) * 4) : quint32(bytes.size());
        if (start < previous || end < start || end > quint32(bytes.size())) return {};
        items.append(bytes.mid(start, end - start));
        previous = start;
    }
    return items;
}

QString decodeString8(QByteArray bytes, int codePage)
{
    const qsizetype nul = bytes.indexOf('\0');
    if (nul >= 0) bytes.truncate(nul);
    return decodeCodePage(bytes, codePage);
}

} // namespace

template <typename Base>
bool ValueBag<Base>::get(quint16 id, quint16 type, QByteArray *out, bool quiet) const
{
    out->clear();
    if (typeOf(id) != type) return false;
    PropertyValue value;
    QString why;
    if (!fetch(id, &value, &why)) {
        if (!why.isEmpty() && !quiet && m_error.isEmpty()) m_error = why;
        return false;
    }
    if (value.bytes.size() > m_budget->remaining) {
        if (!quiet && m_error.isEmpty()) m_error = QStringLiteral("The message holds more content than the reader allows");
        return false;
    }
    m_budget->remaining -= value.bytes.size();
    *out = std::move(value.bytes);
    return true;
}

template <typename Base>
std::optional<quint32> ValueBag<Base>::longValue(quint16 id) const
{
    QByteArray bytes;
    if (!get(id, Mapi::kPtLong, &bytes) || bytes.size() < 4) return std::nullopt;
    return le32(bytes, 0);
}

template <typename Base>
std::optional<double> ValueBag<Base>::doubleValue(quint16 id) const
{
    QByteArray bytes;
    if (!get(id, Mapi::kPtDouble, &bytes) || bytes.size() != 8) return std::nullopt;
    return std::bit_cast<double>(le64(bytes, 0));
}

template <typename Base>
bool ValueBag<Base>::boolValue(quint16 id) const
{
    QByteArray bytes;
    return get(id, Mapi::kPtBoolean, &bytes) && !bytes.isEmpty() && bytes.at(0) != 0;
}

template <typename Base>
QDateTime ValueBag<Base>::timeValue(quint16 id) const
{
    QByteArray bytes;
    if (!get(id, Mapi::kPtSysTime, &bytes) || bytes.size() != 8) return {};
    return MsgDetail::fileTimeToDateTime(le64(bytes, 0));
}

template <typename Base>
QString ValueBag<Base>::string(quint16 id)
{
    QByteArray bytes;
    if (get(id, Mapi::kPtUnicode, &bytes)) return MsgDetail::utf16LeToString(bytes);
    if (get(id, Mapi::kPtString8, &bytes)) return decodeString8(bytes, m_codePage);
    return {};
}

template <typename Base>
QByteArray ValueBag<Base>::binary(quint16 id)
{
    QByteArray bytes;
    get(id, Mapi::kPtBinary, &bytes);
    return bytes;
}

template <typename Base>
QByteArray ValueBag<Base>::optionalBinary(quint16 id)
{
    QByteArray bytes;
    get(id, Mapi::kPtBinary, &bytes, true);
    return bytes;
}

template <typename Base>
QStringList ValueBag<Base>::strings(quint16 id)
{
    QByteArray bytes;
    QStringList out;
    if (get(id, Mapi::kPtMvUnicode, &bytes)) {
        for (const QByteArray &item : splitMultiValue(bytes)) out.append(MsgDetail::utf16LeToString(item));
    } else if (get(id, Mapi::kPtMvString8, &bytes)) {
        for (const QByteArray &item : splitMultiValue(bytes)) out.append(decodeString8(item, m_codePage));
    }
    return out;
}

template <typename Base>
QList<QByteArray> ValueBag<Base>::binaries(quint16 id)
{
    QByteArray bytes;
    return get(id, Mapi::kPtMvBinary, &bytes) ? splitMultiValue(bytes) : QList<QByteArray>();
}

template class ValueBag<Mapi::Message>;
template class ValueBag<Mapi::Attachment>;
template class ValueBag<Mapi::PropertyBag>;

PstMessage::PstMessage(PstFile *file, PropertyContext properties, std::shared_ptr<ReadBudget> budget, int depth)
    : ValueBag(std::move(budget))
    , m_file(file)
    , m_properties(std::move(properties))
    , m_depth(depth)
{
}

bool PstMessage::fetch(quint16 id, PropertyValue *out, QString *error) const
{
    return m_properties.value(id, out, error, qMin(m_budget->remaining + 1, NodeDatabase::kMaxNodeBytes));
}

const Mapi::NamedPropertyMap &PstMessage::namedProperties() const
{
    return m_file->namedProperties();
}

bool PstMessage::recipients(std::vector<std::unique_ptr<Mapi::PropertyBag>> *out, QString *error)
{
    out->clear();
    const auto it = m_properties.subnodes().constFind(kNidRecipientTable);
    if (it == m_properties.subnodes().cend()) return true;
    QList<QByteArray> blocks;
    SubnodeMap subnodes;
    auto table = std::make_shared<TableContext>();
    NodeDatabase &db = m_file->database();
    if (!db.readData(it->bidData, &blocks, error) || !db.readSubnodes(it->bidSub, &subnodes, error)
        || !table->load(&db, blocks, subnodes, error))
        return false;
    if (table->rowCount() > Mapi::kMaxRecipients) {
        if (error) *error = QStringLiteral("A message has more than %1 recipients").arg(Mapi::kMaxRecipients);
        return false;
    }
    for (int row = 0; row < table->rowCount(); ++row) out->push_back(std::make_unique<PstRow>(table, row, m_budget));
    return true;
}

bool PstMessage::attachments(std::vector<std::unique_ptr<Mapi::Attachment>> *out, QString *error)
{
    out->clear();
    const SubnodeMap &subnodes = m_properties.subnodes();
    const auto it = subnodes.constFind(kNidAttachmentTable);
    if (it == subnodes.cend()) return true;
    QList<QByteArray> blocks;
    SubnodeMap tableSubnodes;
    TableContext table;
    NodeDatabase &db = m_file->database();
    if (!db.readData(it->bidData, &blocks, error) || !db.readSubnodes(it->bidSub, &tableSubnodes, error)
        || !table.load(&db, blocks, tableSubnodes, error))
        return false;
    if (table.rowCount() > Mapi::kMaxAttachments) {
        if (error) *error = QStringLiteral("A message has more than %1 attachments").arg(Mapi::kMaxAttachments);
        return false;
    }
    for (int row = 0; row < table.rowCount(); ++row) {
        const auto node = subnodes.constFind(table.rowId(row));
        if (node == subnodes.cend()) {
            if (error) *error = QStringLiteral("An attachment of the message is missing");
            return false;
        }
        PropertyContext properties;
        if (!m_file->loadProperties(*node, &properties, error)) return false;
        out->push_back(std::make_unique<PstAttachment>(m_file, std::move(properties), m_budget, m_depth));
    }
    return true;
}

PstAttachment::PstAttachment(PstFile *file, PropertyContext properties, std::shared_ptr<ReadBudget> budget, int depth)
    : ValueBag(std::move(budget))
    , m_file(file)
    , m_properties(std::move(properties))
    , m_depth(depth)
{
}

bool PstAttachment::fetch(quint16 id, PropertyValue *out, QString *error) const
{
    return m_properties.value(id, out, error, qMin(m_budget->remaining + 1, NodeDatabase::kMaxNodeBytes));
}

std::unique_ptr<Mapi::Message> PstAttachment::embeddedMessage(QString *error)
{
    // MS-PST 2.3.3.5: a PtypObject value is {subnode id, size} in the heap;
    // the subnode is the embedded message's property context.
    if (m_properties.typeOf(0x3701) != Mapi::kPtObject) return nullptr;
    PropertyValue value;
    QString why;
    if (!m_properties.value(0x3701, &value, &why) || value.bytes.size() < 4) {
        if (error) *error = why.isEmpty() ? QStringLiteral("An attached message is malformed") : why;
        return nullptr;
    }
    if (m_depth + 1 > Mapi::kMaxEmbeddingDepth) {
        if (error) *error = QStringLiteral("Attached messages are nested more than %1 deep").arg(Mapi::kMaxEmbeddingDepth);
        return nullptr;
    }
    const auto node = m_properties.subnodes().constFind(le32(value.bytes, 0));
    if (node == m_properties.subnodes().cend()) {
        if (error) *error = QStringLiteral("An attached message is missing");
        return nullptr;
    }
    PropertyContext properties;
    if (!m_file->loadProperties(*node, &properties, error)) return nullptr;
    return std::make_unique<PstMessage>(m_file, std::move(properties), m_budget, m_depth + 1);
}

PstRow::PstRow(std::shared_ptr<const TableContext> table, int row, std::shared_ptr<ReadBudget> budget)
    : ValueBag(std::move(budget))
    , m_table(std::move(table))
    , m_row(row)
{
}

quint16 PstRow::typeOf(quint16 id) const
{
    return m_table->cellExists(m_row, id) ? m_table->columnType(id) : 0;
}

bool PstRow::fetch(quint16 id, PropertyValue *out, QString *error) const
{
    return m_table->cell(m_row, id, out, error, qMin(m_budget->remaining + 1, NodeDatabase::kMaxNodeBytes));
}

} // namespace QindaOffice::Mail::Pst
