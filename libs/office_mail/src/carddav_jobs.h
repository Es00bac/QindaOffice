// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// CardDAV dialogues (M76, D-085), run on a worker thread like the
// toolkit's CalDAV jobs (QindaTK D-186: one blocking exchange per request
// over Protocol::Channel, the result applied on the GUI thread by
// CardDavProvider):
// - discover(): RFC 6764 (/.well-known/carddav and its redirects), RFC 5397
//   current-user-principal, RFC 6352 addressbook-home-set, then the address
//   books of the home set. The start may be a server, a principal, a home
//   set or a book.
// - sync(): per address book, the server's changes — RFC 6578
//   sync-collection from the last token, else (unsupported, or the token
//   refused) the ctag and a listing of etags — fetched with
//   addressbook-multiget; then this computer's changes — PUT with If-Match
//   (or If-None-Match: * for a new card) and DELETE with If-Match.
//
// AGENT-CONTRACT (carddav_provider.cpp, ContactStore::applySync): conflicts
// resolve server-wins. A card changed here AND on the server (seen in the
// pull, or a 412 on the PUT) is not sent; its id is in `conflicts` and the
// server's version in `changed` (or its href in `removed` when the server
// deleted it); the store keeps the local version as a copy. A DELETE refused
// with 412 brings the server's card back (`restored`).
// AGENT-GUARD: credentials go only to https (or loopback, or with
// allowInsecureAuth) and only to the account's site (CalDavDiscovery::
// sameSite); redirects are followed for discovery only (≤ 5), each checked
// the same way. Every error and transcript line is redacted. A 401/403 is
// `authRejected` (the account runtime refreshes the credential and retries
// once); 429 and 5xx are offline and retryable.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace QindaOffice::Mail::CardDav {

struct Credentials {
    QByteArray authorization;    // the whole Authorization header value, or empty
    QStringList secrets;         // redacted from errors and transcripts
    QString site;                // the host the credentials belong to
    int timeoutMs = 15000;
    bool allowInsecureAuth = false;
};

struct Outcome {
    QString state = QStringLiteral("accepted");   // accepted | offline | rejected
    QString error;
    bool retryable = false;
    bool authRejected = false;
    QStringList transcript;
    int requests = 0;
};

struct DiscoveredBook {
    QString url;
    QString name;
};

struct Discovery : Outcome {
    QString principalUrl;
    QString homeSetUrl;
    QList<DiscoveredBook> books;
};

Discovery discover(const QUrl &start, const Credentials &credentials);

struct LocalCard {
    QString contactId;
    QString href;          // "" = not on the server yet
    QString etag;
    QString uid;           // names a new card's file
    QByteArray vcard;      // what is sent when `changed`
    bool changed = false;
};

struct Tombstone {
    QString href;
    QString etag;
};

struct BookSync {
    QUrl url;
    QString syncToken;
    QString ctag;
    QList<LocalCard> cards;
    QList<Tombstone> deleted;
};

struct RemoteCard {
    QString href;
    QString etag;
    QByteArray vcard;
};

struct Pushed {
    QString contactId;
    QString href;
    QString etag;
};

struct BookResult {
    QString url;
    QString syncToken;
    QString ctag;
    QList<RemoteCard> changed;
    QStringList removed;
    QList<Pushed> pushed;
    QStringList deletedHrefs;
    QStringList conflicts;
    QList<RemoteCard> restored;
    QStringList refused;    // contact ids the server would not take (kept, retried)
    bool usedSyncCollection = false;
};

struct SyncResult : Outcome {
    QList<BookResult> books;
};

SyncResult sync(const QList<BookSync> &books, const Credentials &credentials);

// The path a server's href names, resolved against `base` (how hrefs are
// stored and compared).
[[nodiscard]] QString hrefKey(const QUrl &base, const QString &href);

} // namespace QindaOffice::Mail::CardDav
