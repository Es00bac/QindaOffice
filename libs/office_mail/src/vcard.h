// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// vCard import/export for the address book (M64): RFC 6350 (4.0),
// RFC 2426 (3.0) and the vCard 2.1 that Outlook and old phones still write.
//
// Reading is lenient and bounded (ContactLimits): many cards per file, a
// UTF-8 / UTF-16LE / UTF-16BE BOM honoured (UTF-8 otherwise; 8-bit values
// that are not UTF-8 read as windows-1252), 2.1 bare parameters,
// QUOTED-PRINTABLE soft breaks, CHARSET=, the 2.1 base64 block, 3.0
// ENCODING=b, 4.0 data: URIs, Apple's itemN.X-ABLabel custom labels.
// Every property the Contact fields do not map is kept in Contact::extra
// (VERSION, PRODID, BEGIN and END are the only ones dropped), so
// read(write(c)) == c. A card without a name, organization or e-mail is
// skipped and counted; a card without END:VCARD is read up to the next
// BEGIN or the end of the data (with a warning); a card without VERSION is
// read as 3.0 (with a warning); an embedded vCard (2.1 AGENT) is dropped.
// Remote PHOTO URIs are kept as text and never fetched.
//
// Writing produces UTF-8 with CRLF line breaks, folded at 75 octets, FN
// always (the effective display name) and N always in 3.0; UID is the
// contact's uid, else its store id.
//
// Threading: pure functions, any thread.

#include "contact.h"

#include <QByteArray>
#include <QList>
#include <QStringList>

namespace QindaOffice::Mail::VCard {

struct ReadResult {
    QList<Contact> contacts;
    QStringList warnings;   // one line each, at most kMaxWarnings (+ a summary line)
    int skipped = 0;        // cards that were not usable or did not fit the bounds
};

[[nodiscard]] ReadResult read(const QByteArray &data);

enum class Version { V30, V40 };

// 3.0 is the interchange default (Outlook, Google, Apple all read it).
[[nodiscard]] QByteArray write(const QList<Contact> &contacts, Version version = Version::V30);

} // namespace QindaOffice::Mail::VCard
