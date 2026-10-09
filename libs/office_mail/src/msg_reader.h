// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Outlook .msg (MS-OXMSG) reader: one compound file (cfb_reader.h) →
// MessageContent (message_content.h), so an imported .msg follows the same
// path as RFC 5322 mail from then on (store record, rendering, printing,
// Save as .eml through the RFC 5322 writer). M76 (D-085): the property
// mapping itself is mapi_mail.h, shared with the .pst reader; the storages
// are Mapi:: bags (msg_objects.h).
//
// AGENT-CONTRACT (message_content.h, the QindaMail app's import path):
// - `html` is the body AS SENT (PidTagHtml, or the HTML de-encapsulated from
//   a \fromhtml1 PidTagRtfCompressed); never sanitized here.
// - Address fields are RFC 5322 display strings built from SMTP addresses;
//   Exchange "EX" (X.500) addresses are never used as addresses — only the
//   display name is kept when no SMTP form exists.
// - Embedded messages (attach method 5) become MessageAttachment::embedded
//   with mimeType "message/rfc822"; `section`, `calendar` and
//   `calendarMethod` stay empty; meeting requests read as ordinary mail.
//
// AGENT-GUARD: the file is untrusted. Limits: 128 MiB file (CfbReader),
// 64 MiB per stream, 256 MiB of streams per message tree in total,
// embedded messages ≤ 8 deep, ≤ 1000 attachments and ≤ 10 000 recipients
// per message object. A broken RTF body never fails the message (the other
// bodies are kept); any other corrupt structure fails with a readable error.
//
// Threading: reentrant (no shared state).

#include "mapi_mail.h"
#include "message_content.h"

#include <QByteArray>
#include <QString>

namespace QindaOffice::Mail {

inline constexpr int kMaxMsgEmbeddingDepth = Mapi::kMaxEmbeddingDepth;
inline constexpr int kMaxMsgAttachments = Mapi::kMaxAttachments;
inline constexpr int kMaxMsgRecipients = Mapi::kMaxRecipients;
inline constexpr qint64 kMaxMsgContentBytes = Mapi::kMaxContentBytes;

// Reads a whole .msg file. On failure `*out` is reset and `*error` (when
// non-null) holds a human-readable reason.
bool readMsg(const QByteArray &file, MessageContent *out, QString *error);

} // namespace QindaOffice::Mail
