// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_bibliography.h"
#include "ooxml_copier.h"
#include <QDate>
#include <QUuid>

namespace QindaOffice::Docx {
namespace {
using Ooxml::XmlElement;
using Ooxml::XmlElementPtr;
const QString kB = QString::fromUtf16(kBibliographyNs);
const QString kCustom = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/customXml");
const QList<QPair<QString, QString>> kFields{
    {QStringLiteral("tag"), QStringLiteral("Tag")}, {QStringLiteral("type"), QStringLiteral("SourceType")},
    {QStringLiteral("title"), QStringLiteral("Title")}, {QStringLiteral("year"), QStringLiteral("Year")},
    {QStringLiteral("publisher"), QStringLiteral("Publisher")}, {QStringLiteral("city"), QStringLiteral("City")},
    {QStringLiteral("journal"), QStringLiteral("JournalName")}, {QStringLiteral("volume"), QStringLiteral("Volume")},
    {QStringLiteral("issue"), QStringLiteral("Issue")}, {QStringLiteral("pages"), QStringLiteral("Pages")},
    {QStringLiteral("url"), QStringLiteral("URL")}, {QStringLiteral("site"), QStringLiteral("InternetSiteTitle")}};
QString textOf(const XmlElement &node, const QString &name)
{
    const auto *child = node.child(kB, name); return child ? child->text() : QString();
}
void setText(XmlElement &node, const QString &name, const QString &text)
{
    node.removeChildren(kB, name);
    if (!text.isEmpty()) node.appendElement(kB, name).appendText(text);
}
QVariantMap valuesOf(const XmlElement &element)
{
    QVariantMap values;
    for (const auto &[key, name] : kFields) values.insert(key, textOf(element, name));
    const auto *authors = element.child(kB, u"Author");
    const auto *author = authors ? authors->child(kB, u"Author") : nullptr;
    const auto *corporate = author ? author->child(kB, u"Corporate") : nullptr;
    QStringList names;
    if (const auto *list = author ? author->child(kB, u"NameList") : nullptr) {
        for (const auto *person : list->elements(kB, u"Person")) {
            const QString first = (textOf(*person, QStringLiteral("First")) + QLatin1Char(' ')
                                   + textOf(*person, QStringLiteral("Middle"))).trimmed();
            QString name = textOf(*person, QStringLiteral("Last"));
            if (!first.isEmpty()) name += (name.isEmpty() ? QString() : QStringLiteral(", ")) + first;
            names.append(name);
        }
    }
    values.insert(QStringLiteral("corporate"), corporate != nullptr);
    values.insert(QStringLiteral("author"), corporate ? corporate->text() : names.join(QStringLiteral("; ")));
    const QDate date(textOf(element, QStringLiteral("YearAccessed")).toInt(),
                     textOf(element, QStringLiteral("MonthAccessed")).toInt(), textOf(element, QStringLiteral("DayAccessed")).toInt());
    values.insert(QStringLiteral("accessed"), date.isValid() ? date.toString(Qt::ISODate) : QString());
    return values;
}
XmlElementPtr sourceXml(const BibliographySource &source)
{
    auto node = Ooxml::parseFragment(source.sourceXml);
    if (!node) node = std::make_shared<XmlElement>(kB, QStringLiteral("Source"));
    for (const auto &[key, name] : kFields) if (source.sourceXml.isEmpty() || source.values.value(key) != source.imported.value(key))
        setText(*node, name, source.values.value(key).toString());
    if (source.sourceXml.isEmpty() || source.values.value(QStringLiteral("author")) != source.imported.value(QStringLiteral("author"))
        || source.values.value(QStringLiteral("corporate")) != source.imported.value(QStringLiteral("corporate"))) {
        auto *authors = node->child(kB, u"Author");
        if (!authors) authors = &node->appendElement(kB, QStringLiteral("Author"));
        authors->removeChildren(kB, u"Author"); // retain Editor, Translator and other roles
        const QString text = source.values.value(QStringLiteral("author")).toString().trimmed();
        if (!text.isEmpty()) {
            auto &author = authors->appendElement(kB, QStringLiteral("Author"));
            if (source.values.value(QStringLiteral("corporate")).toBool()) author.appendElement(kB, QStringLiteral("Corporate")).appendText(text);
            else {
                auto &list = author.appendElement(kB, QStringLiteral("NameList"));
                for (const QString &name : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                    auto &person = list.appendElement(kB, QStringLiteral("Person"));
                    setText(person, QStringLiteral("Last"), name.section(QLatin1Char(','), 0, 0).trimmed());
                    setText(person, QStringLiteral("First"), name.section(QLatin1Char(','), 1).trimmed());
                }
            }
        }
    }
    if (source.sourceXml.isEmpty() || source.values.value(QStringLiteral("accessed")) != source.imported.value(QStringLiteral("accessed"))) {
        const QDate date = QDate::fromString(source.values.value(QStringLiteral("accessed")).toString(), Qt::ISODate);
        setText(*node, QStringLiteral("YearAccessed"), date.isValid() ? QString::number(date.year()) : QString());
        setText(*node, QStringLiteral("MonthAccessed"), date.isValid() ? QString::number(date.month()) : QString());
        setText(*node, QStringLiteral("DayAccessed"), date.isValid() ? QString::number(date.day()) : QString());
    }
    if (source.sourceXml.isEmpty() && !node->child(kB, u"Guid")) setText(*node, QStringLiteral("Guid"), QUuid::createUuid().toString().toUpper());
    return node;
}
void addProperties(Ooxml::PackageWriter &writer, const QString &part)
{
    const QString ns = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/customXml");
    XmlElement props(ns, QStringLiteral("datastoreItem"));
    props.setAttribute(ns, QStringLiteral("itemID"), QUuid::createUuid().toString().toUpper());
    props.appendElement(ns, QStringLiteral("schemaRefs")).appendElement(ns, QStringLiteral("schemaRef")).setAttribute(ns, QStringLiteral("uri"), kB);
    const QString name = writer.uniquePartName(QStringLiteral("customXml/itemProps"), QStringLiteral(".xml"));
    writer.addXmlPart(name, props, QStringLiteral("application/vnd.openxmlformats-officedocument.customXmlProperties+xml"));
    writer.addRelationship(part, kCustom + QStringLiteral("Props"), name);
}
} // namespace
Bibliography readBibliography(const std::shared_ptr<const Ooxml::Package> &package)
{
    Bibliography data;
    if (!package) return data;
    for (const auto &rel : package->relationships(package->mainPart())) {
        if (rel.external || rel.type != kCustom) continue;
        const QString part = package->targetPart(package->mainPart(), rel);
        const auto root = package->xml(part);
        if (!root || !root->is(kB, u"Sources")) continue;
        data.roots.insert(part, Ooxml::serializeFragment(*root));
        if (data.roots.size() == 1) {
            const QString style = root->attr(u"SelectedStyle").toLower();
            const QString uri = root->attr(u"URI");
            data.style = style.contains(u"ieee") || uri.endsWith(u":numeric") ? QStringLiteral("numeric")
                : style.isEmpty() || style.contains(u"apa") || uri.endsWith(u":authorDate") ? QStringLiteral("authorDate") : QStringLiteral("preserved");
        }
        for (const auto *node : root->elements(kB, u"Source")) {
            BibliographySource source; source.values = valuesOf(*node); source.imported = source.values;
            source.sourcePart = part; source.sourceXml = Ooxml::serializeFragment(*node); data.sources.append(source);
        }
    }
    data.importedStyle = data.style; return data;
}
QSet<QString> writeBibliography(const Bibliography &data, const std::shared_ptr<const Ooxml::Package> &source,
                                Ooxml::PackageWriter &writer, const QString &story)
{
    QSet<QString> replaced;
    QStringList groups = data.roots.keys();
    if (groups.isEmpty() && !data.sources.isEmpty()) groups.append(QString());
    for (const QString &group : groups) {
        auto root = Ooxml::parseFragment(data.roots.value(group));
        const int previousCount = root ? int(root->elements(kB, u"Source").size()) : 0;
        QList<const BibliographySource *> records;
        bool changed = !root || data.style != data.importedStyle;
        for (const auto &record : data.sources) if (record.sourcePart == group || (record.sourcePart.isEmpty() && group == groups.first())) {
            records.append(&record); changed = changed || !record.unchanged();
        }
        changed = changed || records.size() != previousCount;
        if (!changed) continue;
        if (!root) root = std::make_shared<XmlElement>(kB, QStringLiteral("Sources"));
        if (group.isEmpty() || data.style != data.importedStyle) {
            const bool numeric = data.style == u"numeric";
            root->setAttribute({}, QStringLiteral("SelectedStyle"), numeric ? QStringLiteral("IEEE.XSL") : QStringLiteral("APA.XSL"));
            root->setAttribute({}, QStringLiteral("StyleName"), numeric ? QStringLiteral("Numeric") : QStringLiteral("Author-date"));
            root->setAttribute({}, QStringLiteral("URI"), QStringLiteral("urn:qindaoffice:references:") + data.style);
        }
        root->removeChildren(kB, u"Source");
        for (const auto *record : records) root->appendElement(sourceXml(*record));
        const QString part = !group.isEmpty() && !writer.hasPart(group) ? group
            : writer.uniquePartName(QStringLiteral("customXml/item"), QStringLiteral(".xml"));
        writer.addXmlPart(part, *root, QStringLiteral("application/xml"));
        if (source && !group.isEmpty()) {
            Ooxml::RelationshipCopier copier(source, &writer);
            for (const auto &rel : source->relationships(group)) copier.copyRelationship(group, rel.id, part);
        } else addProperties(writer, part);
        writer.addRelationship(story, kCustom, part);
        if (!group.isEmpty()) replaced.insert(group);
    }
    return replaced;
}
} // namespace QindaOffice::Docx
