// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The WebDAV XML of CardDAV (M76, D-085): the request bodies QindaMail
// sends (RFC 4918 PROPFIND, RFC 6578 sync-collection, RFC 6352
// addressbook-multiget) and one parser for every 207 Multi-Status answer.
//
// AGENT-GUARD: answers are untrusted. The parser is namespace-aware (DAV:,
// urn:ietf:params:xml:ns:carddav, http://calendarserver.org/ns/), refuses a
// DTD (no entity expansion), and bounds depth, responses and text; a
// malformed document is an error, never a partial read taken for complete.

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace QindaOffice::Mail::CardDav {

inline constexpr int kMaxResponses = 50000;

// One <D:response>: the href, its own status (sync-collection marks removed
// members 404 there) and the properties of its 200 propstat.
struct DavResponse {
    QString href;
    int status = 0;
    QStringList resourceTypes;   // local names: collection, addressbook, principal, …
    QStringList reports;         // supported-report-set: sync-collection, addressbook-multiget, …
    QString displayName;
    QString etag;
    QString ctag;                // CalendarServer getctag
    QString syncToken;
    QString principal;           // current-user-principal href
    QString homeSet;             // addressbook-home-set href
    QByteArray addressData;      // CARDDAV address-data (a vCard)
};

struct Multistatus {
    QList<DavResponse> responses;
    QString syncToken;           // a sync-collection answer's new token
};

bool parseMultistatus(const QByteArray &xml, Multistatus *out, QString *error);

// Request bodies.
QByteArray propfindDiscovery();     // principal, home set, resource type, name
QByteArray propfindBooks();         // resource type, name, ctag, sync token, reports
QByteArray propfindEtags();         // resource type, getetag
QByteArray syncCollection(const QString &token);
QByteArray multiget(const QStringList &hrefs);

// An HTTP status line's code ("HTTP/1.1 404 Not Found" → 404), 0 when none.
[[nodiscard]] int statusCode(const QString &line);

} // namespace QindaOffice::Mail::CardDav
