// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Pure draft construction for QindaMail (no store, no network): address
// lists, the Re:/Fwd: subject rules, quoting, reply / reply-all / forward
// drafts from a stored message record, and RFC 6068 mailto: URLs.
//
// AGENT-CONTRACT (RichMailComposer, SmtpMailProvider): a draft map keeps
// `to`, `cc`, `bcc` as address-list STRINGS ("Name <a@b>, c@d"), `subject`,
// plain `body`, `inReplyTo` (a bare message id, no angle brackets — the
// provider adds them) and `references` (space-separated "<id>" list). The
// service adds `id`, `accountId` and the signature; nothing here reads the
// clock except through the `now` / locale arguments the caller passes.

#include <QDateTime>
#include <QList>
#include <QLocale>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QindaOffice::Apps::Mail::Drafts {

struct Address {
    QString name;
    QString address;
};

// Splits "Name <a@b>, \"Doe, J\" <j@x>, c@d" at top-level commas and
// semicolons (never inside quotes or angle brackets). Empty parts dropped.
[[nodiscard]] QList<Address> parseAddressList(const QString &text);
// "Name <a@b>" (the name quoted when it holds a special character) or "a@b".
[[nodiscard]] QString formatAddress(const Address &address);
[[nodiscard]] QString formatAddressList(const QList<Address> &addresses);

// "Re: " unless the subject already carries a reply prefix (Re, RE, Aw, Sv,
// Antw, with an optional [n] counter); likewise "Fwd: " unless it carries a
// forward prefix (Fwd, Fw, WG, Tr, RV). An empty subject stays "Re: "/"Fwd: ".
[[nodiscard]] QString replySubject(const QString &subject);
[[nodiscard]] QString forwardSubject(const QString &subject);
// Every line prefixed with "> " (an already quoted line with ">").
[[nodiscard]] QString quoteBody(const QString &body);

// A reply to `message` (a store `messages` record). `ownAddresses` are the
// user's addresses (every account): none appears in to/cc, and a reply to a
// message the user sent goes to that message's recipients.
// `signature` is placed above the quotation ("-- \n" separator).
[[nodiscard]] QVariantMap reply(const QVariantMap &message, bool replyAll, const QStringList &ownAddresses,
                                const QString &signature, const QLocale &locale);
// A forward: empty recipients, the forwarded header block and body. The
// caller copies attachment bytes (the draft must own its attachments).
[[nodiscard]] QVariantMap forward(const QVariantMap &message, const QString &signature,
                                  const QLocale &locale);

// RFC 6068: `mailto:` addresses (percent-decoded, UTF-8) plus the `to`, `cc`,
// `bcc`, `subject`, `body` and `in-reply-to` header fields (names are case-
// insensitive; '+' is NOT a space; CRLF in a body becomes LF). Any other
// header is ignored (RFC 6068 §3: a client must not trust them). Returns
// {ok, error, to, cc, bcc, subject, body, inReplyTo}.
[[nodiscard]] QVariantMap parseMailto(const QString &url);

} // namespace QindaOffice::Apps::Mail::Drafts
