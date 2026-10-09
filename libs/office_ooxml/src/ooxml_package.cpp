// SPDX-License-Identifier: GPL-3.0-or-later
#include "ooxml_package.h"

#include "ooxml_namespaces.h"

#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KZip>

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

namespace QindaOffice::Ooxml {

namespace {

QString extensionOf(const QString &partName)
{
    const int slash = int(partName.lastIndexOf(QLatin1Char('/')));
    const int dot = int(partName.lastIndexOf(QLatin1Char('.')));
    return dot > slash ? partName.mid(dot + 1).toLower() : QString();
}

QString stripSlash(const QString &name)
{
    return name.startsWith(QLatin1Char('/')) ? name.mid(1) : name;
}

bool collectFiles(const KArchiveDirectory *dir, const QString &prefix, QList<const KArchiveFile *> &files,
                  QStringList &names)
{
    for (const QString &entryName : dir->entries()) {
        const KArchiveEntry *entry = dir->entry(entryName);
        const QString path = prefix.isEmpty() ? entryName : prefix + QLatin1Char('/') + entryName;
        if (entry->isDirectory()) {
            if (!collectFiles(static_cast<const KArchiveDirectory *>(entry), path, files, names)) {
                return false;
            }
        } else {
            files.append(static_cast<const KArchiveFile *>(entry));
            names.append(path);
            if (files.size() > Package::kMaxParts) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

// ---- reading ------------------------------------------------------------------------

std::shared_ptr<const Package> Package::load(const QString &path, QString *error)
{
    std::shared_ptr<Package> package(new Package);
    if (!package->readZip(nullptr, path, error)) {
        return nullptr;
    }
    return package;
}

std::shared_ptr<const Package> Package::fromEntries(const QList<std::pair<QString, QByteArray>> &entries, QString *error)
{
    std::shared_ptr<Package> package(new Package);
    for (const auto &[name, bytes] : entries) {
        package->m_parts.insert(name, bytes);
        package->m_lowerToName.insert(name.toLower(), name);
    }
    if (!package->m_lowerToName.contains(QStringLiteral("[content_types].xml"))) {
        if (error != nullptr) {
            *error = QStringLiteral("not an Office Open XML package: [Content_Types].xml is missing");
        }
        return nullptr;
    }
    package->parseContentTypes();
    return package;
}

std::shared_ptr<const Package> Package::fromData(const QByteArray &zip, QString *error)
{
    std::shared_ptr<Package> package(new Package);
    if (!package->readZip(&zip, QString(), error)) {
        return nullptr;
    }
    return package;
}

bool Package::readZip(const QByteArray *bytes, const QString &path, QString *error)
{
    auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    QBuffer buffer;
    std::unique_ptr<KZip> zip;
    if (bytes != nullptr) {
        buffer.setData(*bytes);
        zip = std::make_unique<KZip>(&buffer);
    } else {
        zip = std::make_unique<KZip>(path);
    }
    if (!zip->open(QIODevice::ReadOnly)) {
        return fail(QStringLiteral("not a zip package (%1)").arg(zip->errorString()));
    }
    QList<const KArchiveFile *> files;
    QStringList names;
    if (!collectFiles(zip->directory(), QString(), files, names)) {
        return fail(QStringLiteral("package has more than %1 parts").arg(kMaxParts));
    }
    qint64 total = 0;
    for (qsizetype i = 0; i < files.size(); ++i) {
        const qint64 size = files.at(i)->size();
        total += size;
        if (size > kMaxPartBytes || total > kMaxTotalBytes) {
            return fail(QStringLiteral("package part %1 is too large").arg(names.at(i)));
        }
    }
    for (qsizetype i = 0; i < files.size(); ++i) {
        const QString &name = names.at(i);
        m_parts.insert(name, files.at(i)->data());
        m_lowerToName.insert(name.toLower(), name);
    }
    if (!m_lowerToName.contains(QStringLiteral("[content_types].xml"))) {
        return fail(QStringLiteral("not an Office Open XML package: [Content_Types].xml is missing"));
    }
    parseContentTypes();
    return true;
}

void Package::parseContentTypes()
{
    const XmlElementPtr root = xml(QStringLiteral("[Content_Types].xml"));
    if (!root) {
        return;
    }
    for (const XmlElement *e : root->elements()) {
        if (e->name == u"Default") {
            m_defaults.insert(e->attr(u"Extension").toLower(), e->attr(u"ContentType"));
        } else if (e->name == u"Override") {
            m_overrides.insert(stripSlash(e->attr(u"PartName")).toLower(), e->attr(u"ContentType"));
        }
    }
}

QString Package::actualName(const QString &partName) const
{
    const QString key = stripSlash(partName).toLower();
    const auto found = m_lowerToName.constFind(key);
    if (found != m_lowerToName.constEnd()) {
        return found.value();
    }
    // A Target may percent-encode what the zip stores literally.
    if (key.contains(QLatin1Char('%'))) {
        const QString decoded = QUrl::fromPercentEncoding(key.toUtf8());
        return m_lowerToName.value(decoded);
    }
    return {};
}

QStringList Package::partNames() const
{
    QStringList names = m_parts.keys();
    names.sort();
    return names;
}

bool Package::contains(const QString &partName) const
{
    return !actualName(partName).isEmpty();
}

QByteArray Package::data(const QString &partName) const
{
    const QString name = actualName(partName);
    return name.isEmpty() ? QByteArray() : m_parts.value(name);
}

QString Package::contentType(const QString &partName) const
{
    const QString key = stripSlash(partName).toLower();
    const auto over = m_overrides.constFind(key);
    if (over != m_overrides.constEnd()) {
        return over.value();
    }
    return m_defaults.value(extensionOf(key));
}

XmlElementPtr Package::xml(const QString &partName, QString *error) const
{
    const QString name = actualName(partName);
    if (name.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("part %1 is missing").arg(partName);
        }
        return nullptr;
    }
    QString parseError;
    XmlElementPtr root = parseXml(m_parts.value(name), &parseError);
    if (!root && error != nullptr) {
        *error = QStringLiteral("%1: %2").arg(partName, parseError);
    }
    return root;
}

QString relationshipsPartFor(const QString &sourcePart)
{
    const QString source = stripSlash(sourcePart);
    if (source.isEmpty()) {
        return QStringLiteral("_rels/.rels");
    }
    const int slash = int(source.lastIndexOf(QLatin1Char('/')));
    const QString dir = slash < 0 ? QString() : source.left(slash + 1);
    return dir + QStringLiteral("_rels/") + source.mid(slash + 1) + QStringLiteral(".rels");
}

QList<Relationship> Package::relationships(const QString &sourcePart) const
{
    const QString key = stripSlash(sourcePart).toLower();
    const auto cached = m_rels.constFind(key);
    if (cached != m_rels.constEnd()) {
        return cached.value();
    }
    QList<Relationship> out;
    if (const XmlElementPtr root = xml(relationshipsPartFor(sourcePart))) {
        for (const XmlElement *e : root->elements()) {
            if (e->name != u"Relationship") {
                continue;
            }
            Relationship rel;
            rel.id = e->attr(u"Id");
            rel.type = RelType::normalizeRelationshipType(e->attr(u"Type"));
            rel.target = e->attr(u"Target");
            rel.external = e->attr(u"TargetMode").compare(u"External", Qt::CaseInsensitive) == 0;
            out.append(rel);
        }
    }
    m_rels.insert(key, out);
    return out;
}

std::optional<Relationship> Package::relationship(const QString &sourcePart, const QString &id) const
{
    for (const Relationship &rel : relationships(sourcePart)) {
        if (rel.id == id) {
            return rel;
        }
    }
    return std::nullopt;
}

std::optional<Relationship> Package::firstOfType(const QString &sourcePart, const QString &type) const
{
    for (const Relationship &rel : relationships(sourcePart)) {
        if (rel.type == type) {
            return rel;
        }
    }
    return std::nullopt;
}

QString Package::resolveTarget(const QString &sourcePart, const QString &target)
{
    QString t = target;
    const int hash = int(t.indexOf(QLatin1Char('#')));
    if (hash >= 0) {
        t.truncate(hash);
    }
    if (t.startsWith(QLatin1Char('/'))) {
        return QDir::cleanPath(t).mid(1);
    }
    const QString source = stripSlash(sourcePart);
    const int slash = int(source.lastIndexOf(QLatin1Char('/')));
    const QString dir = slash < 0 ? QString() : source.left(slash);
    QString joined = QDir::cleanPath(dir.isEmpty() ? t : dir + QLatin1Char('/') + t);
    while (joined.startsWith(QStringLiteral("../"))) {
        joined = joined.mid(3); // above the package root: clamp
    }
    return stripSlash(joined);
}

QString Package::targetPart(const QString &sourcePart, const Relationship &rel) const
{
    if (rel.external) {
        return {};
    }
    const QString name = actualName(resolveTarget(sourcePart, rel.target));
    return name;
}

QString Package::mainPart() const
{
    const auto rel = firstOfType(QString(), QString::fromUtf16(RelType::OfficeDocument));
    return rel ? targetPart(QString(), *rel) : QString();
}

// ---- writing ------------------------------------------------------------------------

QString relativeTargetFor(const QString &sourcePart, const QString &partName)
{
    const QString source = stripSlash(sourcePart);
    const QString target = stripSlash(partName);
    if (source.isEmpty()) {
        return target;
    }
    const QStringList from = source.split(QLatin1Char('/')).mid(0, source.count(QLatin1Char('/')));
    const QStringList to = target.split(QLatin1Char('/'));
    qsizetype common = 0;
    while (common < from.size() && common < to.size() - 1 && from.at(common) == to.at(common)) {
        ++common;
    }
    QStringList parts;
    for (qsizetype i = common; i < from.size(); ++i) {
        parts.append(QStringLiteral(".."));
    }
    for (qsizetype i = common; i < to.size(); ++i) {
        parts.append(to.at(i));
    }
    return parts.join(QLatin1Char('/'));
}

void PackageWriter::addPart(const QString &partName, const QByteArray &data, const QString &contentType)
{
    const QString name = stripSlash(partName);
    if (!m_parts.contains(name)) {
        m_order.append(name);
    }
    m_parts.insert(name, data);
    if (!contentType.isEmpty()) {
        m_types.insert(name, contentType);
    }
}

void PackageWriter::addXmlPart(const QString &partName, const XmlElement &root, const QString &contentType)
{
    addPart(partName, serializeDocument(root), contentType);
}

bool PackageWriter::hasPart(const QString &partName) const
{
    return m_parts.contains(stripSlash(partName));
}

QByteArray PackageWriter::partData(const QString &partName) const
{
    return m_parts.value(stripSlash(partName));
}

QString PackageWriter::contentTypeOf(const QString &partName) const
{
    const QString name = stripSlash(partName);
    const QString explicitType = m_types.value(name);
    if (!explicitType.isEmpty()) {
        return explicitType;
    }
    const QString ext = extensionOf(name);
    if (ext == u"rels") {
        return QString::fromUtf16(ContentType::Relationships);
    }
    if (ext == u"xml") {
        return QString::fromUtf16(ContentType::Xml);
    }
    return ContentType::forImageExtension(ext);
}

QString PackageWriter::uniquePartName(const QString &stem, const QString &extension) const
{
    for (int n = 1;; ++n) {
        const QString candidate = stripSlash(stem) + QString::number(n) + extension;
        if (!m_parts.contains(candidate)) {
            return candidate;
        }
    }
}

QString PackageWriter::addRelationship(const QString &sourcePart, const QString &type, const QString &target,
                                       bool external)
{
    QList<Relationship> &list = m_rels[stripSlash(sourcePart)];
    int n = int(list.size()) + 1;
    auto taken = [&list](const QString &id) {
        return std::any_of(list.cbegin(), list.cend(), [&id](const Relationship &r) { return r.id == id; });
    };
    QString id = QStringLiteral("rId%1").arg(n);
    while (taken(id)) {
        id = QStringLiteral("rId%1").arg(++n);
    }
    list.append({id, type, external ? target : stripSlash(target), external});
    return id;
}

void PackageWriter::addRelationshipWithId(const QString &sourcePart, const QString &id, const QString &type,
                                          const QString &target, bool external)
{
    m_rels[stripSlash(sourcePart)].append({id, type, external ? target : stripSlash(target), external});
}

QList<Relationship> PackageWriter::relationshipsOf(const QString &sourcePart) const
{
    return m_rels.value(stripSlash(sourcePart));
}

bool PackageWriter::retargetRelationship(const QString &sourcePart, const QString &id, const QString &target)
{
    for (Relationship &rel : m_rels[stripSlash(sourcePart)]) {
        if (rel.id == id && !rel.external) {
            rel.target = stripSlash(target);
            return true;
        }
    }
    return false;
}

bool PackageWriter::removeRelationship(const QString &sourcePart, const QString &id)
{
    QList<Relationship> &list = m_rels[stripSlash(sourcePart)];
    return list.removeIf([&id](const Relationship &rel) { return rel.id == id; }) > 0;
}

QByteArray PackageWriter::contentTypesXml() const
{
    XmlElement root(QString::fromUtf16(Ns::CT), QStringLiteral("Types"));
    const QString ct = root.ns;
    QStringList defaults{QStringLiteral("rels"), QStringLiteral("xml")};
    for (const QString &name : m_order) {
        const QString ext = extensionOf(name);
        if (!m_types.contains(name) && !ext.isEmpty() && !defaults.contains(ext)
            && !ContentType::forImageExtension(ext).isEmpty()) {
            defaults.append(ext);
        }
    }
    for (const QString &ext : std::as_const(defaults)) {
        const QString type = ext == u"rels" ? QString::fromUtf16(ContentType::Relationships)
            : ext == u"xml"                ? QString::fromUtf16(ContentType::Xml)
                                           : ContentType::forImageExtension(ext);
        root.appendElement(ct, QStringLiteral("Default"))
            .setAttribute(QString(), QStringLiteral("Extension"), ext)
            .setAttribute(QString(), QStringLiteral("ContentType"), type);
    }
    for (const QString &name : m_order) {
        const QString type = m_types.value(name);
        if (type.isEmpty()) {
            continue;
        }
        root.appendElement(ct, QStringLiteral("Override"))
            .setAttribute(QString(), QStringLiteral("PartName"), QLatin1Char('/') + name)
            .setAttribute(QString(), QStringLiteral("ContentType"), type);
    }
    return serializeDocument(root);
}

QByteArray PackageWriter::relationshipsXml(const QString &sourcePart) const
{
    XmlElement root(QString::fromUtf16(Ns::RELS), QStringLiteral("Relationships"));
    for (const Relationship &rel : m_rels.value(sourcePart)) {
        XmlElement &e = root.appendElement(root.ns, QStringLiteral("Relationship"));
        e.setAttribute(QString(), QStringLiteral("Id"), rel.id);
        e.setAttribute(QString(), QStringLiteral("Type"), rel.type);
        e.setAttribute(QString(), QStringLiteral("Target"),
                       rel.external ? rel.target : relativeTargetFor(sourcePart, rel.target));
        if (rel.external) {
            e.setAttribute(QString(), QStringLiteral("TargetMode"), QStringLiteral("External"));
        }
    }
    return serializeDocument(root);
}

QByteArray PackageWriter::toZip(QString *error) const
{
    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);
    KZip zip(&buffer);
    if (!zip.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot start the zip package");
        }
        return {};
    }
    // AGENT-NOTE: KZip stamps entries with these times; a fixed instant
    // keeps byte-identical output for identical content (tests rely on it).
    const QDateTime stamp(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC);
    auto write = [&](const QString &name, const QByteArray &data, bool compress) {
        zip.setCompression(compress ? KZip::DeflateCompression : KZip::NoCompression);
        return zip.writeFile(name, data, 0100644, QString(), QString(), stamp, stamp, stamp);
    };
    bool ok = write(QStringLiteral("[Content_Types].xml"), contentTypesXml(), true);
    for (const QString &name : m_order) {
        const QString type = contentTypeOf(name);
        // Media that is compressed already is stored (M75: video and audio too).
        const bool compressed = (type.startsWith(u"image/") && !type.contains(u"svg") && !type.contains(u"emf")
                                 && !type.contains(u"wmf") && !type.contains(u"bmp"))
            || type.startsWith(u"video/") || type.startsWith(u"audio/");
        ok = ok && write(name, m_parts.value(name), !compressed);
    }
    for (auto it = m_rels.cbegin(); it != m_rels.cend(); ++it) {
        if (!it.value().isEmpty()) {
            ok = ok && write(relationshipsPartFor(it.key()), relationshipsXml(it.key()), true);
        }
    }
    ok = zip.close() && ok;
    if (!ok) {
        if (error != nullptr) {
            *error = QStringLiteral("cannot write the zip package");
        }
        return {};
    }
    return buffer.data();
}

QList<std::pair<QString, QByteArray>> PackageWriter::entries() const
{
    QList<std::pair<QString, QByteArray>> out;
    out.append({QStringLiteral("[Content_Types].xml"), contentTypesXml()});
    for (const QString &name : m_order) {
        out.append({name, m_parts.value(name)});
    }
    for (auto it = m_rels.cbegin(); it != m_rels.cend(); ++it) {
        if (!it.value().isEmpty()) {
            out.append({relationshipsPartFor(it.key()), relationshipsXml(it.key())});
        }
    }
    return out;
}

bool PackageWriter::save(const QString &path, QString *error) const
{
    const QByteArray bytes = toZip(error);
    if (bytes.isEmpty()) {
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Ooxml
