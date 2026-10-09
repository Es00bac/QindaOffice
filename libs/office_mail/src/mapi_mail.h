// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Outlook mail (MS-OXCMSG / MS-OXOMSG properties) → MessageContent: the
// mapping the .msg reader (M64) and the .pst reader (M76, D-085) share, so
// an item from either file becomes the same RFC 5322 message through the
// deterministic writer (rfc822_writer.h).
//
// AGENT-CONTRACT (message_content.h, msg_reader.h, the QindaMail importers):
// - `html` is the body AS SENT (PidTagHtml, or the HTML de-encapsulated from
//   a \fromhtml1 PidTagRtfCompressed); never sanitized here.
// - Address fields are RFC 5322 display strings built from SMTP addresses;
//   Exchange "EX" (X.500) addresses are never used as addresses — only the
//   display name is kept when no SMTP form exists.
// - Embedded messages (attach method 5) become MessageAttachment::embedded
//   with mimeType "message/rfc822"; `section`, `calendar` and
//   `calendarMethod` stay empty; meeting requests read as ordinary mail.
// AGENT-GUARD: limits for every message tree — embedded messages ≤ 8 deep,
// ≤ 1000 attachments and ≤ 10 000 recipients per message object, 256 MiB of
// values in total (the bags enforce it per file format). A broken RTF body
// never fails the message (the other bodies are kept); any other corrupt
// structure fails it with a readable error.

#include "mapi_object.h"
#include "message_content.h"

#include <QString>

namespace QindaOffice::Mail::Mapi {

inline constexpr int kMaxEmbeddingDepth = 8;
inline constexpr int kMaxAttachments = 1000;
inline constexpr int kMaxRecipients = 10000;
inline constexpr qint64 kMaxContentBytes = 256LL * 1024 * 1024;

// On failure `*out` is reset and `*error` holds the reason.
bool readMailContent(Message &message, MessageContent *out, QString *error);

// Helpers the item mappings share (outlook_items.cpp).
// A usable SMTP address: contains '@' and is never an Exchange X.500 DN.
[[nodiscard]] bool usableAddress(const QString &address);
// The SMTP form of an address property triple (…SmtpAddress, …AddressType,
// …EmailAddress), or "".
QString smtpAddress(PropertyBag &bag, quint16 smtpId, quint16 typeId, quint16 addressId);

} // namespace QindaOffice::Mail::Mapi
