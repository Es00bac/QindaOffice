// SPDX-License-Identifier: GPL-3.0-or-later
#include "pptx_write_context.h"

#include "ooxml_namespaces.h"

#include <QCryptographicHash>

#include <functional>

namespace QindaOffice::Show::Pptx {

QString relType(const char *name)
{
    return QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/") + QLatin1String(name);
}

QString extensionForMime(const QString &mime, const QByteArray &bytes)
{
    if (mime == u"image/png") {
        return QStringLiteral("png");
    }
    if (mime == u"image/jpeg") {
        return QStringLiteral("jpeg");
    }
    if (mime == u"image/gif") {
        return QStringLiteral("gif");
    }
    if (mime == u"image/bmp") {
        return QStringLiteral("bmp");
    }
    if (mime == u"image/tiff") {
        return QStringLiteral("tiff");
    }
    if (mime == u"image/x-emf" || mime == u"image/emf") {
        return QStringLiteral("emf");
    }
    if (mime == u"image/x-wmf" || mime == u"image/wmf") {
        return QStringLiteral("wmf");
    }
    if (mime == u"image/svg+xml") {
        return QStringLiteral("svg");
    }
    // Bytes the app stored without a type (pictures inserted from disk).
    if (bytes.startsWith("\x89PNG")) {
        return QStringLiteral("png");
    }
    if (bytes.startsWith("\xFF\xD8")) {
        return QStringLiteral("jpeg");
    }
    if (bytes.startsWith("GIF8")) {
        return QStringLiteral("gif");
    }
    if (bytes.startsWith("BM")) {
        return QStringLiteral("bmp");
    }
    if (bytes.startsWith("II*") || bytes.startsWith("MM\x00*")) {
        return QStringLiteral("tiff");
    }
    if (bytes.size() > 44 && bytes.mid(40, 4) == " EMF") {
        return QStringLiteral("emf");
    }
    if (bytes.contains("<svg")) {
        return QStringLiteral("svg");
    }
    return QStringLiteral("png");
}

WriteSession::WriteSession(const Presentation &d)
    : deck(d)
{
    if (deck.origin && deck.origin->package) {
        source = deck.origin->package;
        copier = std::make_unique<Ooxml::RelationshipCopier>(source, &writer);
    }
}

QString WriteSession::media(const QByteArray &bytes, const QString &mime)
{
    const QByteArray key = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
    const auto found = m_media.constFind(key);
    if (found != m_media.constEnd()) {
        return found.value();
    }
    const QString extension = extensionForMime(mime, bytes);
    const QString part = writer.uniquePartName(QStringLiteral("ppt/media/image"), QLatin1Char('.') + extension);
    writer.addPart(part, bytes, Ooxml::ContentType::forImageExtension(extension));
    m_media.insert(key, part);
    return part;
}

QString WriteSession::mediaFile(const QByteArray &bytes, const QString &mime, const QString &name)
{
    const QByteArray key = QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
    const auto found = m_media.constFind(key);
    if (found != m_media.constEnd()) {
        return found.value();
    }
    QString extension = mediaExtensionForMime(mime);
    if (extension.isEmpty()) {
        extension = name.section(QLatin1Char('.'), -1).toLower();
    }
    if (extension.isEmpty() || extension == name.toLower()) {
        extension = probeMedia(bytes).extension;
    }
    if (extension.isEmpty()) {
        extension = QStringLiteral("bin");
    }
    QString type = mime.isEmpty() ? mediaMimeForExtension(extension) : mime;
    if (type.isEmpty()) {
        type = QStringLiteral("application/octet-stream");
    }
    const QString part = writer.uniquePartName(QStringLiteral("ppt/media/media"), QLatin1Char('.') + extension);
    writer.addPart(part, bytes, type);
    m_media.insert(key, part);
    return part;
}

PartWriter::PartWriter(WriteSession &s, QString name)
    : session(s)
    , part(std::move(name))
{
}

QString PartWriter::relate(const QString &type, const QString &target, bool external)
{
    return session.writer.addRelationship(part, type, target, external);
}

void PartWriter::carry(Ooxml::XmlElement &element, const QString &sourcePart)
{
    if (session.copier) {
        session.copier->rewrite(element, sourcePart, part);
    } else {
        // Nothing to resolve against: drop the ids (a dangling id makes
        // PowerPoint refuse the file).
        std::function<void(Ooxml::XmlElement &)> strip = [&strip](Ooxml::XmlElement &e) {
            e.attributes.removeIf([](const Ooxml::XmlAttribute &a) { return Ooxml::isRelationshipIdAttribute(a); });
            for (Ooxml::XmlChild &c : e.children) {
                if (c.element) {
                    strip(*c.element);
                }
            }
        };
        strip(element);
    }
}

Ooxml::XmlElementPtr PartWriter::hyperlink(const QString &target)
{
    if (target.isEmpty()) {
        return nullptr;
    }
    auto link = std::make_shared<Ooxml::XmlElement>(nsA(), QStringLiteral("hlinkClick"));
    if (target.startsWith(QLatin1Char('#'))) {
        const QString slidePart = session.slideParts.value(target.mid(1));
        if (slidePart.isEmpty()) {
            return nullptr; // the slide is gone
        }
        link->setAttribute(nsR(), QStringLiteral("id"), relate(relType("slide"), slidePart));
        link->setAttribute(QString(), QStringLiteral("action"), QStringLiteral("ppaction://hlinksldjump"));
    } else if (target.startsWith(QLatin1String("ppaction:"))) {
        link->setAttribute(nsR(), QStringLiteral("id"), QString());
        link->setAttribute(QString(), QStringLiteral("action"), target);
    } else {
        link->setAttribute(nsR(), QStringLiteral("id"), relate(relType("hyperlink"), target, true));
    }
    return link;
}

quint32 PartWriter::claim(quint32 wanted)
{
    if (wanted != 0 && !m_taken.contains(wanted)) {
        m_taken.insert(wanted);
        m_reserved.insert(wanted);
        return wanted;
    }
    quint32 id = 2;
    for (const quint32 used : m_reserved + m_taken) {
        id = qMax(id, used + 1);
    }
    m_taken.insert(id);
    m_reserved.insert(id);
    return id;
}

} // namespace QindaOffice::Show::Pptx
