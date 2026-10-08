// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// RelationshipCopier: carries XML preserved from a source package into a
// package being written.
//
// Preserved XML (a chart, a SmartArt diagram, a floating picture, a text
// box, a header the user never touched) refers to other parts through
// relationship ids — r:id, r:embed, r:link, r:dm, r:lo, r:qs, r:cs, o:relid.
// Those ids are only meaningful in the SOURCE package. rewrite() copies
// every part such an attribute points at (and, recursively, every part
// THOSE parts point at, keeping their own relationship ids so their bytes
// need no change), adds fresh relationships from the destination part, and
// rewrites the attribute values to the new ids.
//
// Copies are memoised: the same source part is copied once however often
// it is referenced, so a logo used on five pages is stored once.
//
// AGENT-CONTRACT: preserved fragments are stored by the format readers
// together with the SOURCE PART they came from (office_docx, show_model's
// pptx reader); rewrite() must be called with that part, or ids resolve
// against the wrong relationships file.

#include "ooxml_package.h"

#include <QHash>

#include <memory>

namespace QindaOffice::Ooxml {

class RelationshipCopier {
public:
    RelationshipCopier(std::shared_ptr<const Package> source, PackageWriter *writer);

    // Rewrites relationship-id attributes in `element` (recursively): ids
    // resolve against `sourcePart` in the source package and are re-created
    // from `destPart` in the writer. Ids that do not resolve are removed
    // from the element (a dangling id makes Word refuse the file).
    void rewrite(XmlElement &element, const QString &sourcePart, const QString &destPart);

    // Copies one relationship (by id) of `sourcePart` to `destPart` and
    // returns the new id; empty when it does not resolve.
    QString copyRelationship(const QString &sourcePart, const QString &id, const QString &destPart);

    // Copies a source part and everything it references; returns its name
    // in the writer (kept when free, else made unique).
    QString copyPart(const QString &sourcePartName);

    [[nodiscard]] const std::shared_ptr<const Package> &source() const { return m_source; }

private:
    std::shared_ptr<const Package> m_source;
    PackageWriter *m_writer;
    QHash<QString, QString> m_copiedParts; // source part → dest part
    QHash<QString, QString> m_copiedRels;  // source part|id|dest part → new id
};

// True for attributes whose value is a relationship id.
[[nodiscard]] bool isRelationshipIdAttribute(const XmlAttribute &attribute);

} // namespace QindaOffice::Ooxml
