// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// One address-book contact (M64): the value type QindaMail's People
// workspace, recipient autocomplete, the vCard reader/writer (vcard.h), the
// Outlook/Google CSV reader (contact_csv.h) and the persistent store
// (contact_store.h) all speak.
//
// Types. ContactValue/ContactAddress::type is either a lower-case token
// list ("work", "cell", "work,fax" — vCard TYPE values without the noise
// tokens pref/voice/internet, which become `preferred` or are dropped), or
// a user's custom label kept verbatim ("School", from Apple's
// itemN.X-ABLabel or a Google CSV label). isTypeTokenList() tells them
// apart; the vCard writer writes tokens as TYPE= and custom labels through
// Apple's item grouping.
//
// AGENT-CONTRACT (contact.cpp ⇄ ContactStore ⇄ the People/autocomplete
// QML): toVariantMap() is the QML shape. Keys: id, uid, displayName (the
// EFFECTIVE name, never empty for a valid contact), formattedName (the raw
// FN, informational, ignored on input), prefix, givenName, middleName,
// familyName, suffix, nickname, emails/phones/urls [{type, value,
// preferred}], addresses [{type, poBox, extended, street, locality, region,
// postalCode, country, label, preferred}], organization, department, title,
// birthday, anniversary, notes, categories (string list), hasPhoto,
// photoMimeType, photoRemoteUri, revision, primaryEmail, extra [{group,
// name, params [{name, value}], value}]. ContactStore adds photoUrl (the
// LOCAL file: URL of the stored picture, or ""). The photo bytes are never
// in the map.
// AGENT-GUARD: a remote PHOTO URI travels as `photoRemoteUri`, never as
// `photoUrl`: binding it to an Image would fetch it (a tracking pixel).
//
// AGENT-CONTRACT (contact.cpp ⇄ contact_store.cpp): toJson() is the
// storage shape inside contacts.json (schema qindaoffice.mail.contacts/1).
// Empty fields are omitted; `displayName` is the raw FN there; the photo is
// a file reference (photoFile + photoMimeType), written by ContactStore;
// unknown keys survive in `jsonExtra`.

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace QindaOffice::Mail {

// Bounds for untrusted address-book input and for the store.
// AGENT-GUARD: every reader checks these before it allocates; raising one
// raises what a hostile .vcf/.csv file can make the app allocate.
namespace ContactLimits {
inline constexpr qint64 kMaxImportFileBytes = 64LL * 1024 * 1024;  // one .vcf/.csv file
inline constexpr qint64 kMaxStoreFileBytes = 256LL * 1024 * 1024;  // contacts.json
inline constexpr int kMaxContacts = 20000;            // per file and per store
inline constexpr int kMaxValueBytes = 1024 * 1024;    // one property value / text field
inline constexpr int kMaxPhotoBytes = 1024 * 1024;    // a decoded photo
inline constexpr int kMaxMultiValues = 64;            // emails, phones, urls, addresses, categories
inline constexpr int kMaxExtraProperties = 256;       // unmapped vCard properties per contact
inline constexpr int kMaxLineBytes = 2 * 1024 * 1024; // one unfolded vCard content line
inline constexpr int kMaxWarnings = 100;              // per read; then one summary line
} // namespace ContactLimits

struct ContactValue {
    QString type;
    QString value;
    bool preferred = false;
    friend bool operator==(const ContactValue &, const ContactValue &) = default;
};

struct ContactAddress {
    QString type;
    QString poBox, extended, street, locality, region, postalCode, country;
    QString label;          // the formatted label (vCard LABEL / ADR;LABEL=)
    bool preferred = false;
    [[nodiscard]] bool isEmpty() const;
    friend bool operator==(const ContactAddress &, const ContactAddress &) = default;
};

// A vCard property the Contact fields do not map, kept for round trip.
// `value` is the text after CHARSET/QUOTED-PRINTABLE decoding with the vCard
// escapes (\n \, \; \\) LEFT IN PLACE, so structured and list values (GEO,
// CLIENTPIDMAP, X-…) are written back unchanged; binary values stay base64
// text with their ("ENCODING", "b") param. Names keep their case.
struct VCardProperty {
    QString group;
    QString name;
    QList<QPair<QString, QString>> params;
    QString value;
    friend bool operator==(const VCardProperty &, const VCardProperty &) = default;
};

struct Contact {
    QString id;             // store id (UUID); written to vCard only as UID when uid is empty
    QString uid;            // vCard UID
    QString displayName;    // FN
    QString prefix, givenName, middleName, familyName, suffix;  // N
    QString nickname;
    QList<ContactValue> emails, phones, urls;
    QList<ContactAddress> addresses;
    QString organization, department, title;
    QString birthday;       // "YYYY-MM-DD" or "--MM-DD" (no year)
    QString anniversary;    // same forms
    QString notes;
    QStringList categories;
    QByteArray photo;       // embedded photo bytes (readers/writer); empty inside the store
    QString photoMimeType;
    QString photoUrl;       // a remote PHOTO URI kept as text, NEVER fetched
    QString revision;       // REV, verbatim
    QList<VCardProperty> extra;
    // Store-only (never read or written by vCard/CSV):
    QString photoFile;      // file name under contact-photos/ (ContactStore)
    QJsonObject jsonExtra;  // unknown per-contact JSON keys, preserved on save

    // FN, else "Given Family", else nickname, else organization, else the
    // primary e-mail.
    [[nodiscard]] QString effectiveDisplayName() const;
    // The preferred e-mail, else the first; "" when there is none.
    [[nodiscard]] QString primaryEmail() const;
    // Every query word must be a prefix of a word of the name, nickname,
    // e-mail, organization or department (case- and accent-insensitive);
    // a word of 3+ digits also matches inside a phone number.
    [[nodiscard]] bool matches(const QString &query) const;
    [[nodiscard]] bool hasPhoto() const { return !photo.isEmpty() || !photoFile.isEmpty(); }

    friend bool operator==(const Contact &, const Contact &) = default;
};

// QML shape (see the contract above). contactFromVariantMap() builds a new
// contact; applyVariantMap() overwrites only the keys present in `map` (an
// update from QML that omits a key keeps that field). Neither reads files:
// `photoFile`/`photoRemove` are ContactStore's business.
[[nodiscard]] QVariantMap toVariantMap(const Contact &contact);
[[nodiscard]] Contact contactFromVariantMap(const QVariantMap &map);
void applyVariantMap(Contact &contact, const QVariantMap &map);

// Storage shape (see the contract above). contactFromJson() tolerates wrong
// value types (they read as empty) and bounds every list.
[[nodiscard]] QJsonObject toJson(const Contact &contact);
[[nodiscard]] Contact contactFromJson(const QJsonObject &object);

// "YYYY-MM-DD" or "--MM-DD" from ISO 8601 basic/extended dates with or
// without a year or a time ("19750415", "1975-04-15T00:00:00Z", "--0415");
// "" when the text is not a valid calendar date.
[[nodiscard]] QString normalizedContactDate(const QString &text);
// Loose syntax check: one '@', non-empty local part and domain, no
// whitespace, at most 254 characters.
[[nodiscard]] bool isPlausibleEmail(const QString &address);
// True for a lower-case vCard TYPE token list ("work", "home,fax").
[[nodiscard]] bool isTypeTokenList(const QString &type);
// Trims every field, drops empty values, canonical dates, CRLF → LF.
void normalizeContact(Contact &contact);
// One human-readable line per problem ("emails[2]: …"); empty = valid.
[[nodiscard]] QStringList validateContact(const Contact &contact);

// Magic-byte sniffing: image/jpeg|png|gif|bmp|webp, or "".
[[nodiscard]] QString sniffImageMimeType(const QByteArray &bytes);
// jpg|png|gif|bmp|webp for those MIME types, else "".
[[nodiscard]] QString imageExtensionForMimeType(const QString &mimeType);

// Search helpers (ContactStore caches the key per contact).
struct ContactSearchKey {
    QStringList tokens;       // folded words and whole fields
    QStringList phoneDigits;  // each phone number's digits
};
[[nodiscard]] QString foldForSearch(const QString &text);
[[nodiscard]] ContactSearchKey contactSearchKey(const Contact &contact);
[[nodiscard]] QStringList searchWords(const QString &query);
[[nodiscard]] bool searchKeyMatches(const ContactSearchKey &key, const QStringList &words);

// "Name <addr>" as an RFC 5322 mailbox (the name quoted when it contains
// specials), or just the address when the name is empty or equal to it.
[[nodiscard]] QString formatMailbox(const QString &name, const QString &address);

} // namespace QindaOffice::Mail
