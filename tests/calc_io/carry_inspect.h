// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Following a saved package's wiring for the M66 carried-parts gates
// (D-075): which part a relationship of a given type leads to, the part an
// r:id inside some element names, and whether every r:id in a part
// resolves — so a test asserts "the pivot table is reachable from the
// sheet" rather than "a file of that name exists".

#include "excel_inspect.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

namespace QindaOffice::Test {

// With QQO_KEEP_XLSX_DIR set, a copy of a saved package lands there — for
// opening the M66 outputs in Excel or LibreOffice by hand (verification.md).
inline void keepForInspection(const QString &path)
{
    const QString dir = qEnvironmentVariable("QQO_KEEP_XLSX_DIR");
    if (!dir.isEmpty() && QDir().mkpath(dir)) {
        const QString target = QDir(dir).filePath(QFileInfo(path).fileName());
        QFile::remove(target);
        QFile::copy(path, target);
    }
}

// The parts `source` reaches through relationships of `type`.
inline QStringList partsByType(const Ooxml::Package &package, const QString &source, const QString &type)
{
    QStringList out;
    for (const Ooxml::Relationship &rel : package.relationships(source)) {
        if (rel.type == type && !rel.external) {
            out.append(package.targetPart(source, rel));
        }
    }
    return out;
}

// The part an r:id (or any relationship-id attribute value) resolves to
// from `source`; empty when it dangles.
inline QString partForId(const Ooxml::Package &package, const QString &source, const QString &id)
{
    const auto rel = package.relationship(source, id);
    return rel && !rel->external ? package.targetPart(source, *rel) : QString();
}

// Every r:* attribute value inside `element` (any depth).
inline QStringList relationshipIds(const Ooxml::XmlElement &element)
{
    QStringList out;
    for (const Ooxml::XmlAttribute &attribute : element.attributes) {
        if (attribute.ns == QStringView(Ooxml::Ns::R)) {
            out.append(attribute.value);
        }
    }
    for (const Ooxml::XmlChild &child : element.children) {
        if (child.element) {
            out += relationshipIds(*child.element);
        }
    }
    return out;
}

// Ids used in an XML part that its .rels does not define (Excel refuses a
// package with one). Empty when everything resolves.
inline QStringList danglingIds(const Ooxml::Package &package, const QString &part)
{
    QStringList out;
    const Ooxml::XmlElementPtr root = package.xml(part);
    if (!root) {
        return out;
    }
    for (const QString &id : relationshipIds(*root)) {
        if (!package.relationship(part, id)) {
            out.append(part + QStringLiteral(" → ") + id);
        }
    }
    return out;
}

// Every part of the package checked by danglingIds, plus relationships
// whose internal target does not exist.
inline QStringList brokenWiring(const Ooxml::Package &package)
{
    QStringList out;
    QStringList sources = package.partNames();
    sources.prepend(QString());
    for (const QString &part : std::as_const(sources)) {
        if (part.endsWith(QLatin1String(".xml"))) {
            out += danglingIds(package, part);
        }
        if (part.endsWith(QLatin1String(".rels"))) {
            continue;
        }
        for (const Ooxml::Relationship &rel : package.relationships(part)) {
            if (!rel.external && !package.contains(package.targetPart(part, rel))) {
                out.append(QStringLiteral("%1 → missing %2").arg(part, rel.target));
            }
        }
    }
    return out;
}

// The first descendant element with this local name in any namespace.
inline const Ooxml::XmlElement *findLocal(const Ooxml::XmlElement &element, QStringView name)
{
    for (const Ooxml::XmlChild &child : element.children) {
        if (!child.element) {
            continue;
        }
        if (child.element->name == name) {
            return child.element.get();
        }
        if (const Ooxml::XmlElement *found = findLocal(*child.element, name)) {
            return found;
        }
    }
    return nullptr;
}

// <sheet name> → sheetId of a saved workbook.
inline QHash<QString, QString> savedSheetIds(const Ooxml::Package &package)
{
    QHash<QString, QString> out;
    const Ooxml::XmlElementPtr root = package.xml(QStringLiteral("xl/workbook.xml"));
    const Ooxml::XmlElement *sheets = root ? root->child(sml(), u"sheets") : nullptr;
    for (const Ooxml::XmlElement *sheet : sheets != nullptr ? sheets->elements(sml(), u"sheet") : QList<const Ooxml::XmlElement *>()) {
        out.insert(sheet->attr(u"name"), sheet->attr(u"sheetId"));
    }
    return out;
}

// The worksheet part a saved workbook gives the named sheet.
inline QString savedSheetPart(const Ooxml::Package &package, const QString &name)
{
    const QString workbook = QStringLiteral("xl/workbook.xml");
    const Ooxml::XmlElementPtr root = package.xml(workbook);
    const Ooxml::XmlElement *sheets = root ? root->child(sml(), u"sheets") : nullptr;
    for (const Ooxml::XmlElement *sheet : sheets != nullptr ? sheets->elements(sml(), u"sheet") : QList<const Ooxml::XmlElement *>()) {
        if (sheet->attr(u"name") == name) {
            return partForId(package, workbook, sheet->attribute(QString::fromUtf16(Ooxml::Ns::R), u"id"));
        }
    }
    return {};
}

} // namespace QindaOffice::Test
