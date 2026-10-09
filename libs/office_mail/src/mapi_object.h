// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One Outlook item as MAPI properties, whatever file it came from (M76,
// D-085): a message, an embedded message, a recipient row or an attachment
// as a read-only property bag. Two implementations exist — the MS-OXMSG
// storages of a .msg compound file (msg_properties.h, msg_objects.h) and the
// property and table contexts of a .pst (pst_objects.h) — and ONE mapping
// reads them: mail into MessageContent (mapi_mail.h), contacts, groups,
// appointments and tasks into their own values (outlook_items.h). The
// property ids are MS-OXPROPS'; both formats share them.
//
// AGENT-CONTRACT (msg_objects.cpp, pst_objects.cpp ⇄ mapi_mail.cpp,
// outlook_*.cpp): a getter answers only for its exact property type
// (longValue → PtypInteger32, string → PtypString then PtypString8, …) and
// returns empty/nullopt when the property is absent; a corrupt value makes
// failed() true with error() set, and the mappers fail the item then
// (optionalBinary() is the exception: its failure is ignored).
//
// Threading: an object and everything it returns belong to one thread.

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <memory>
#include <optional>
#include <vector>

namespace QindaOffice::Mail::Mapi {

// MS-OXCDATA 2.11.1 property types.
inline constexpr quint16 kPtInt16 = 0x0002;
inline constexpr quint16 kPtLong = 0x0003;
inline constexpr quint16 kPtDouble = 0x0005;
inline constexpr quint16 kPtBoolean = 0x000B;
inline constexpr quint16 kPtObject = 0x000D;
inline constexpr quint16 kPtInt64 = 0x0014;
inline constexpr quint16 kPtString8 = 0x001E;
inline constexpr quint16 kPtUnicode = 0x001F;
inline constexpr quint16 kPtSysTime = 0x0040;
inline constexpr quint16 kPtGuid = 0x0048;
inline constexpr quint16 kPtBinary = 0x0102;
inline constexpr quint16 kPtMvLong = 0x1003;
inline constexpr quint16 kPtMvString8 = 0x101E;
inline constexpr quint16 kPtMvUnicode = 0x101F;
inline constexpr quint16 kPtMvBinary = 0x1102;

class NamedPropertyMap;

class PropertyBag {
public:
    virtual ~PropertyBag() = default;

    [[nodiscard]] virtual bool has(quint16 id, quint16 type) const = 0;
    [[nodiscard]] virtual std::optional<quint32> longValue(quint16 id) const = 0;
    [[nodiscard]] virtual std::optional<double> doubleValue(quint16 id) const = 0;
    [[nodiscard]] virtual bool boolValue(quint16 id) const = 0;
    // UTC; invalid when absent, zero, out of range or MAPI's "none" (4501).
    [[nodiscard]] virtual QDateTime timeValue(quint16 id) const = 0;
    // PtypString, else PtypString8 in string8CodePage(), up to the first NUL.
    virtual QString string(quint16 id) = 0;
    virtual QByteArray binary(quint16 id) = 0;
    virtual QByteArray optionalBinary(quint16 id) = 0;
    // PtypMultipleString (or …String8) and PtypMultipleBinary.
    virtual QStringList strings(quint16 id) = 0;
    virtual QList<QByteArray> binaries(quint16 id) = 0;
    virtual void setString8CodePage(int codePage) = 0;
    [[nodiscard]] virtual int string8CodePage() const = 0;
    [[nodiscard]] virtual bool failed() const = 0;
    [[nodiscard]] virtual QString error() const = 0;
};

class Message;

class Attachment : public PropertyBag {
public:
    // The attached message object (attach method 5), or null — with
    // `*error` set when it exists but is unreadable.
    virtual std::unique_ptr<Message> embeddedMessage(QString *error) = 0;
};

class Message : public PropertyBag {
public:
    // Recipients in file order (MSG: recipient storages; PST: rows of the
    // recipient table). False with `*error` when they cannot be read.
    virtual bool recipients(std::vector<std::unique_ptr<PropertyBag>> *out, QString *error) = 0;
    virtual bool attachments(std::vector<std::unique_ptr<Attachment>> *out, QString *error) = 0;
    // The file's named-property map (never null; may be empty).
    [[nodiscard]] virtual const NamedPropertyMap &namedProperties() const = 0;
};

} // namespace QindaOffice::Mail::Mapi
