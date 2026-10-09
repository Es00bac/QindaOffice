// SPDX-License-Identifier: GPL-3.0-or-later
// ContactStore import (vCard, CSV) and vCard export (M64). See contact_store.h.

#include "contact_store.h"

#include "contact_csv.h"
#include "vcard.h"

#include <atomic_file_store.h>

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <utility>

namespace QindaOffice::Mail {

using namespace Qt::StringLiterals;

namespace {

QString digitsOf(const QString &text)
{
    QString out;
    for (QChar ch : text) {
        if (ch.isDigit()) out.append(ch);
    }
    return out;
}

void unionValues(QList<ContactValue> &into, const QList<ContactValue> &from, bool phone)
{
    auto same = [phone](const ContactValue &a, const ContactValue &b) {
        if (phone && !digitsOf(a.value).isEmpty()) return digitsOf(a.value) == digitsOf(b.value);
        return a.value.compare(b.value, Qt::CaseInsensitive) == 0;
    };
    for (const ContactValue &value : from) {
        if (into.size() >= ContactLimits::kMaxMultiValues) return;
        const bool present = std::any_of(into.cbegin(), into.cend(),
                                         [&](const ContactValue &existing) { return same(existing, value); });
        if (!present) into.append(value);
    }
}

// An import matched by e-mail: fill what is empty and union the lists;
// nothing the user already has is overwritten.
void mergeInto(Contact &into, const Contact &from)
{
    const QList<QString Contact::*> scalars = {
        &Contact::uid, &Contact::displayName, &Contact::prefix, &Contact::givenName, &Contact::middleName,
        &Contact::familyName, &Contact::suffix, &Contact::nickname, &Contact::organization,
        &Contact::department, &Contact::title, &Contact::birthday, &Contact::anniversary, &Contact::notes,
        &Contact::photoUrl, &Contact::revision,
    };
    for (QString Contact::*member : scalars) {
        if ((into.*member).isEmpty()) into.*member = from.*member;
    }
    if (into.photoFile.isEmpty() && !from.photoFile.isEmpty()) {
        into.photoFile = from.photoFile;
        into.photoMimeType = from.photoMimeType;
    }
    unionValues(into.emails, from.emails, false);
    unionValues(into.phones, from.phones, true);
    unionValues(into.urls, from.urls, false);
    for (const ContactAddress &address : from.addresses) {
        if (into.addresses.size() < ContactLimits::kMaxMultiValues && !into.addresses.contains(address)) {
            into.addresses.append(address);
        }
    }
    for (const QString &category : from.categories) {
        if (into.categories.size() < ContactLimits::kMaxMultiValues && !into.categories.contains(category, Qt::CaseInsensitive)) {
            into.categories.append(category);
        }
    }
    for (const VCardProperty &property : from.extra) {
        if (into.extra.size() < ContactLimits::kMaxExtraProperties && !into.extra.contains(property)) {
            into.extra.append(property);
        }
    }
}

} // namespace

QVariantMap ContactStore::importFailure(const QString &error)
{
    setError(error);
    return {{u"ok"_s, false}, {u"added"_s, 0}, {u"updated"_s, 0}, {u"skipped"_s, 0},
            {u"warnings"_s, QStringList()}, {u"error"_s, error}};
}

QVariantMap ContactStore::importFile(const QUrl &fileOrPath)
{
    const QString file = localPathFor(fileOrPath);
    if (file.isEmpty()) return importFailure(u"Only local files can be imported."_s);
    const QFileInfo info(file);
    if (!info.isFile()) return importFailure(u"%1 is not a file."_s.arg(file));
    if (info.size() > ContactLimits::kMaxImportFileBytes) {
        return importFailure(u"%1 is larger than 64 MiB; it was not imported."_s.arg(file));
    }
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) return importFailure(u"Cannot read %1: %2"_s.arg(file, in.errorString()));
    const QByteArray data = in.read(ContactLimits::kMaxImportFileBytes + 1);
    return importData(data, info.fileName());
}

QVariantMap ContactStore::importData(const QByteArray &data, const QString &fileName)
{
    ensureLoaded();
    if (!m_readOnlyReason.isEmpty()) return importFailure(readOnlyError());
    if (data.size() > ContactLimits::kMaxImportFileBytes) {
        return importFailure(u"“%1” is larger than 64 MiB; it was not imported."_s.arg(fileName));
    }
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    bool vcard = suffix == "vcf"_L1 || suffix == "vcard"_L1 || suffix == "vct"_L1;
    bool csv = suffix == "csv"_L1;
    if (!vcard && !csv) {
        QByteArray probe = data.left(4096);
        probe.removeIf([](char ch) { return ch == '\0'; }); // UTF-16 text
        vcard = probe.toLower().contains("begin:vcard");
        csv = !vcard && (probe.contains(',') || probe.contains(';') || probe.contains('\t'));
    }
    if (!vcard && !csv) return importFailure(u"“%1” is neither a vCard (.vcf) nor a CSV file."_s.arg(fileName));
    QList<Contact> contacts;
    QStringList warnings;
    int skipped = 0;
    if (vcard) {
        VCard::ReadResult read = VCard::read(data);
        contacts = std::move(read.contacts);
        warnings = std::move(read.warnings);
        skipped = read.skipped;
    } else {
        ContactCsv::ReadResult read = ContactCsv::read(data);
        contacts = std::move(read.contacts);
        warnings = std::move(read.warnings);
        skipped = read.skipped;
    }
    if (contacts.isEmpty() && skipped == 0) {
        // Nothing usable at all (no card, an unknown CSV layout, too large):
        // a failure the app can show, with the reader's explanation.
        QVariantMap failure = importFailure(warnings.isEmpty()
                                                ? u"No contacts were found in “%1”."_s.arg(fileName)
                                                : warnings.constFirst());
        failure.insert(u"warnings"_s, warnings);
        return failure;
    }
    return mergeImported(contacts, warnings, skipped);
}

QVariantMap ContactStore::mergeImported(QList<Contact> incoming, QStringList warnings, int skipped)
{
    QList<Contact> next = m_contacts;
    const qsizetype originalCount = next.size();
    QHash<QString, qsizetype> byUid;      // uid, or the store id of a contact without one
    QHash<QString, qsizetype> byAddress;  // lower-case address
    auto remember = [&](qsizetype index) {
        const Contact &c = next.at(index);
        byUid.insert(c.uid.isEmpty() ? c.id : c.uid, index);
        for (const ContactValue &email : c.emails) {
            if (!byAddress.contains(email.value.toLower())) byAddress.insert(email.value.toLower(), index);
        }
    };
    for (qsizetype i = 0; i < next.size(); ++i) remember(i);
    QStringList written;
    QSet<QString> updatedIds;
    int added = 0;
    for (Contact &c : incoming) {
        normalizeContact(c);
        const QString who = c.effectiveDisplayName();
        // Imported data is repaired where it can be, not refused: bad
        // addresses and dates are left out, an oversized photo dropped.
        const qsizetype emailCount = c.emails.size();
        c.emails.removeIf([](const ContactValue &e) { return !isPlausibleEmail(e.value); });
        if (c.emails.size() != emailCount) warnings.append(u"“%1”: a value that is not an e-mail address was left out."_s.arg(who));
        for (QString *date : {&c.birthday, &c.anniversary}) {
            if (normalizedContactDate(*date) != *date) date->clear();
        }
        if (c.photo.size() > ContactLimits::kMaxPhotoBytes) {
            warnings.append(u"The photo for “%1” is larger than 1 MiB and was not stored."_s.arg(who));
            c.photo.clear();
        }
        c.id.clear();
        c.photoFile.clear();
        c.jsonExtra = QJsonObject();
        if (const QStringList errors = validateContact(c); !errors.isEmpty()) {
            ++skipped;
            warnings.append(u"“%1” was skipped: %2"_s.arg(who, errors.constFirst()));
            continue;
        }
        const QByteArray photo = std::exchange(c.photo, QByteArray());
        const auto stored = photo.isEmpty() ? std::nullopt : storePhoto(photo, c.photoMimeType, who, &warnings, &written);
        c.photoFile = stored ? stored->first : QString();
        c.photoMimeType = stored ? stored->second : QString();

        qsizetype target = c.uid.isEmpty() ? -1 : byUid.value(c.uid, -1);
        const QString primary = c.primaryEmail().toLower();
        if (target >= 0) {
            Contact &existing = next[target];
            c.id = existing.id;
            c.jsonExtra = existing.jsonExtra;
            if (c.photoFile.isEmpty()) {
                c.photoFile = existing.photoFile;
                c.photoMimeType = existing.photoMimeType;
            }
            existing = c;
        } else if (!primary.isEmpty() && (target = byAddress.value(primary, -1)) >= 0) {
            mergeInto(next[target], c);
        } else {
            c.id = newId();
            next.append(c);
            target = next.size() - 1;
            ++added;
        }
        if (target < originalCount) updatedIds.insert(next.at(target).id);
        remember(target);
    }
    QVariantMap result{{u"ok"_s, true}, {u"added"_s, added}, {u"updated"_s, int(updatedIds.size())},
                       {u"skipped"_s, skipped}, {u"warnings"_s, warnings}, {u"error"_s, QString()}};
    if (next.size() > ContactLimits::kMaxContacts) {
        // AGENT-GUARD: all or nothing — a partial import is hard to undo.
        discardWritten(written);
        QVariantMap failure = importFailure(
            u"The address book holds at most %1 contacts; this import would add %2 to the %3 already there. "
            "Nothing was imported."_s.arg(ContactLimits::kMaxContacts).arg(added).arg(originalCount));
        failure.insert(u"skipped"_s, skipped);
        failure.insert(u"warnings"_s, warnings);
        return failure;
    }
    if (added == 0 && updatedIds.isEmpty()) {
        setError({});
        return result;
    }
    QString error;
    if (!save(next, &error)) {
        discardWritten(written);
        QVariantMap failure = importFailure(error);
        failure.insert(u"skipped"_s, skipped);
        failure.insert(u"warnings"_s, warnings);
        return failure;
    }
    commit(std::move(next));
    setError({});
    emit contactsChanged();
    return result;
}

QVariantMap ContactStore::exportVcf(const QUrl &fileOrPath, const QStringList &ids, const QString &version)
{
    ensureLoaded();
    auto fail = [this](const QString &error) {
        setError(error);
        return QVariantMap{{u"ok"_s, false}, {u"count"_s, 0}, {u"error"_s, error}};
    };
    const QString v = version.trimmed();
    VCard::Version format = VCard::Version::V30;
    if (v == "4.0"_L1 || v == "4"_L1) format = VCard::Version::V40;
    else if (!v.isEmpty() && v != "3.0"_L1 && v != "3"_L1) return fail(u"vCard version “%1” is not supported; use 3.0 or 4.0."_s.arg(v));
    const QString file = localPathFor(fileOrPath);
    if (file.isEmpty()) return fail(u"Contacts can only be exported to a local file."_s);
    // AGENT-GUARD: an export must never replace the address book itself.
    if (QFileInfo(file).absoluteFilePath() == QFileInfo(path()).absoluteFilePath()) {
        return fail(u"Choose another file: %1 is the address book itself."_s.arg(file));
    }
    ensureIndex();
    QList<qsizetype> selection;
    if (ids.isEmpty()) {
        selection = m_order;
    } else {
        const QSet<QString> wanted(ids.cbegin(), ids.cend());
        for (qsizetype i : std::as_const(m_order)) {
            if (wanted.contains(m_contacts.at(i).id)) selection.append(i);
        }
    }
    if (selection.isEmpty()) return fail(u"There are no contacts to export."_s);
    QByteArray out;
    for (qsizetype i : std::as_const(selection)) {
        Contact contact = m_contacts.at(i);
        contact.photo = readPhotoFile(contact.photoFile);
        out += VCard::write({contact}, format);
        if (out.size() > ContactLimits::kMaxImportFileBytes) {
            return fail(u"The export would be larger than 64 MiB (the most an import accepts); export fewer contacts."_s);
        }
    }
    const Core::AtomicFileStore::Result written = Core::AtomicFileStore::write(file, out);
    if (!written.ok) return fail(u"Cannot write %1: %2"_s.arg(file, written.error));
    setError({});
    return {{u"ok"_s, true}, {u"count"_s, int(selection.size())}, {u"error"_s, QString()}};
}

} // namespace QindaOffice::Mail
