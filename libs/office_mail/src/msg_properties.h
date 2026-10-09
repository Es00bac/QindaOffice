// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One MS-OXMSG object storage — a message, an embedded message, a recipient
// or an attachment — as a property bag: fixed-size properties from its
// "__properties_version1.0" stream and variable-size ones from its
// "__substg1.0_PPPPTTTT" streams (MS-OXMSG 2.1, 2.4). Internal to
// msg_reader.cpp; not a public API of office_mail.

#include "cfb_reader.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>

#include <optional>

namespace QindaOffice::Mail::MsgDetail {

// MS-OXCDATA 2.11.1 property types.
inline constexpr quint16 kPtLong = 0x0003;
inline constexpr quint16 kPtBoolean = 0x000B;
inline constexpr quint16 kPtObject = 0x000D;
inline constexpr quint16 kPtString8 = 0x001E;
inline constexpr quint16 kPtUnicode = 0x001F;
inline constexpr quint16 kPtSysTime = 0x0040;
inline constexpr quint16 kPtBinary = 0x0102;

// AGENT-NOTE: real property streams hold a few hundred 16-byte entries;
// anything above this is hostile, and the cap bounds the hash below.
inline constexpr qint64 kMaxPropertyStreamSize = 1024 * 1024;

// The property stream header differs per object (MS-OXMSG 2.4.1).
enum class MsgObjectKind {
    TopLevel,     // 32-byte header
    Embedded,     // 24-byte header
    Recipient,    // 8-byte header
    Attachment,   // 8-byte header
};

// AGENT-GUARD: one budget for a whole readMsg() call, embedded messages
// included. Directory entries may share sectors, so a hostile file can show
// the same bytes as many large streams; the total read is capped, not only
// each stream.
struct MsgReadBudget {
    qint64 remaining = 0;
};

class MsgPropertyBag {
public:
    // Indexes the storage's children and parses its property stream. False
    // (with `*error`) only when the property stream is unreadable or too big.
    // `cfb` and `budget` must outlive the bag.
    bool load(const CfbReader &cfb, int storageId, MsgObjectKind kind, MsgReadBudget *budget,
              QString *error);
    void setString8CodePage(int codePage) { m_codePage = codePage; }

    // A property stream or at least one __substg1.0_ stream exists.
    bool hasProperties() const { return m_hasProperties; }

    std::optional<quint32> longValue(quint16 id) const;
    bool boolValue(quint16 id) const;
    QDateTime timeValue(quint16 id) const;   // invalid when absent or null

    // PT_UNICODE, else PT_STRING8 (in the string-8 code page), up to the
    // first NUL; empty when absent. A corrupt stream yields empty and sets
    // the sticky error().
    QString string(quint16 id);
    QByteArray binary(quint16 id);
    // As binary(), but a read failure is ignored (for optional extras like
    // the compressed RTF body).
    QByteArray optionalBinary(quint16 id);
    bool hasStream(quint16 id, quint16 type) const { return streamId(id, type) >= 0; }

    int childStorage(const QString &name) const;
    // Child storages named `prefix` + hex number ("__recip_version1.0_#"),
    // ordered by that number.
    QList<int> numberedStorages(const QString &prefix) const;

    bool failed() const { return !m_error.isEmpty(); }
    QString error() const { return m_error; }

private:
    int streamId(quint16 id, quint16 type) const;
    bool readStream(int entryId, QByteArray *out, QString *error);
    bool readOrRecord(int entryId, QByteArray *out);

    const CfbReader *m_cfb = nullptr;
    MsgReadBudget *m_budget = nullptr;
    QHash<QString, int> m_children;   // CfbReader::upperName(name) → entry id
    QHash<quint32, quint64> m_fixed;  // property tag → the entry's 8-byte value
    bool m_hasProperties = false;
    int m_codePage = 1252;
    QString m_error;
};

// UTF-16LE bytes → text, stopping at the first NUL; an odd last byte is dropped.
QString utf16LeToString(const QByteArray &bytes);
// FILETIME (100 ns since 1601-01-01 UTC) → UTC date-time; invalid for 0,
// out-of-range values and MAPI's "no date" (year 4501).
QDateTime fileTimeToDateTime(quint64 fileTime);

} // namespace QindaOffice::Mail::MsgDetail
