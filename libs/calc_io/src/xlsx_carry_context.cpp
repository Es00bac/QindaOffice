// SPDX-License-Identifier: GPL-3.0-or-later
// The carry stage's bridge between source, destination and copier
// (xlsx_carry_context.h, M66, D-075).

#include "xlsx_carry_context.h"

#include "xlsx_xml.h"

#include "ooxml_namespaces.h"

#include <QRegularExpression>
#include <QXmlStreamReader>

namespace QindaOffice::Calc::IO::Carry {

namespace {

const QString kWorkbook = QStringLiteral("xl/workbook.xml");

// (id, type, target, external) of a written .rels part.
QList<Ooxml::Relationship> parseRels(const QByteArray &xml)
{
    QList<Ooxml::Relationship> out;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement || reader.name() != QLatin1String("Relationship")) {
            continue;
        }
        const auto a = reader.attributes();
        out.append({a.value(QLatin1String("Id")).toString(), a.value(QLatin1String("Type")).toString(),
                    a.value(QLatin1String("Target")).toString(),
                    a.value(QLatin1String("TargetMode")) == QLatin1String("External")});
    }
    return out;
}

QByteArray relsXml(const QString &owner, const QList<Ooxml::Relationship> &rels)
{
    QByteArray xml = QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                       "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (const Ooxml::Relationship &rel : rels) {
        const QString target = rel.external ? rel.target : Ooxml::relativeTargetFor(owner, rel.target);
        xml += QByteArrayLiteral("<Relationship Id=\"") + xmlAttribute(rel.id) + QByteArrayLiteral("\" Type=\"")
            + xmlAttribute(rel.type) + QByteArrayLiteral("\" Target=\"") + xmlAttribute(target) + '"'
            + (rel.external ? QByteArrayLiteral(" TargetMode=\"External\"") : QByteArray()) + QByteArrayLiteral("/>");
    }
    return xml + QByteArrayLiteral("</Relationships>");
}

// name, sheetId and r:id of each <sheet> of a workbook part.
QList<QStringList> workbookSheets(const QByteArray &workbookXml)
{
    QList<QStringList> out;
    const Ooxml::XmlElementPtr root = Ooxml::parseXml(workbookXml);
    const Ooxml::XmlElement *sheets = root ? root->child(QString::fromUtf16(Ooxml::Ns::S), u"sheets") : nullptr;
    if (sheets == nullptr) {
        return out;
    }
    for (const Ooxml::XmlElement *sheet : sheets->elements(QString::fromUtf16(Ooxml::Ns::S), u"sheet")) {
        out.append({sheet->attr(u"name"), sheet->attr(u"sheetId"),
                    sheet->attribute(QString::fromUtf16(Ooxml::Ns::R), u"id")});
    }
    return out;
}

} // namespace

QString relsPathFor(const QString &part)
{
    return Ooxml::relationshipsPartFor(part);
}

Ooxml::XmlElementPtr parse(const QByteArray &xml)
{
    return Ooxml::parseXml(xml);
}

Context::Context(QHash<QString, QByteArray> *parts, const PatchManifest &manifest,
                 std::shared_ptr<const Ooxml::Package> source)
    : m_parts(parts)
    , m_manifest(manifest)
    , m_source(std::move(source))
{
    for (auto it = parts->constBegin(); it != parts->constEnd(); ++it) {
        if (it.key().endsWith(QLatin1String(".rels")) || it.key() == QLatin1String("[Content_Types].xml")) {
            continue;
        }
        m_original.insert(it.key());
        m_writer.addPart(it.key(), QByteArray()); // the name only: never copy onto it
    }
    m_copier = std::make_unique<Ooxml::RelationshipCopier>(m_source, &m_writer);
    m_sourceWorkbook = m_source->mainPart();
    if (m_sourceWorkbook.isEmpty()) {
        m_sourceWorkbook = kWorkbook;
    }
    for (const QStringList &sheet : workbookSheets(m_source->data(m_sourceWorkbook))) {
        const auto rel = m_source->relationship(m_sourceWorkbook, sheet.at(2));
        m_sourceSheets.append({sheet.at(0), sheet.at(1), rel ? m_source->targetPart(m_sourceWorkbook, *rel) : QString()});
    }
    const QList<Ooxml::Relationship> destRels = parseRels(parts->value(relsPathFor(kWorkbook)));
    qint32 index = 0;
    for (const QStringList &sheet : workbookSheets(parts->value(kWorkbook))) {
        DestSheet dest;
        dest.index = index++;
        dest.name = sheet.at(0);
        dest.sheetId = sheet.at(1);
        dest.relId = sheet.at(2);
        for (const Ooxml::Relationship &rel : destRels) {
            if (rel.id == sheet.at(2)) {
                dest.part = Ooxml::Package::resolveTarget(kWorkbook, rel.target);
            }
        }
        for (const PatchManifest::SheetPatch &patch : manifest.sheets) {
            if (patch.sheetPart == dest.part) {
                dest.patch = &patch;
            }
        }
        m_destSheets.append(dest);
    }
}

const DestSheet *Context::destForSource(const QString &sourcePart) const
{
    if (sourcePart.isEmpty()) {
        return nullptr;
    }
    for (const DestSheet &dest : m_destSheets) {
        if (dest.patch != nullptr && dest.patch->source.part == sourcePart) {
            return &dest;
        }
    }
    return nullptr;
}

const SourceSheet *Context::sourceSheetById(const QString &sheetId) const
{
    for (const SourceSheet &sheet : m_sourceSheets) {
        if (sheet.sheetId == sheetId) {
            return &sheet;
        }
    }
    return nullptr;
}

void Context::ensureOwner(const QString &owner)
{
    if (m_owners.contains(owner) || (!owner.isEmpty() && !m_original.contains(owner))) {
        return; // seeded already, or a part this stage made (its rels are its own)
    }
    m_owners.insert(owner);
    for (const Ooxml::Relationship &rel : parseRels(m_parts->value(relsPathFor(owner)))) {
        m_writer.addRelationshipWithId(owner, rel.id, rel.type,
                                       rel.external ? rel.target : Ooxml::Package::resolveTarget(owner, rel.target),
                                       rel.external);
    }
}

QString Context::copyRelationship(const QString &sourcePart, const QString &id, const QString &destPart)
{
    ensureOwner(destPart);
    return m_copier->copyRelationship(sourcePart, id, destPart);
}

void Context::rewrite(Ooxml::XmlElement &element, const QString &sourcePart, const QString &destPart)
{
    ensureOwner(destPart);
    m_copier->rewrite(element, sourcePart, destPart);
}

QString Context::copyPart(const QString &sourcePart)
{
    return m_copier->copyPart(sourcePart);
}

void Context::replaceCopied(const QString &destPart, const QByteArray &data)
{
    m_writer.addPart(destPart, data, m_writer.contentTypeOf(destPart));
}

QString Context::addNewPart(const QString &stem, const QString &extension, const QByteArray &data,
                            const QString &contentType)
{
    const QString name = m_writer.uniquePartName(stem, extension);
    m_writer.addPart(name, data, contentType);
    return name;
}

QString Context::addRelationship(const QString &owner, const QString &type, const QString &targetPart)
{
    ensureOwner(owner);
    return m_writer.addRelationship(owner, type, targetPart);
}

void Context::warn(const QString &message)
{
    if (m_manifest.warnings != nullptr && !m_manifest.warnings->contains(message)) {
        m_manifest.warnings->append(message);
    }
}

int Context::allocateVmlBlocks(int count)
{
    if (m_nextVmlBlock < 0) {
        m_nextVmlBlock = 1;
        static const QRegularExpression idmap(QStringLiteral("<o:idmap[^>]*data=\"([^\"]*)\""));
        for (auto it = m_parts->constBegin(); it != m_parts->constEnd(); ++it) {
            if (!it.key().endsWith(QLatin1String(".vml"))) {
                continue;
            }
            const QRegularExpressionMatch match = idmap.match(QString::fromUtf8(it.value()));
            for (const QString &block : match.captured(1).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                m_nextVmlBlock = qMax(m_nextVmlBlock, block.trimmed().toInt() + 1);
            }
        }
    }
    const int first = m_nextVmlBlock;
    m_nextVmlBlock += qMax(1, count);
    return first;
}

bool Context::flush(QString *error)
{
    QByteArray types = m_parts->value(QStringLiteral("[Content_Types].xml"));
    if (types.isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("carry: the package has no [Content_Types].xml");
        }
        return false;
    }
    QSet<QString> owners = m_owners;
    for (const QString &name : m_writer.partNames()) {
        if (m_original.contains(name)) {
            continue;
        }
        m_parts->insert(name, m_writer.partData(name));
        const QString type = m_writer.contentTypeOf(name);
        if (!type.isEmpty()) {
            appendContentTypeOverride(&types, QLatin1Char('/') + name, type);
        }
        owners.insert(name);
    }
    m_parts->insert(QStringLiteral("[Content_Types].xml"), types);
    for (const QString &owner : std::as_const(owners)) {
        const QList<Ooxml::Relationship> rels = m_writer.relationshipsOf(owner);
        if (!rels.isEmpty()) {
            m_parts->insert(relsPathFor(owner), relsXml(owner, rels));
        }
    }
    return true;
}

} // namespace QindaOffice::Calc::IO::Carry
