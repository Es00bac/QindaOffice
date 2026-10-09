// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// MessageContent → RFC 5322 source (M64, D-072): how an imported Outlook
// .msg becomes an ordinary message QindaMail stores, shows and saves as
// .eml (which Outlook opens again). QindaTK::Mime::build cannot write inline
// (Content-ID) parts, multipart/related, attached messages or a given Date
// and Bcc (toolkit follow-up), so the suite writes its own.
//
// AGENT-CONTRACT (the QindaMail app's message import): the output is
// DETERMINISTIC — boundaries and a missing Message-ID derive from the
// content — so converting the same .msg twice gives the same bytes, and the
// app's content-hash id keeps a re-import from duplicating the message.
// Output lines end in CRLF; text bodies are 7bit when clean, else base64;
// attachments are base64; an embedded message is a message/rfc822 part.

#include "message_content.h"

#include <QByteArray>
#include <QString>

namespace QindaOffice::Mail {

QByteArray writeRfc822(const MessageContent &message);

// "<hash>@qindamail.invalid" from subject, sender, date and body, for a
// message without a Message-ID (no angle brackets).
QString derivedMessageId(const MessageContent &message);

} // namespace QindaOffice::Mail
