// SPDX-License-Identifier: GPL-3.0-or-later
#include "ooxml_properties.h"

#include "ooxml_namespaces.h"

namespace QindaOffice::Ooxml {

namespace {

const QString kCp = QString::fromUtf16(Ns::CP);
const QString kDc = QString::fromUtf16(Ns::DC);
const QString kDcTerms = QString::fromUtf16(Ns::DCTERMS);
const QString kXsi = QString::fromUtf16(Ns::XSI);

QString childText(const XmlElement &root, const QString &ns, QStringView name)
{
    const XmlElement *e = root.child(ns, name);
    return e != nullptr ? e->text().trimmed() : QString();
}

void appendIfSet(XmlElement &root, const QString &ns, const QString &name, const QString &value)
{
    if (!value.isEmpty()) {
        root.appendElement(ns, name).appendText(value);
    }
}

void appendDate(XmlElement &root, const QString &name, const QDateTime &when)
{
    if (!when.isValid()) {
        return;
    }
    XmlElement &e = root.appendElement(kDcTerms, name);
    // The xsi:type VALUE names the dcterms prefix, which the serializer
    // declares because this very element is in that namespace.
    e.setAttribute(kXsi, QStringLiteral("type"), QStringLiteral("dcterms:W3CDTF"));
    e.appendText(when.toUTC().toString(Qt::ISODate));
}

} // namespace

CoreProperties readCoreProperties(const Package &package)
{
    CoreProperties out;
    const auto rel = package.firstOfType(QString(), QString::fromUtf16(RelType::CoreProperties));
    const QString part = rel ? package.targetPart(QString(), *rel) : QStringLiteral("docProps/core.xml");
    const XmlElementPtr root = package.xml(part);
    if (!root) {
        return out;
    }
    out.title = childText(*root, kDc, u"title");
    out.subject = childText(*root, kDc, u"subject");
    out.creator = childText(*root, kDc, u"creator");
    out.keywords = childText(*root, kCp, u"keywords");
    out.description = childText(*root, kDc, u"description");
    out.lastModifiedBy = childText(*root, kCp, u"lastModifiedBy");
    out.category = childText(*root, kCp, u"category");
    out.revision = childText(*root, kCp, u"revision");
    out.created = QDateTime::fromString(childText(*root, kDcTerms, u"created"), Qt::ISODate);
    out.modified = QDateTime::fromString(childText(*root, kDcTerms, u"modified"), Qt::ISODate);
    return out;
}

void writeProperties(PackageWriter &writer, const CoreProperties &properties, const QString &application,
                     int pages, int words)
{
    XmlElement core(kCp, QStringLiteral("coreProperties"));
    appendIfSet(core, kDc, QStringLiteral("title"), properties.title);
    appendIfSet(core, kDc, QStringLiteral("subject"), properties.subject);
    appendIfSet(core, kDc, QStringLiteral("creator"), properties.creator);
    appendIfSet(core, kCp, QStringLiteral("keywords"), properties.keywords);
    appendIfSet(core, kDc, QStringLiteral("description"), properties.description);
    appendIfSet(core, kCp, QStringLiteral("lastModifiedBy"), properties.lastModifiedBy);
    appendIfSet(core, kCp, QStringLiteral("revision"), properties.revision);
    appendDate(core, QStringLiteral("created"), properties.created);
    appendDate(core, QStringLiteral("modified"), properties.modified);
    appendIfSet(core, kCp, QStringLiteral("category"), properties.category);
    const QByteArray coreXml = serializeDocument(core);
    writer.addPart(QStringLiteral("docProps/core.xml"), coreXml,
                   QString::fromUtf16(ContentType::CoreProperties));
    writer.addRelationship(QString(), QString::fromUtf16(RelType::CoreProperties),
                           QStringLiteral("docProps/core.xml"));

    const QString ep = QString::fromUtf16(Ns::EP);
    XmlElement app(ep, QStringLiteral("Properties"));
    app.appendElement(ep, QStringLiteral("Application")).appendText(application);
    if (pages > 0) {
        app.appendElement(ep, QStringLiteral("Pages")).appendText(QString::number(pages));
    }
    if (words > 0) {
        app.appendElement(ep, QStringLiteral("Words")).appendText(QString::number(words));
    }
    app.appendElement(ep, QStringLiteral("DocSecurity")).appendText(QStringLiteral("0"));
    app.appendElement(ep, QStringLiteral("ScaleCrop")).appendText(QStringLiteral("false"));
    app.appendElement(ep, QStringLiteral("LinksUpToDate")).appendText(QStringLiteral("false"));
    app.appendElement(ep, QStringLiteral("SharedDoc")).appendText(QStringLiteral("false"));
    app.appendElement(ep, QStringLiteral("HyperlinksChanged")).appendText(QStringLiteral("false"));
    app.appendElement(ep, QStringLiteral("AppVersion")).appendText(QStringLiteral("16.0000"));
    writer.addXmlPart(QStringLiteral("docProps/app.xml"), app,
                      QString::fromUtf16(ContentType::ExtendedProperties));
    writer.addRelationship(QString(), QString::fromUtf16(RelType::ExtendedProperties),
                           QStringLiteral("docProps/app.xml"));
}

} // namespace QindaOffice::Ooxml
