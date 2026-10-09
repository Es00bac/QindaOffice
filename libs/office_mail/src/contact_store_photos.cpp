// SPDX-License-Identifier: GPL-3.0-or-later
// ContactStore photo files (M64). See contact_store.h.

#include "contact_store.h"

#include <atomic_file_store.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace QindaOffice::Mail {

using namespace Qt::StringLiterals;

// AGENT-GUARD: "<64 lower-case hex>.<jpg|png|gif|bmp|webp>" and nothing
// else, so a hand-edited photoFile can never point outside contact-photos/
// (it is read, handed to QML as a file: URL and deleted by this store).
bool ContactStore::validPhotoFileName(const QString &name)
{
    if (name.indexOf(u'.') != 64) return false;
    for (qsizetype i = 0; i < 64; ++i) {
        const QChar ch = name.at(i);
        if (!((ch >= u'0' && ch <= u'9') || (ch >= u'a' && ch <= u'f'))) return false;
    }
    static const QStringList extensions = {u"jpg"_s, u"png"_s, u"gif"_s, u"bmp"_s, u"webp"_s};
    return extensions.contains(name.mid(65));
}

QSet<QString> ContactStore::photoReferences(const QList<Contact> &contacts)
{
    QSet<QString> names;
    for (const Contact &contact : contacts) {
        if (!contact.photoFile.isEmpty()) names.insert(contact.photoFile);
    }
    return names;
}

// Writes the picture as contact-photos/<sha256>.<ext> (content-addressed:
// the same picture is stored once) and returns (file name, MIME type), or
// nullopt with a warning when the picture is refused.
std::optional<QPair<QString, QString>> ContactStore::storePhoto(const QByteArray &bytes, const QString &mimeHint,
                                                                const QString &who, QStringList *warnings,
                                                                QStringList *written) const
{
    if (bytes.size() > ContactLimits::kMaxPhotoBytes) {
        warnings->append(u"The photo for “%1” is larger than 1 MiB and was not stored."_s.arg(who));
        return std::nullopt;
    }
    QString mime = sniffImageMimeType(bytes);
    if (mime.isEmpty()) mime = mimeHint;
    const QString extension = imageExtensionForMimeType(mime);
    if (bytes.isEmpty() || extension.isEmpty()) {
        warnings->append(u"The photo for “%1” is not a JPEG, PNG, GIF, BMP or WebP picture and was not stored."_s.arg(who));
        return std::nullopt;
    }
    mime = extension == "jpg"_L1 ? u"image/jpeg"_s : u"image/"_s + extension;
    const QString name = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
        + u'.' + extension;
    const QString file = photoDirectory() + u'/' + name;
    if (QFileInfo(file).size() != bytes.size()) {
        QDir().mkpath(photoDirectory());
        const Core::AtomicFileStore::Result result = Core::AtomicFileStore::write(file, bytes);
        if (!result.ok) {
            warnings->append(u"The photo for “%1” could not be stored: %2"_s.arg(who, result.error));
            return std::nullopt;
        }
        written->append(name);
    }
    return QPair<QString, QString>(name, mime);
}

// The QML-only photo keys of add()/update(): "photoRemove": true clears the
// stored photo; "photoFile" (a file: URL or a local path) stores a new one.
// A refused picture leaves the contact's photo as it was (with a warning).
void ContactStore::applyPhotoKeys(Contact &contact, const QVariantMap &map, QStringList *warnings,
                                  QStringList *written) const
{
    contact.photo.clear(); // a QML map never carries bytes
    if (map.value(u"photoRemove"_s).toBool()) {
        contact.photoFile.clear();
        contact.photoMimeType.clear();
    }
    const QVariant source = map.value(u"photoFile"_s);
    const QString text = source.toString();
    if (text.isEmpty()) return;
    QString file;
    if (source.typeId() == QMetaType::QUrl) file = localPathFor(source.toUrl());
    else if (text.startsWith("file:"_L1, Qt::CaseInsensitive)) file = QUrl(text).toLocalFile();
    else if (!text.contains("://"_L1)) file = text;
    const QString who = contact.effectiveDisplayName();
    if (file.isEmpty()) {
        warnings->append(u"The photo for “%1” must be a local file; it was not changed."_s.arg(who));
        return;
    }
    QFile in(file);
    if (!in.open(QIODevice::ReadOnly)) {
        warnings->append(u"Cannot read the photo %1 (%2); it was not changed."_s.arg(file, in.errorString()));
        return;
    }
    const QByteArray bytes = in.read(ContactLimits::kMaxPhotoBytes + 1);
    if (const auto stored = storePhoto(bytes, {}, who, warnings, written)) {
        contact.photoFile = stored->first;
        contact.photoMimeType = stored->second;
    }
}

QByteArray ContactStore::readPhotoFile(const QString &fileName) const
{
    if (!validPhotoFileName(fileName)) return {};
    QFile in(photoDirectory() + u'/' + fileName);
    if (!in.open(QIODevice::ReadOnly) || in.size() > ContactLimits::kMaxPhotoBytes) return {};
    return in.readAll();
}

void ContactStore::removePhotoFiles(const QSet<QString> &fileNames) const
{
    for (const QString &name : fileNames) {
        if (validPhotoFileName(name)) QFile::remove(photoDirectory() + u'/' + name);
    }
}

// After a failed save: the files this operation wrote that the (unchanged)
// list does not reference.
void ContactStore::discardWritten(const QStringList &written) const
{
    const QSet<QString> referenced = photoReferences(m_contacts);
    QSet<QString> orphans;
    for (const QString &name : written) {
        if (!referenced.contains(name)) orphans.insert(name);
    }
    removePhotoFiles(orphans);
}

} // namespace QindaOffice::Mail
