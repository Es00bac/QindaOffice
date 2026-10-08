// SPDX-License-Identifier: GPL-3.0-or-later
#include "ooxml_copier.h"

#include "ooxml_namespaces.h"

namespace QindaOffice::Ooxml {

bool isRelationshipIdAttribute(const XmlAttribute &attribute)
{
    if (attribute.ns == QStringView(Ns::R)) {
        return true;
    }
    // VML pictures: <v:imagedata o:relid="rId7"/>.
    return attribute.ns == QStringView(Ns::O) && attribute.name == u"relid";
}

RelationshipCopier::RelationshipCopier(std::shared_ptr<const Package> source, PackageWriter *writer)
    : m_source(std::move(source))
    , m_writer(writer)
{
}

void RelationshipCopier::rewrite(XmlElement &element, const QString &sourcePart, const QString &destPart)
{
    for (qsizetype i = element.attributes.size() - 1; i >= 0; --i) {
        XmlAttribute &attribute = element.attributes[i];
        if (!isRelationshipIdAttribute(attribute) || attribute.value.isEmpty()) {
            continue;
        }
        const QString id = copyRelationship(sourcePart, attribute.value, destPart);
        if (id.isEmpty()) {
            element.attributes.removeAt(i);
        } else {
            attribute.value = id;
        }
    }
    for (XmlChild &child : element.children) {
        if (child.element) {
            rewrite(*child.element, sourcePart, destPart);
        }
    }
}

QString RelationshipCopier::copyRelationship(const QString &sourcePart, const QString &id, const QString &destPart)
{
    if (!m_source) {
        return {};
    }
    const QString key = sourcePart + QLatin1Char('|') + id + QLatin1Char('|') + destPart;
    const auto memo = m_copiedRels.constFind(key);
    if (memo != m_copiedRels.constEnd()) {
        return memo.value();
    }
    const std::optional<Relationship> rel = m_source->relationship(sourcePart, id);
    if (!rel) {
        return {};
    }
    QString newId;
    if (rel->external) {
        newId = m_writer->addRelationship(destPart, rel->type, rel->target, true);
    } else {
        const QString target = m_source->targetPart(sourcePart, *rel);
        if (target.isEmpty()) {
            return {};
        }
        newId = m_writer->addRelationship(destPart, rel->type, copyPart(target));
    }
    m_copiedRels.insert(key, newId);
    return newId;
}

QString RelationshipCopier::copyPart(const QString &sourcePartName)
{
    const auto memo = m_copiedParts.constFind(sourcePartName);
    if (memo != m_copiedParts.constEnd()) {
        return memo.value();
    }
    QString dest = sourcePartName;
    if (m_writer->hasPart(dest)) {
        const int slash = int(dest.lastIndexOf(QLatin1Char('/')));
        const int dot = int(dest.lastIndexOf(QLatin1Char('.')));
        QString stem = dot > slash ? dest.left(dot) : dest;
        const QString ext = dot > slash ? dest.mid(dot) : QString();
        while (!stem.isEmpty() && stem.back().isDigit()) {
            stem.chop(1);
        }
        dest = m_writer->uniquePartName(stem, ext);
    }
    // Register before recursing: parts may reference each other.
    m_copiedParts.insert(sourcePartName, dest);
    m_writer->addPart(dest, m_source->data(sourcePartName), m_source->contentType(sourcePartName));
    // The part's own relationships keep their ids — its bytes are copied
    // verbatim and still name them.
    for (const Relationship &rel : m_source->relationships(sourcePartName)) {
        if (rel.external) {
            m_writer->addRelationshipWithId(dest, rel.id, rel.type, rel.target, true);
            continue;
        }
        const QString target = m_source->targetPart(sourcePartName, rel);
        if (!target.isEmpty()) {
            m_writer->addRelationshipWithId(dest, rel.id, rel.type, copyPart(target));
        }
    }
    return dest;
}

} // namespace QindaOffice::Ooxml
