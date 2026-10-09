// SPDX-License-Identifier: GPL-3.0-or-later
// CardDAV: the sync dialogue of one or more address books (carddav_jobs.h).

#include "carddav_dialogue_p.h"

#include <QHash>
#include <QSet>
#include <QUuid>

namespace QindaOffice::Mail::CardDav {

using QindaTK::Protocol::Failure;
using QindaTK::Protocol::HttpResponse;

namespace {

constexpr int kMultigetBatch = 50;

using Detail::Dialogue;
using Detail::pathKey;
using Detail::selfOf;

// The server's cards for `hrefs` (multiget, else one GET each): changed
// versions, and the hrefs it no longer has.
bool fetch(Dialogue &d, const QUrl &book, const QStringList &hrefs, QList<RemoteCard> *changed, QStringList *removed)
{
    for (qsizetype at = 0; at < hrefs.size(); at += kMultigetBatch) {
        const QStringList batch = hrefs.mid(at, kMultigetBatch);
        Multistatus ms;
        int status = 0;
        if (!d.multistatus("REPORT", book, 1, multiget(batch), &ms, &status)) return false;
        if (status == 207) {
            for (const DavResponse &response : std::as_const(ms.responses)) {
                const QString key = hrefKey(book, response.href);
                if (response.status == 404 || (response.addressData.isEmpty() && response.status != 0)) {
                    if (!removed->contains(key)) removed->append(key);
                } else if (!response.addressData.isEmpty()) {
                    changed->append({key, response.etag, response.addressData});
                }
            }
            continue;
        }
        for (const QString &href : batch) {   // no multiget here: GET each card
            HttpResponse response;
            if (!d.request("GET", book.resolved(QUrl(href)), {}, {}, &response)) return false;
            if (response.status == 404 || response.status == 410) {
                if (!removed->contains(href)) removed->append(href);
            } else if (response.status >= 200 && response.status < 300) {
                changed->append({href, QString::fromUtf8(response.headers.value("etag")), response.body});
            } else {
                d.fail(Failure::Rejected, QStringLiteral("Reading a contact failed (HTTP %1)").arg(response.status));
                return false;
            }
        }
    }
    return true;
}

// A new card's file name: its UID when that is a safe name, else a UUID.
QString cardName(const LocalCard &card)
{
    QString base = card.uid.startsWith(QLatin1String("urn:uuid:")) ? card.uid.mid(9) : card.uid;
    for (const QChar c : std::as_const(base)) {
        if (!(c.isLetterOrNumber() && c.unicode() < 128) && c != u'-' && c != u'_' && c != u'.') {
            base.clear();
            break;
        }
    }
    if (base.isEmpty() || base.size() > 100) base = QUuid::createUuid().toString(QUuid::WithoutBraces);
    return base + QStringLiteral(".vcf");
}

bool push(Dialogue &d, const BookSync &book, const LocalCard &card, BookResult *r)
{
    const QMap<QByteArray, QByteArray> vcard{{"Content-Type", "text/vcard; charset=utf-8"}};
    HttpResponse response;
    if (card.href.isEmpty()) {
        QUrl target = book.url.resolved(QUrl(cardName(card)));
        for (int attempt = 0; attempt < 2; ++attempt) {
            QMap<QByteArray, QByteArray> headers = vcard;
            headers.insert("If-None-Match", "*");
            if (!d.request("PUT", target, headers, card.vcard, &response)) return false;
            if (response.status != 412) break;
            target = book.url.resolved(QUrl(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".vcf")));
        }
        if (response.status >= 200 && response.status < 300)
            r->pushed.append({card.contactId, hrefKey(book.url, target.path(QUrl::FullyEncoded)),
                              QString::fromUtf8(response.headers.value("etag"))});
        else
            r->refused.append(card.contactId);
        return true;
    }
    QMap<QByteArray, QByteArray> headers = vcard;
    if (!card.etag.isEmpty()) headers.insert("If-Match", card.etag.toUtf8());
    if (!d.request("PUT", book.url.resolved(QUrl(card.href)), headers, card.vcard, &response)) return false;
    const QString key = hrefKey(book.url, card.href);
    if (response.status >= 200 && response.status < 300) {
        r->pushed.append({card.contactId, key, QString::fromUtf8(response.headers.value("etag"))});
    } else if (response.status == 412 || response.status == 404 || response.status == 410) {
        // Changed (or deleted) on the server since we last saw it: the
        // server wins, the local version becomes a copy.
        r->conflicts.append(card.contactId);
        if (response.status == 412) return fetch(d, book.url, {key}, &r->changed, &r->removed);
        r->removed.append(key);
    } else {
        r->refused.append(card.contactId);
    }
    return true;
}

bool syncBook(Dialogue &d, const BookSync &book, BookResult *r)
{
    r->url = book.url.toString();
    QHash<QString, const LocalCard *> byHref;
    for (const LocalCard &card : book.cards) {
        if (!card.href.isEmpty()) byHref.insert(hrefKey(book.url, card.href), &card);
    }
    Multistatus ms;
    int status = 0;
    if (!d.multistatus("PROPFIND", book.url, 0, propfindBooks(), &ms, &status)) return false;
    if (status != 207) {
        d.fail(Failure::Rejected, status == 404 ? QStringLiteral("The address book %1 is gone from the server").arg(book.url.toDisplayString())
                                                : QStringLiteral("The address book did not answer (HTTP %1)").arg(status));
        return false;
    }
    const DavResponse self = selfOf(ms, book.url);
    r->ctag = self.ctag;
    r->syncToken = self.syncToken;
    const QString selfKey = pathKey(book.url);
    QStringList wanted;
    QSet<QString> listed;
    bool full = false;
    const auto consider = [&](const DavResponse &response) {
        const QString key = hrefKey(book.url, response.href);
        if (pathKey(book.url.resolved(QUrl(response.href))) == selfKey || response.resourceTypes.contains(QLatin1String("collection")))
            return;
        if (response.status == 404) {
            if (byHref.contains(key) && !r->removed.contains(key)) r->removed.append(key);
            return;
        }
        listed.insert(key);
        const LocalCard *local = byHref.value(key);
        if (local != nullptr && !response.etag.isEmpty() && local->etag == response.etag) return;   // ours, unchanged
        wanted.append(key);
    };
    // 1. What changed there: sync-collection from the last token; a refused
    //    token starts over; no support falls back to the ctag and etags.
    const bool canSync = self.reports.contains(QLatin1String("sync-collection"))
        || (self.reports.isEmpty() && !self.syncToken.isEmpty());
    if (canSync) {
        QStringList tokens{book.syncToken};
        if (!book.syncToken.isEmpty()) tokens.append(QString());
        for (const QString &token : std::as_const(tokens)) {
            Multistatus changes;
            int code = 0;
            if (!d.multistatus("REPORT", book.url, 0, syncCollection(token), &changes, &code)) return false;
            if (code == 207) {
                r->usedSyncCollection = true;
                full = token.isEmpty();
                if (!changes.syncToken.isEmpty()) r->syncToken = changes.syncToken;
                for (const DavResponse &response : std::as_const(changes.responses)) consider(response);
                break;
            }
            if (code != 403 && code != 409) break;   // unsupported: fall back
        }
    }
    if (!r->usedSyncCollection && (book.ctag.isEmpty() || book.ctag != self.ctag || self.ctag.isEmpty())) {
        Multistatus listing;
        int code = 0;
        if (!d.multistatus("PROPFIND", book.url, 1, propfindEtags(), &listing, &code)) return false;
        if (code != 207) {
            d.fail(Failure::Rejected, QStringLiteral("Listing the address book failed (HTTP %1)").arg(code));
            return false;
        }
        full = true;
        for (const DavResponse &response : std::as_const(listing.responses)) consider(response);
    }
    if (full) {
        for (auto it = byHref.cbegin(); it != byHref.cend(); ++it) {
            if (!listed.contains(it.key()) && !r->removed.contains(it.key())) r->removed.append(it.key());
        }
    }
    if (!fetch(d, book.url, wanted, &r->changed, &r->removed)) return false;
    // 2. Conflicts: changed here and touched there.
    QSet<QString> touched;
    for (const RemoteCard &card : std::as_const(r->changed)) touched.insert(card.href);
    for (const QString &href : std::as_const(r->removed)) touched.insert(href);
    for (const LocalCard &card : book.cards) {
        if (card.changed && !card.href.isEmpty() && touched.contains(hrefKey(book.url, card.href))) r->conflicts.append(card.contactId);
    }
    // 3. This computer's changes.
    for (const LocalCard &card : book.cards) {
        if (card.changed && !r->conflicts.contains(card.contactId) && !push(d, book, card, r)) return false;
    }
    for (const Tombstone &gone : book.deleted) {
        HttpResponse response;
        QMap<QByteArray, QByteArray> headers;
        if (!gone.etag.isEmpty()) headers.insert("If-Match", gone.etag.toUtf8());
        if (!d.request("DELETE", book.url.resolved(QUrl(gone.href)), headers, {}, &response)) return false;
        if ((response.status >= 200 && response.status < 300) || response.status == 404 || response.status == 410) {
            r->deletedHrefs.append(gone.href);
        } else if (response.status == 412) {
            // Changed on the server after it was deleted here: it comes back.
            r->deletedHrefs.append(gone.href);
            QStringList gone404;
            if (!fetch(d, book.url, {gone.href}, &r->restored, &gone404)) return false;
        }
    }
    return true;
}

} // namespace

SyncResult sync(const QList<BookSync> &books, const Credentials &credentials)
{
    SyncResult out;
    Dialogue d(credentials);
    for (const BookSync &book : books) {
        BookResult result;
        if (!syncBook(d, book, &result)) break;
        out.books.append(result);
    }
    d.finish(&out);
    return out;
}

} // namespace QindaOffice::Mail::CardDav
