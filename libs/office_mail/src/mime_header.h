// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// RFC 5322 header blocks and RFC 2045/2231 structured header values
// (Content-Type, Content-Disposition) for QindaMail's own MIME reader
// (rfc822_reader.cpp). QindaTK's Mime::parse keeps these private; the reader
// needs Content-ID, the disposition and the multipart/related `start`
// parameter, which the toolkit drops (toolkit follow-up, D-072).
//
// AGENT-CONTRACT (rfc822_reader.cpp, mbox.cpp): headers are kept in source
// order with lower-case names and unfolded raw (still RFC 2047 encoded)
// values; splitHeaderBody accepts CRLF and bare-LF sources exactly like
// QindaTK::Mime::parse, so section numbering agrees with the toolkit.

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>

namespace QindaOffice::Mail::MimeHeader {

struct Split {
    QByteArray head;
    QByteArray body;
};

// Splits at the first empty line (CRLF CRLF or LF LF, whichever is first).
Split splitHeaderBody(const QByteArray &raw);

using Fields = QList<QPair<QByteArray, QByteArray>>;

// Unfolds (RFC 5322 §2.2.3) and lower-cases the names; malformed lines are
// skipped. At most `maxFields` fields are kept.
Fields parseFields(const QByteArray &head, int maxFields = 4096);

// The first value of `name` (lower case), or `fallback`.
QByteArray value(const Fields &fields, const char *name, const QByteArray &fallback = {});

struct Structured {
    QString main;                       // lower-cased "type/subtype" or disposition
    QMap<QString, QString> params;      // lower-case names, decoded values
};

// "text/html; charset=\"utf-8\"; name*0*=utf-8''a%20b; name*1=.txt".
Structured parseStructured(const QByteArray &raw);

// "<id@host>" → "id@host" (first bracketed id, else the trimmed value).
QString bareId(const QByteArray &raw);

} // namespace QindaOffice::Mail::MimeHeader
