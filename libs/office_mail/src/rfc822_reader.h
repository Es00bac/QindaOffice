// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// RFC 5322 / MIME source → MessageContent (M64, D-072): what QindaMail needs
// to show, print and save a message that QindaTK::Mime::parse leaves out —
// the multipart/alternative and multipart/related structure, Content-ID and
// the inline disposition of each part, Bcc, Sender/Reply-To and an iMIP
// text/calendar part.
//
// AGENT-CONTRACT (QindaTK mail_mime_parse.cpp; the QindaMail app):
// `MessageAttachment::section` numbers parts exactly as QindaTK::Mime::parse
// does (multipart children 1..n, nested "1.2", a single leaf "1",
// message/rfc822 parts are leaves, at most 8 multipart levels), so the app
// maps a part to the provider record's attachment id "<recordId>/<section>".
// Unnamed parts are named "part-<section>" like the toolkit names them.
//
// Never fails: unreadable structure degrades to what could be read. Input
// size is the caller's bound (the app refuses files above 64 MiB).

#include "message_content.h"

#include <QByteArray>

namespace QindaOffice::Mail {

MessageContent readRfc822(const QByteArray &raw);

// The decoded Subject of an RFC 5322 source without walking its body.
QString rfc822Subject(const QByteArray &raw);

} // namespace QindaOffice::Mail
