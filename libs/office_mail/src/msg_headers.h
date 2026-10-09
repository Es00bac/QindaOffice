// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// RFC 5322 helpers for the .msg reader (msg_reader.cpp): the few header
// fields it falls back to from PidTagTransportMessageHeaders (the original
// Internet headers Outlook keeps for received mail), RFC 2047 encoded words,
// and the display-string form of address lists MessageContent carries.
// Internal to office_mail; the RFC 5322 reader has its own full parser.
//
// AGENT-GUARD: the header text is untrusted. Parsing is linear and
// iterative (comment nesting is a counter, not recursion), field and
// address counts are capped, and every value returned for a single-line
// MessageContent field has passed oneLine(), so no CR/LF can reach the
// RFC 5322 writer through it.

#include <QDateTime>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringView>

namespace QindaOffice::Mail::MsgDetail {

struct MailAddress {
    QString name;      // display name, encoded words decoded
    QString address;   // addr-spec; empty when only a name is known
};

// An RFC 5322 header block: fields unfolded, names lower-cased, parsing
// stops at the first empty line; lines without a colon are skipped.
class HeaderBlock {
public:
    explicit HeaderBlock(const QString &text);
    // The first field called `lowerName`, raw (encoded words kept); empty when absent.
    QString value(QStringView lowerName) const;

private:
    QList<QPair<QString, QString>> m_fields;
};

// RFC 2047 "=?charset?B|Q?…?=" words decoded through QStringDecoder;
// whitespace between adjacent encoded words is dropped. Words in an unknown
// charset or malformed ones are kept verbatim.
QString decodeEncodedWords(const QString &text);

// Mailboxes of an address-list header value (groups flattened, comments
// dropped or used as the name of "addr (Name)"). Capped at 10 000.
QList<MailAddress> parseAddressList(const QString &value);

// `"Name" <addr>` (quoted when the name has RFC 5322 specials), `Name
// <addr>`, a bare `addr` when there is no name, or the (quoted) name alone
// when there is no address.
QString formatAddress(const QString &name, const QString &address);
QString formatAddressList(const QList<MailAddress> &addresses);

// RFC 5322 date (comments and common obsolete zone names allowed) → UTC.
QDateTime parseRfc5322Date(const QString &value);

// A single-line field: CR/LF runs and TABs become one space, other control
// characters are removed, the ends are trimmed.
QString oneLine(const QString &text);

// A message id without angle brackets: the content of the first <…> when
// present, else the trimmed text.
QString stripAngles(const QString &id);

} // namespace QindaOffice::Mail::MsgDetail
