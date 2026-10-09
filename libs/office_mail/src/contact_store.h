// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// QindaMail's local address book (M64): the contacts list persisted
// atomically as JSON (schema `qindaoffice.mail.contacts/1`) at
// $XDG_DATA_HOME/qindaoffice/mail/contacts.json, with each photo a file in
// contact-photos/<sha256 of the bytes>.<jpg|png|gif|bmp|webp> next to it.
//
// Conventions mirror AccountStore (docs/mail.md#accounts-json): every
// mutation validates, writes the file (Core::AtomicFileStore), and only
// then changes the in-memory list; unknown top-level and per-contact keys
// are preserved; a file with another schema is loaded read-only (never
// downgraded); an unparseable file is moved to contacts.json.corrupt and
// the store starts empty. A file holding more than 20 000 contacts is also
// read-only (writing back the first 20 000 would lose the rest).
//
// Photos: a photo file is written before the JSON that references it and
// removed once no contact references it any more (only files this store
// knew about are ever deleted). A photo over 1 MiB, or one that is not a
// JPEG/PNG/GIF/BMP/WebP picture, is refused with a warning and the contact
// is kept without it. Remote photo URIs are kept as text, never fetched.
//
// AGENT-CONTRACT (ContactStore ⇄ the People workspace and recipient
// autocomplete QML): the property and invokable names below, the contact
// map shape (contact.h, plus `photoUrl` = local file: URL or ""), the
// result maps ({ok, id, error, errors, warnings}; import {ok, added,
// updated, skipped, warnings, error}; export {ok, count, error}) and the
// autocomplete entries ({label, value, name, contactId, type}).
// importFile/importData emit contactsChanged once, not one signal per
// imported contact.
//
// Threading: GUI thread only. One store per process is the app's concern.

#include "contact.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <optional>
#include <tuple>

namespace QindaOffice::Mail {

class ContactStore : public QObject {
    Q_OBJECT
    QML_ELEMENT
    // Empty means defaultPath(). Setting it reloads.
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    // Every contact as a map, sorted by effective display name (QCollator).
    Q_PROPERTY(QVariantList contacts READ contacts NOTIFY contactsChanged)
    Q_PROPERTY(int count READ count NOTIFY contactsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY contactsChanged)

public:
    static constexpr const char *kSchema = "qindaoffice.mail.contacts/1";

    explicit ContactStore(QObject *parent = nullptr);
    ContactStore(const QString &path, QObject *parent = nullptr);

    // $XDG_DATA_HOME/qindaoffice/mail/contacts.json (not created here).
    [[nodiscard]] static QString defaultPath();

    [[nodiscard]] QString path() const;
    void setPath(const QString &path);
    [[nodiscard]] QVariantList contacts() const;
    [[nodiscard]] int count() const;
    [[nodiscard]] QString lastError() const { return m_lastError; }
    [[nodiscard]] bool readOnly() const;
    // contact-photos/ next to the JSON file.
    [[nodiscard]] QString photoDirectory() const;

    // C++ view of the same list (photo bytes stay on disk; see photoBytes()).
    [[nodiscard]] QList<Contact> list() const;
    [[nodiscard]] std::optional<Contact> find(const QString &id) const;
    [[nodiscard]] QByteArray photoBytes(const QString &id) const;

    // add/update take the QML map (contact.h). update() changes only the
    // keys present. Both also take "photoFile" (a file: URL or local path of
    // a picture to store) and update() "photoRemove": true. Returns {ok, id,
    // error, errors (one line per problem), warnings}.
    Q_INVOKABLE QVariantMap add(const QVariantMap &contact);
    Q_INVOKABLE QVariantMap update(const QVariantMap &contact);
    Q_INVOKABLE QVariantMap remove(const QString &id);
    // {} when unknown.
    Q_INVOKABLE QVariantMap contactById(const QString &id) const;
    // The contact with this e-mail address (case-insensitive; "Name <addr>"
    // is accepted); {} when none.
    Q_INVOKABLE QVariantMap contactForAddress(const QString &email) const;
    // Every query word must prefix a word of the name, nickname, e-mail,
    // organization or department (or appear in a phone number when it has
    // 3+ digits); display order; empty query = everything (up to limit).
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 200) const;
    // Recipient autocomplete: [{label (RFC 5322 "Name <addr>" or addr),
    // value (addr), name, contactId, type}], best first: exact, then prefix
    // matches on display/given/family/nickname and the address's local
    // part, then any word match; the preferred address of a contact first;
    // each address once.
    Q_INVOKABLE QVariantList complete(const QString &query, int limit = 8) const;
    // "Add sender to contacts": {ok, id, error, existed}; an address already
    // in the book returns that contact with existed: true.
    Q_INVOKABLE QVariantMap addSender(const QString &name, const QString &address);
    // .vcf/.vcard/.vct or .csv by extension, else sniffed. Merge: same UID
    // (or a UID equal to a contact's store id, which is what an export
    // writes for a contact without one) → that contact is replaced (its id,
    // unknown keys and — when the card has none — photo kept); else the
    // primary e-mail matches any address of a contact → merged (empty
    // fields filled; e-mails, phones, URLs, addresses, categories and
    // extras unioned); else added. All or nothing: an import that would
    // pass 20 000 contacts is refused. {ok, added, updated, skipped,
    // warnings, error}.
    Q_INVOKABLE QVariantMap importFile(const QUrl &fileOrPath);
    Q_INVOKABLE QVariantMap importData(const QByteArray &data, const QString &fileName);
    // M76 (D-085): contacts read from elsewhere (an Outlook .pst or .msg),
    // merged exactly as an import is; one write, one contactsChanged.
    QVariantMap importContacts(const QList<Contact> &contacts, int skipped = 0);
    // M76: a group's members as recipients: {ok, name, recipients
    // ["Name <addr>"], count, missing (members without an address), error};
    // groups inside groups expand too (each once, ≤ 8 deep), every address once.
    Q_INVOKABLE QVariantMap expandGroup(const QString &id) const;
    // M76: the contact a group member's uri names ("urn:uuid:<uid>", a bare
    // uid or a store id); {} when the address book has none.
    Q_INVOKABLE QVariantMap contactForMember(const QString &uri) const;
    // ids empty = every contact (display order); version "3.0" (default) or
    // "4.0". Written atomically; refuses to overwrite the address book and
    // files over 64 MiB (what import accepts). {ok, count, error}.
    Q_INVOKABLE QVariantMap exportVcf(const QUrl &fileOrPath, const QStringList &ids = {},
                                      const QString &version = QStringLiteral("3.0"));
    Q_INVOKABLE void reload();

    // ---- CardDAV (M76, D-085; contact_store_sync.cpp; C++ only) ----
    // AGENT-CONTRACT (carddav_provider.cpp): a contact syncs with the book
    // its `sync` names; its current vCard 3.0 differing from `sync.digest`
    // means "changed here". Deleting a synced contact leaves a tombstone,
    // and books' tokens live in the file's "carddav" object.
    static constexpr const char *kConflictCategory = "Sync conflict";
    struct SyncCard {
        QString contactId, href, etag, uid;
        QByteArray vcard;
        bool changed = false;
    };
    struct SyncSnapshot {
        QString syncToken, ctag;
        QList<SyncCard> cards;
        QList<QPair<QString, QString>> deleted;   // (href, etag)
    };
    struct RemoteVCard {
        QString href, etag;
        QByteArray vcard;
    };
    struct SyncApply {
        QString syncToken, ctag;
        QList<RemoteVCard> changed, restored;
        QStringList removed, deletedHrefs, conflicts;
        QList<std::tuple<QString, QString, QString>> pushed;   // contact id, href, etag
        QHash<QString, QString> sentDigests;                   // contact id → digest of what was sent
    };
    [[nodiscard]] SyncSnapshot syncSnapshot(const QString &account, const QString &book) const;
    // One book's sync in ONE write: {ok, created, updated, deleted,
    // conflicts (names of the local copies kept), warnings, error}.
    QVariantMap applySync(const QString &account, const QString &book, const SyncApply &apply);
    // An account that no longer syncs: its contacts stay, unbound.
    QVariantMap forgetSync(const QString &account);
    [[nodiscard]] static QString syncDigest(const QByteArray &vcard);

signals:
    void pathChanged();
    void contactsChanged();
    void lastErrorChanged();
    void contactAdded(const QString &id);
    void contactUpdated(const QString &id);
    void contactRemoved(const QString &id);

private:
    struct IndexEntry {
        ContactSearchKey key;
        QString display;     // folded effective display name
        QStringList names;   // folded given name, family name, nickname
        QStringList emails;  // folded addresses, parallel to Contact::emails
    };

    // contact_store.cpp — persistence and edits
    void ensureLoaded() const;
    bool loadFile();
    [[nodiscard]] bool save(const QList<Contact> &contacts, QString *error) const;
    void commit(QList<Contact> next);
    void setError(const QString &error);
    [[nodiscard]] QString readOnlyError() const;
    QVariantMap failure(const QString &id, const QString &error, const QStringList &errors = {},
                        const QStringList &warnings = {});
    [[nodiscard]] static QVariantMap success(const QString &id, const QStringList &warnings);
    [[nodiscard]] qsizetype indexOf(const QString &id) const;
    [[nodiscard]] static QString newId();
    [[nodiscard]] static QString localPathFor(const QUrl &url);
    // contact_store_photos.cpp
    std::optional<QPair<QString, QString>> storePhoto(const QByteArray &bytes, const QString &mimeHint,
                                                      const QString &who, QStringList *warnings,
                                                      QStringList *written) const;
    void applyPhotoKeys(Contact &contact, const QVariantMap &map, QStringList *warnings, QStringList *written) const;
    [[nodiscard]] QByteArray readPhotoFile(const QString &fileName) const;
    void removePhotoFiles(const QSet<QString> &fileNames) const;
    void discardWritten(const QStringList &written) const;
    [[nodiscard]] static QSet<QString> photoReferences(const QList<Contact> &contacts);
    [[nodiscard]] static bool validPhotoFileName(const QString &name);
    // contact_store_query.cpp
    void invalidate();
    void ensureIndex() const;
    [[nodiscard]] QVariantMap mapFor(const Contact &contact) const;
    // contact_store_groups.cpp (M76)
    [[nodiscard]] qsizetype indexForMember(const QString &uri) const;
    // A group's members with what each resolves to (QML: People's group card).
    [[nodiscard]] QVariantList resolvedMembers(const Contact &group) const;
    // QML may name a member by its contactId: that becomes its card's uri.
    [[nodiscard]] QVariantMap withMemberUris(const QVariantMap &map) const;
    [[nodiscard]] qsizetype indexForAddress(const QString &address) const;
    // contact_store_sync.cpp
    [[nodiscard]] QByteArray syncCard(const Contact &contact, const QByteArray &photo) const;
    // contact_store_io.cpp
    QVariantMap importFailure(const QString &error);
    QVariantMap mergeImported(QList<Contact> incoming, QStringList warnings, int skipped);

    QString m_path;
    mutable bool m_loaded = false;
    QList<Contact> m_contacts;      // file order
    QJsonObject m_rootExtra;        // unknown top-level keys, preserved on save
    QString m_lastError;
    QString m_readOnlyReason;       // non-empty = read-only
    // Caches over m_contacts, rebuilt lazily after every change.
    mutable bool m_indexValid = false;
    mutable QList<IndexEntry> m_index;
    mutable QList<qsizetype> m_order;            // display order
    mutable QHash<QString, qsizetype> m_byAddress; // folded address → contact
    mutable QHash<QString, qsizetype> m_byUid;     // lower-case uid and id → contact (M76)
    mutable bool m_cacheValid = false;
    mutable QVariantList m_cache;                // contacts() in display order
};

} // namespace QindaOffice::Mail
