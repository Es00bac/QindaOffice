// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Open Packaging Conventions (ECMA-376 Part 2): a zip of parts, a
// [Content_Types].xml naming their media types, and _rels/*.rels parts
// linking them. Package reads one into memory; PackageWriter builds one.
//
// Part names are stored WITHOUT the leading slash ("word/document.xml").
// Lookups ignore ASCII case, as OPC requires — Word itself never relies on
// that, but packages from other producers do.
//
// Ownership/lifetime: a loaded Package is immutable and shared
// (std::shared_ptr<const Package>) — a document keeps the package it was
// opened from so a save can carry over the parts the suite does not model.
// Threading: a const Package may be read from any thread.
//
// AGENT-GUARD: load() enforces size limits (kMaxParts, kMaxPartBytes,
// kMaxTotalBytes) BEFORE inflating anything; hostile zips with huge
// declared sizes are rejected with an error instead of exhausting memory.

#include "ooxml_xml.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMap>
#include <QString>

#include <memory>
#include <optional>

namespace QindaOffice::Ooxml {

struct Relationship {
    QString id;
    QString type;   // normalised to the Transitional URI
    QString target; // as written in the .rels part
    bool external = false;
};

class Package {
public:
    static constexpr int kMaxParts = 20000;
    static constexpr qint64 kMaxPartBytes = qint64(512) * 1024 * 1024;
    static constexpr qint64 kMaxTotalBytes = qint64(2) * 1024 * 1024 * 1024;

    [[nodiscard]] static std::shared_ptr<const Package> load(const QString &path, QString *error = nullptr);
    [[nodiscard]] static std::shared_ptr<const Package> fromData(const QByteArray &zip,
                                                               QString *error = nullptr);

    [[nodiscard]] QStringList partNames() const;
    [[nodiscard]] bool contains(const QString &partName) const;
    // Bytes of a part; empty when absent.
    [[nodiscard]] QByteArray data(const QString &partName) const;
    // Media type from [Content_Types].xml (Override, then Default by
    // extension); empty when undeclared.
    [[nodiscard]] QString contentType(const QString &partName) const;
    // The part parsed as XML; nullptr when absent or malformed.
    [[nodiscard]] XmlElementPtr xml(const QString &partName, QString *error = nullptr) const;

    // Relationships whose source is `sourcePart` ("" = the package itself).
    [[nodiscard]] QList<Relationship> relationships(const QString &sourcePart) const;
    [[nodiscard]] std::optional<Relationship> relationship(const QString &sourcePart,
                                                           const QString &id) const;
    [[nodiscard]] std::optional<Relationship> firstOfType(const QString &sourcePart,
                                                          const QString &type) const;
    // Part name a relationship target resolves to from `sourcePart`
    // ("word/document.xml" + "media/image1.png" → "word/media/image1.png").
    [[nodiscard]] static QString resolveTarget(const QString &sourcePart, const QString &target);
    // The part a relationship points at, or empty for external/missing.
    [[nodiscard]] QString targetPart(const QString &sourcePart, const Relationship &rel) const;

    // The officeDocument part ("word/document.xml", "ppt/presentation.xml", …).
    [[nodiscard]] QString mainPart() const;

private:
    Package() = default;
    bool readZip(const QByteArray *bytes, const QString &path, QString *error);
    void parseContentTypes();
    [[nodiscard]] QString actualName(const QString &partName) const;

    QHash<QString, QByteArray> m_parts;   // actual name → bytes
    QHash<QString, QString> m_lowerToName; // lower-case → actual name
    QHash<QString, QString> m_overrides;   // lower-case part name → type
    QHash<QString, QString> m_defaults;    // lower-case extension → type
    mutable QHash<QString, QList<Relationship>> m_rels; // lazily parsed
};

// The .rels part describing `sourcePart` ("word/document.xml" →
// "word/_rels/document.xml.rels"; "" → "_rels/.rels").
[[nodiscard]] QString relationshipsPartFor(const QString &sourcePart);

// `target` written relative to the directory of `sourcePart`, the form
// every Office producer uses ("word/document.xml" + "word/media/a.png" →
// "media/a.png"; package-level sources get the part name itself).
[[nodiscard]] QString relativeTargetFor(const QString &sourcePart, const QString &partName);

class PackageWriter {
public:
    // Adds or replaces a part. `contentType` may be empty for extensions
    // with a Default entry (rels, xml, the image types).
    void addPart(const QString &partName, const QByteArray &data, const QString &contentType = QString());
    void addXmlPart(const QString &partName, const XmlElement &root, const QString &contentType);
    [[nodiscard]] bool hasPart(const QString &partName) const;
    [[nodiscard]] QByteArray partData(const QString &partName) const;
    [[nodiscard]] QStringList partNames() const { return m_order; }
    [[nodiscard]] QString contentTypeOf(const QString &partName) const;

    // A part name not yet used: stem + n + extension, n from 1
    // ("word/media/image", ".png" → "word/media/image3.png").
    [[nodiscard]] QString uniquePartName(const QString &stem, const QString &extension) const;

    // Adds a relationship from `sourcePart` ("" = package) and returns its
    // fresh id. Internal targets are PART NAMES (made relative on output);
    // external targets are written verbatim with TargetMode="External".
    QString addRelationship(const QString &sourcePart, const QString &type, const QString &target,
                            bool external = false);
    // Same, with a caller-chosen id (must be unused for that source).
    void addRelationshipWithId(const QString &sourcePart, const QString &id, const QString &type,
                               const QString &target, bool external = false);
    [[nodiscard]] QList<Relationship> relationshipsOf(const QString &sourcePart) const;

    // The finished zip: [Content_Types].xml first, then parts in insertion
    // order, then the .rels parts. Media types already compressed (images)
    // are stored, everything else deflated.
    [[nodiscard]] QByteArray toZip(QString *error = nullptr) const;
    // toZip() written atomically through QSaveFile.
    bool save(const QString &path, QString *error = nullptr) const;

private:
    [[nodiscard]] QByteArray contentTypesXml() const;
    [[nodiscard]] QByteArray relationshipsXml(const QString &sourcePart) const;

    QHash<QString, QByteArray> m_parts;
    QHash<QString, QString> m_types; // part → explicit content type
    QStringList m_order;
    QMap<QString, QList<Relationship>> m_rels; // source part → rels (targets as part names)
};

} // namespace QindaOffice::Ooxml
