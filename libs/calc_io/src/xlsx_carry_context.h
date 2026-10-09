// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Internal to the carry stage (M66, D-075): the bridge between the source
// copy (WorkbookSource::carried, an ooxml Package), the package being
// written (the post-pass's part map) and office_ooxml's RelationshipCopier.
//
// AGENT-NOTE: the copier writes into a PackageWriter. That writer is
// seeded with the NAME of every part already written (so a copied part
// never takes an existing name) and, per owner, with the owner's existing
// relationships (so a new id never collides); flush() then moves the new
// parts, their content types and every touched .rels back into the part
// map. Relationship ids inside carried XML are re-made by the copier;
// nothing is ever copied by name alone.

#include "xlsx_manifest.h"

#include "ooxml_copier.h"
#include "ooxml_package.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <memory>

namespace QindaOffice::Calc::IO::Carry {

struct DestSheet {
    qint32 index = -1;     // position in the written workbook
    QString part;          // the written worksheet part
    QString name;
    QString sheetId;       // its sheetId in the written workbook.xml
    const PatchManifest::SheetPatch *patch = nullptr;
};

struct SourceSheet {
    QString name;
    QString sheetId;
    QString part;
};

class Context {
public:
    Context(QHash<QString, QByteArray> *parts, const PatchManifest &manifest, std::shared_ptr<const Ooxml::Package> source);

    [[nodiscard]] const Ooxml::Package &source() const { return *m_source; }
    [[nodiscard]] const PatchManifest &manifest() const { return m_manifest; }
    [[nodiscard]] QString sourceWorkbook() const { return m_sourceWorkbook; }

    // The written package's parts. Never hold a reference across setPart.
    [[nodiscard]] QByteArray part(const QString &name) const { return m_parts->value(name); }
    [[nodiscard]] bool hasPart(const QString &name) const { return m_parts->contains(name); }
    void setPart(const QString &name, const QByteArray &data) { m_parts->insert(name, data); }

    // Source sheets in workbook order, and where each one went.
    [[nodiscard]] const QList<SourceSheet> &sourceSheets() const { return m_sourceSheets; }
    [[nodiscard]] const DestSheet *destForSource(const QString &sourcePart) const;
    [[nodiscard]] const SourceSheet *sourceSheetById(const QString &sheetId) const;
    [[nodiscard]] const QList<DestSheet> &destSheets() const { return m_destSheets; }

    // Copies `id` (a relationship of `sourcePart`) to a relationship of
    // `destPart`, with everything it reaches; the new id, or empty.
    QString copyRelationship(const QString &sourcePart, const QString &id, const QString &destPart);
    // Re-makes every relationship id in `element` (source → destination).
    void rewrite(Ooxml::XmlElement &element, const QString &sourcePart, const QString &destPart);
    // The destination name a source part was (or now is) copied to.
    QString copyPart(const QString &sourcePart);
    // Replaces the bytes of a part this stage copied (a carried part with
    // an attribute changed: refreshOnLoad, a moved location).
    void replaceCopied(const QString &destPart, const QByteArray &data);
    [[nodiscard]] QByteArray copiedData(const QString &destPart) const { return m_writer.partData(destPart); }
    // A new part this stage makes itself (a filtered threaded-comments
    // part), and a relationship from a written part to it.
    QString addNewPart(const QString &stem, const QString &extension, const QByteArray &data, const QString &contentType);
    QString addRelationship(const QString &owner, const QString &type, const QString &targetPart);

    void warn(const QString &message);

    // A fresh run of `count` legacy-drawing shape-id blocks (1024 ids each),
    // after every block a written legacy drawing already uses.
    int allocateVmlBlocks(int count);

    // Moves new parts, content types and relationships into the part map.
    [[nodiscard]] bool flush(QString *error);

private:
    void ensureOwner(const QString &owner);

    QHash<QString, QByteArray> *m_parts;
    const PatchManifest &m_manifest;
    std::shared_ptr<const Ooxml::Package> m_source;
    Ooxml::PackageWriter m_writer;
    std::unique_ptr<Ooxml::RelationshipCopier> m_copier;
    QSet<QString> m_original; // part names written before this stage
    QSet<QString> m_owners;   // parts whose .rels this stage seeded
    QString m_sourceWorkbook;
    QList<SourceSheet> m_sourceSheets;
    QList<DestSheet> m_destSheets;
    int m_nextVmlBlock = -1;
};

// The .rels part of `part` ("" = the package's).
[[nodiscard]] QString relsPathFor(const QString &part);
// Parses `xml` (nullptr when malformed) — source parts and fragments alike.
[[nodiscard]] Ooxml::XmlElementPtr parse(const QByteArray &xml);

} // namespace QindaOffice::Calc::IO::Carry
