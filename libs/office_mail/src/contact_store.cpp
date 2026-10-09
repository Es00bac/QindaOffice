// SPDX-License-Identifier: GPL-3.0-or-later
// ContactStore persistence and edits (M64). See contact_store.h.

#include "contact_store.h"

#include <atomic_file_store.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>

namespace QindaOffice::Mail {

using namespace Qt::StringLiterals;

namespace {

bool validId(const QString &id)
{
    if (id.isEmpty() || id.size() > 128) return false;
    for (QChar ch : id) {
        const bool ok = (ch >= u'A' && ch <= u'Z') || (ch >= u'a' && ch <= u'z') || (ch >= u'0' && ch <= u'9')
            || ch == u'.' || ch == u'_' || ch == u'-';
        if (!ok) return false;
    }
    return true;
}

} // namespace

ContactStore::ContactStore(QObject *parent)
    : QObject(parent)
{
}

ContactStore::ContactStore(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
{
}

QString ContactStore::defaultPath()
{
    // AGENT-NOTE: data, not configuration: next to QindaMail's store.json
    // (docs/mail.md, "QindaMail (app)"), under GenericDataLocation like it.
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + u"/qindaoffice/mail/contacts.json"_s;
}

QString ContactStore::path() const
{
    return m_path.isEmpty() ? defaultPath() : m_path;
}

void ContactStore::setPath(const QString &path)
{
    if (path == m_path) return;
    m_path = path;
    emit pathChanged();
    reload();
}

QString ContactStore::photoDirectory() const
{
    return QFileInfo(path()).absolutePath() + u"/contact-photos"_s;
}

bool ContactStore::readOnly() const
{
    ensureLoaded();
    return !m_readOnlyReason.isEmpty();
}

int ContactStore::count() const
{
    ensureLoaded();
    return static_cast<int>(m_contacts.size());
}

QList<Contact> ContactStore::list() const
{
    ensureLoaded();
    return m_contacts;
}

std::optional<Contact> ContactStore::find(const QString &id) const
{
    const qsizetype index = indexOf(id);
    if (index < 0) return std::nullopt;
    return m_contacts.at(index);
}

QByteArray ContactStore::photoBytes(const QString &id) const
{
    const std::optional<Contact> contact = find(id);
    return contact ? readPhotoFile(contact->photoFile) : QByteArray();
}

void ContactStore::ensureLoaded() const
{
    if (m_loaded) return;
    m_loaded = true;
    // Lazy, so that setting `path` from QML before first use never reads
    // (or quarantines) the default file.
    const_cast<ContactStore *>(this)->loadFile();
}

void ContactStore::reload()
{
    m_loaded = true;
    loadFile();
    emit contactsChanged();
}

bool ContactStore::loadFile()
{
    m_contacts.clear();
    m_rootExtra = QJsonObject();
    m_readOnlyReason.clear();
    invalidate();
    setError({});
    const QString file = path();
    QFile in(file);
    if (!in.exists()) return true;
    auto readOnlyBecause = [this](const QString &reason) {
        // AGENT-GUARD: never overwrite a file this build could not fully read.
        m_readOnlyReason = reason;
        setError(reason);
        qWarning().noquote() << "contacts:" << reason;
        return false;
    };
    if (in.size() > ContactLimits::kMaxStoreFileBytes) {
        return readOnlyBecause(u"%1 is larger than 256 MiB; the address book is read-only."_s.arg(file));
    }
    if (!in.open(QIODevice::ReadOnly)) {
        return readOnlyBecause(u"Cannot read %1: %2; the address book is read-only."_s.arg(file, in.errorString()));
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(in.readAll(), &parseError);
    in.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        // AGENT-GUARD: same rule as AccountStore and Core::JsonSettings — an
        // unparseable file is moved aside, never overwritten.
        const QString aside = file + u".corrupt"_s;
        QFile::remove(aside);
        if (!QFile::rename(file, aside)) {
            return readOnlyBecause(u"%1 could not be read (%2) and could not be moved aside."_s
                                       .arg(file, parseError.errorString()));
        }
        setError(u"%1 could not be read (%2); it was moved to %3."_s.arg(file, parseError.errorString(), aside));
        qWarning().noquote() << "contacts:" << m_lastError;
        return false;
    }
    QJsonObject root = doc.object();
    const QString schema = root.value("schema"_L1).toString();
    QString reason;
    if (!schema.isEmpty() && schema != QLatin1StringView(kSchema)) {
        reason = u"%1 was written by a newer QindaOffice (%2); the address book is read-only."_s.arg(file, schema);
    }
    QSet<QString> seen;
    for (const QJsonValue &value : root.value("contacts"_L1).toArray()) {
        if (!value.isObject()) continue;
        if (m_contacts.size() >= ContactLimits::kMaxContacts) {
            reason = u"%1 holds more than %2 contacts; the address book is read-only."_s
                         .arg(file).arg(ContactLimits::kMaxContacts);
            break;
        }
        Contact contact = contactFromJson(value.toObject());
        // A hand-added entry without a usable id keeps its data under a new one.
        if (!validId(contact.id) || seen.contains(contact.id)) contact.id = newId();
        // AGENT-GUARD: photoFile names a file this store reads, serves and
        // deletes; anything but <sha256>.<ext> (e.g. "../../x") is dropped.
        if (!contact.photoFile.isEmpty() && !validPhotoFileName(contact.photoFile)) {
            contact.photoFile.clear();
            contact.photoMimeType.clear();
        }
        seen.insert(contact.id);
        m_contacts.append(contact);
    }
    root.remove("schema"_L1);
    root.remove("contacts"_L1);
    m_rootExtra = root;
    if (!reason.isEmpty()) return readOnlyBecause(reason);
    return true;
}

bool ContactStore::save(const QList<Contact> &contacts, QString *error) const
{
    if (!m_readOnlyReason.isEmpty()) {
        *error = m_readOnlyReason;
        return false;
    }
    QJsonObject root = m_rootExtra;
    root.insert("schema"_L1, QString::fromLatin1(kSchema));
    QJsonArray list;
    for (const Contact &contact : contacts) list.append(toJson(contact));
    root.insert("contacts"_L1, list);
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (bytes.size() > ContactLimits::kMaxStoreFileBytes) {
        *error = u"The address book would grow beyond 256 MiB; the change was not saved."_s;
        return false;
    }
    const QString file = path();
    QDir().mkpath(QFileInfo(file).absolutePath());
    const Core::AtomicFileStore::Result written = Core::AtomicFileStore::write(file, bytes);
    if (!written.ok) {
        *error = u"Cannot write %1: %2"_s.arg(file, written.error);
        return false;
    }
    return true;
}

// Swaps in a list that is already on disk, then removes the photo files
// that only the old list referenced.
void ContactStore::commit(QList<Contact> next)
{
    const QSet<QString> before = photoReferences(m_contacts);
    m_contacts = std::move(next);
    invalidate();
    removePhotoFiles(before - photoReferences(m_contacts));
}

void ContactStore::setError(const QString &error)
{
    if (error == m_lastError) return;
    m_lastError = error;
    emit lastErrorChanged();
}

QString ContactStore::readOnlyError() const
{
    return m_readOnlyReason;
}

QVariantMap ContactStore::failure(const QString &id, const QString &error, const QStringList &errors,
                                  const QStringList &warnings)
{
    setError(error);
    return {{u"ok"_s, false}, {u"id"_s, id}, {u"error"_s, error},
            {u"errors"_s, errors.isEmpty() ? QStringList{error} : errors}, {u"warnings"_s, warnings}};
}

QVariantMap ContactStore::success(const QString &id, const QStringList &warnings)
{
    return {{u"ok"_s, true}, {u"id"_s, id}, {u"error"_s, QString()}, {u"errors"_s, QStringList()},
            {u"warnings"_s, warnings}};
}

qsizetype ContactStore::indexOf(const QString &id) const
{
    ensureLoaded();
    if (id.isEmpty()) return -1;
    for (qsizetype i = 0; i < m_contacts.size(); ++i) {
        if (m_contacts.at(i).id == id) return i;
    }
    return -1;
}

QString ContactStore::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString ContactStore::localPathFor(const QUrl &url)
{
    if (url.isLocalFile()) return url.toLocalFile();
    if (url.scheme().isEmpty()) return url.path(QUrl::FullyDecoded);
    return {};
}

QVariantMap ContactStore::add(const QVariantMap &map)
{
    ensureLoaded();
    if (!m_readOnlyReason.isEmpty()) return failure({}, readOnlyError());
    if (m_contacts.size() >= ContactLimits::kMaxContacts) {
        return failure({}, u"The address book is full (%1 contacts)."_s.arg(ContactLimits::kMaxContacts));
    }
    Contact contact = contactFromVariantMap(map);
    normalizeContact(contact);
    if (contact.id.isEmpty()) contact.id = newId();
    else if (!validId(contact.id)) return failure(contact.id, u"id: use 1–128 letters, digits, '.', '_' or '-'."_s);
    else if (indexOf(contact.id) >= 0) return failure(contact.id, u"A contact with this id already exists."_s);
    const QStringList errors = validateContact(contact);
    if (!errors.isEmpty()) return failure(contact.id, errors.constFirst(), errors);
    QStringList warnings;
    QStringList written;
    applyPhotoKeys(contact, map, &warnings, &written);
    QList<Contact> next = m_contacts;
    next.append(contact);
    QString error;
    if (!save(next, &error)) {
        discardWritten(written);
        return failure(contact.id, error, {}, warnings);
    }
    commit(std::move(next));
    setError({});
    emit contactsChanged();
    emit contactAdded(contact.id);
    return success(contact.id, warnings);
}

QVariantMap ContactStore::update(const QVariantMap &map)
{
    ensureLoaded();
    const QString id = map.value(u"id"_s).toString().trimmed();
    if (!m_readOnlyReason.isEmpty()) return failure(id, readOnlyError());
    const qsizetype index = indexOf(id);
    if (index < 0) return failure(id, u"There is no contact with id “%1”."_s.arg(id));
    Contact contact = m_contacts.at(index);
    applyVariantMap(contact, map);
    contact.id = id;
    normalizeContact(contact);
    const QStringList errors = validateContact(contact);
    if (!errors.isEmpty()) return failure(id, errors.constFirst(), errors);
    QStringList warnings;
    QStringList written;
    applyPhotoKeys(contact, map, &warnings, &written);
    QList<Contact> next = m_contacts;
    next[index] = contact;
    QString error;
    if (!save(next, &error)) {
        discardWritten(written);
        return failure(id, error, {}, warnings);
    }
    commit(std::move(next));
    setError({});
    emit contactsChanged();
    emit contactUpdated(id);
    return success(id, warnings);
}

QVariantMap ContactStore::remove(const QString &id)
{
    ensureLoaded();
    if (!m_readOnlyReason.isEmpty()) return failure(id, readOnlyError());
    const qsizetype index = indexOf(id);
    if (index < 0) return failure(id, u"There is no contact with id “%1”."_s.arg(id));
    QList<Contact> next = m_contacts;
    next.removeAt(index);
    QString error;
    if (!save(next, &error)) return failure(id, error);
    commit(std::move(next));
    setError({});
    emit contactsChanged();
    emit contactRemoved(id);
    return success(id, {});
}

QVariantMap ContactStore::addSender(const QString &name, const QString &address)
{
    ensureLoaded();
    QString addr = address.trimmed();
    QString display = name.trimmed();
    const qsizetype open = addr.lastIndexOf(u'<');
    if (open >= 0 && addr.endsWith(u'>')) {
        if (display.isEmpty()) display = addr.left(open).trimmed(); // "Grace Hopper <grace@…>"
        addr = addr.mid(open + 1, addr.size() - open - 2).trimmed();
    }
    if (!isPlausibleEmail(addr)) return failure({}, u"“%1” is not an e-mail address."_s.arg(address.left(80)));
    if (const qsizetype existing = indexForAddress(addr); existing >= 0) {
        QVariantMap result = success(m_contacts.at(existing).id, {});
        result.insert(u"existed"_s, true);
        return result;
    }
    QVariantMap map{{u"emails"_s, QVariantList{QVariantMap{{u"value"_s, addr}}}}};
    if (display.size() >= 2 && display.startsWith(u'"') && display.endsWith(u'"')) display = display.mid(1, display.size() - 2).trimmed();
    if (!display.isEmpty() && !display.contains(u'@')) {
        QString given = display;
        QString family;
        if (display.count(u',') == 1) { // "Lovelace, Ada"
            family = display.section(u',', 0, 0).trimmed();
            given = display.section(u',', 1).trimmed();
            display = QStringList{given, family}.join(u' ').trimmed();
        } else if (const qsizetype space = display.lastIndexOf(u' '); space > 0) {
            given = display.left(space).trimmed();
            family = display.mid(space + 1).trimmed();
        }
        map.insert(u"displayName"_s, display);
        map.insert(u"givenName"_s, given);
        map.insert(u"familyName"_s, family);
    }
    QVariantMap result = add(map);
    result.insert(u"existed"_s, false);
    return result;
}

} // namespace QindaOffice::Mail
