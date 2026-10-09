// SPDX-License-Identifier: GPL-3.0-or-later
// ContactStore and CardDAV (M76, D-085): one address book's snapshot for the
// sync and the sync's result applied in one write. See contact_store.h and
// carddav_jobs.h (the conflict rule: server wins, the local version is kept
// as a copy in the "Sync conflict" category).

#include "contact_store.h"

#include "vcard.h"

#include <QCryptographicHash>
#include <QJsonArray>

#include <utility>

namespace QindaOffice::Mail {

using namespace Qt::StringLiterals;

namespace {

QString bookKey(const QString &account, const QString &book)
{
    return account + u'\n' + book;
}

} // namespace

QString ContactStore::syncDigest(const QByteArray &vcard)
{
    return QString::fromLatin1(QCryptographicHash::hash(vcard, QCryptographicHash::Sha256).toHex());
}

QByteArray ContactStore::syncCard(const Contact &contact, const QByteArray &photo) const
{
    Contact card = contact;
    card.photo = photo;
    return VCard::write({card}, VCard::Version::V30);
}

ContactStore::SyncSnapshot ContactStore::syncSnapshot(const QString &account, const QString &book) const
{
    ensureLoaded();
    SyncSnapshot s;
    const QJsonObject state = m_rootExtra.value("carddav"_L1).toObject();
    const QJsonObject position = state.value("books"_L1).toObject().value(bookKey(account, book)).toObject();
    s.syncToken = position.value("syncToken"_L1).toString();
    s.ctag = position.value("ctag"_L1).toString();
    for (const Contact &c : m_contacts) {
        if (c.sync.account != account || c.sync.book != book) continue;
        SyncCard card{c.id, c.sync.href, c.sync.etag, c.uid.isEmpty() ? c.id : c.uid, syncCard(c, readPhotoFile(c.photoFile)), false};
        card.changed = c.sync.href.isEmpty() || syncDigest(card.vcard) != c.sync.digest;
        s.cards.append(card);
    }
    for (const QJsonValue &value : state.value("deleted"_L1).toArray()) {
        const QJsonObject o = value.toObject();
        if (o.value("account"_L1).toString() == account && o.value("book"_L1).toString() == book)
            s.deleted.append({o.value("href"_L1).toString(), o.value("etag"_L1).toString()});
    }
    return s;
}

QVariantMap ContactStore::applySync(const QString &account, const QString &book, const SyncApply &a)
{
    ensureLoaded();
    if (!m_readOnlyReason.isEmpty()) return {{u"ok"_s, false}, {u"error"_s, readOnlyError()}};
    QList<Contact> next = m_contacts;
    QStringList written, warnings, conflictNames;
    int created = 0, updated = 0, deleted = 0;
    const auto byId = [&next](const QString &id) -> qsizetype {
        for (qsizetype i = 0; i < next.size(); ++i)
            if (next.at(i).id == id) return i;
        return -1;
    };
    const auto byHref = [&](const QString &href) -> qsizetype {
        for (qsizetype i = 0; i < next.size(); ++i) {
            const ContactSync &s = next.at(i).sync;
            if (s.account == account && s.book == book && s.href == href) return i;
        }
        return -1;
    };
    // 1. Our creates and updates are on the server now.
    for (const auto &[id, href, etag] : a.pushed) {
        const qsizetype i = byId(id);
        if (i < 0) continue;
        next[i].sync.href = href;
        next[i].sync.etag = etag;
        next[i].sync.digest = a.sentDigests.value(id);
    }
    // 2. A local change that lost to the server stays, as a copy.
    for (const QString &id : a.conflicts) {
        const qsizetype i = byId(id);
        if (i < 0) continue;
        Contact copy = next.at(i);
        copy.id = newId();
        copy.uid.clear();
        copy.sync = ContactSync();
        if (!copy.categories.contains(QLatin1String(kConflictCategory))) copy.categories.append(QLatin1String(kConflictCategory));
        next.append(copy);
        conflictNames.append(copy.effectiveDisplayName());
    }
    // 3. The server's versions (and cards whose deletion it refused).
    for (const RemoteVCard &remote : a.changed + a.restored) {
        const VCard::ReadResult read = VCard::read(remote.vcard);
        if (read.contacts.isEmpty()) {
            warnings.append(u"A contact from the server could not be read (%1)."_s.arg(remote.href));
            continue;
        }
        Contact incoming = read.contacts.first();
        normalizeContact(incoming);
        incoming.emails.removeIf([](const ContactValue &e) { return !isPlausibleEmail(e.value); });
        for (QString *date : {&incoming.birthday, &incoming.anniversary}) {
            if (normalizedContactDate(*date) != *date) date->clear();
        }
        if (!validateContact(incoming).isEmpty()) {
            warnings.append(u"“%1” from the server was not stored."_s.arg(incoming.effectiveDisplayName()));
            continue;
        }
        QByteArray photo = std::exchange(incoming.photo, QByteArray());
        if (photo.size() > ContactLimits::kMaxPhotoBytes) photo.clear();
        const auto stored = photo.isEmpty() ? std::nullopt
                                            : storePhoto(photo, incoming.photoMimeType, incoming.effectiveDisplayName(), &warnings, &written);
        incoming.photoFile = stored ? stored->first : QString();
        incoming.photoMimeType = stored ? stored->second : QString();
        if (!stored) photo.clear();
        qsizetype i = byHref(remote.href);
        if (i >= 0) {
            incoming.id = next.at(i).id;
            incoming.jsonExtra = next.at(i).jsonExtra;
            next[i] = incoming;
            ++updated;
        } else {
            incoming.id = newId();
            next.append(incoming);
            i = next.size() - 1;
            ++created;
        }
        next[i].sync = {account, book, remote.href, remote.etag, QString()};
        next[i].sync.digest = syncDigest(syncCard(next.at(i), photo));
    }
    // 4. Gone from the server.
    for (const QString &href : a.removed) {
        const qsizetype i = byHref(href);
        if (i < 0) continue;
        next.removeAt(i);
        ++deleted;
    }
    // 5. Tombstones done; the book's position.
    QJsonObject state = m_rootExtra.value("carddav"_L1).toObject();
    QJsonArray tombstones;
    for (const QJsonValue &value : state.value("deleted"_L1).toArray()) {
        const QJsonObject o = value.toObject();
        const bool done = o.value("account"_L1).toString() == account && o.value("book"_L1).toString() == book
            && a.deletedHrefs.contains(o.value("href"_L1).toString());
        if (!done) tombstones.append(o);
    }
    state.insert("deleted"_L1, tombstones);
    QJsonObject books = state.value("books"_L1).toObject();
    books.insert(bookKey(account, book), QJsonObject{{u"syncToken"_s, a.syncToken}, {u"ctag"_s, a.ctag}});
    state.insert("books"_L1, books);
    if (next.size() > ContactLimits::kMaxContacts) {
        discardWritten(written);
        return {{u"ok"_s, false}, {u"error"_s, u"The address book holds at most %1 contacts."_s.arg(ContactLimits::kMaxContacts)}};
    }
    const QJsonObject previous = m_rootExtra;
    m_rootExtra.insert("carddav"_L1, state);
    QString error;
    if (!save(next, &error)) {
        m_rootExtra = previous;
        discardWritten(written);
        setError(error);
        return {{u"ok"_s, false}, {u"error"_s, error}};
    }
    commit(std::move(next));
    setError({});
    emit contactsChanged();
    return {{u"ok"_s, true}, {u"created"_s, created}, {u"updated"_s, updated}, {u"deleted"_s, deleted},
            {u"conflicts"_s, conflictNames}, {u"warnings"_s, warnings}, {u"error"_s, QString()}};
}

QVariantMap ContactStore::forgetSync(const QString &account)
{
    ensureLoaded();
    if (!m_readOnlyReason.isEmpty()) return {{u"ok"_s, false}, {u"error"_s, readOnlyError()}};
    QList<Contact> next = m_contacts;
    for (Contact &c : next) {
        if (c.sync.account == account) c.sync = ContactSync();
    }
    QJsonObject state = m_rootExtra.value("carddav"_L1).toObject();
    QJsonArray tombstones;
    for (const QJsonValue &value : state.value("deleted"_L1).toArray()) {
        if (value.toObject().value("account"_L1).toString() != account) tombstones.append(value);
    }
    state.insert("deleted"_L1, tombstones);
    QJsonObject books = state.value("books"_L1).toObject();
    for (const QString &key : books.keys()) {
        if (key.startsWith(account + u'\n')) books.remove(key);
    }
    state.insert("books"_L1, books);
    const QJsonObject previous = m_rootExtra;
    m_rootExtra.insert("carddav"_L1, state);
    QString error;
    if (!save(next, &error)) {
        m_rootExtra = previous;
        return {{u"ok"_s, false}, {u"error"_s, error}};
    }
    commit(std::move(next));
    emit contactsChanged();
    return {{u"ok"_s, true}, {u"error"_s, QString()}};
}

} // namespace QindaOffice::Mail
