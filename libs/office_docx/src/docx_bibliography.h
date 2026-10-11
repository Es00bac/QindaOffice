// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Document-local Word bibliography sources (M94, D-102). Values own their
// data; immutable copies are cheap and participate in Write's text undo.
// Unknown source fields/types and root attributes remain in sourceXml.
#include "ooxml_package.h"
#include <QVariantMap>
#include <QSet>

namespace QindaOffice::Docx {
inline constexpr char16_t kBibliographyNs[] = u"http://schemas.openxmlformats.org/officeDocument/2006/bibliography";
struct BibliographySource {
    // tag, type (Book/JournalArticle/InternetSite), title, author
    // ("Last, First; Last, First" or a corporate name), corporate (bool),
    // year, publisher, city, journal, volume, issue, pages, url, accessed.
    QVariantMap values;
    QVariantMap imported;
    QString sourcePart;
    QString sourceXml;
    [[nodiscard]] QString tag() const { return values.value(QStringLiteral("tag")).toString(); }
    [[nodiscard]] bool unchanged() const { return !sourceXml.isEmpty() && values == imported; }
    bool operator==(const BibliographySource &) const = default;
};
struct Bibliography {
    QList<BibliographySource> sources;
    QMap<QString, QString> roots; // source part → original b:Sources fragment
    QString style = QStringLiteral("authorDate"); // "authorDate" or "numeric"
    QString importedStyle = QStringLiteral("authorDate");
    [[nodiscard]] const BibliographySource *find(const QString &tag) const;
    bool operator==(const Bibliography &) const = default;
};
[[nodiscard]] Bibliography readBibliography(const std::shared_ptr<const Ooxml::Package> &package);
// Parts written here are returned so the caller omits their originals
// from its generic customXml carry pass. Unchanged parts use that pass.
[[nodiscard]] QSet<QString> writeBibliography(const Bibliography &data,
                                             const std::shared_ptr<const Ooxml::Package> &source,
                                             Ooxml::PackageWriter &writer, const QString &story);
[[nodiscard]] bool supportedSourceType(const QString &type);
// Validate a complete editor record before mutating the document.
bool validateSource(const QVariantMap &values, QString *error = nullptr);
[[nodiscard]] QString citationText(const BibliographySource &source, const QString &style,
                                   int number, const QString &pages = QString());
[[nodiscard]] QString bibliographyText(const BibliographySource &source, const QString &style, int number);
} // namespace QindaOffice::Docx
