// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_post.h"

#include "xlsx_xml.h"

#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KZip>

#include <functional>

namespace QindaOffice::Calc::IO {

namespace {

// KZip writing truncates the archive, so the post-pass round-trips every
// entry it does not touch (the same full-copy shape as the read-side
// normalizer).
bool readAllEntries(KZip &zip, QHash<QString, QByteArray> *parts)
{
    std::function<void(const KArchiveDirectory *, const QString &)> walk =
        [&](const KArchiveDirectory *dir, const QString &prefix) {
            for (const QString &name : dir->entries()) {
                const KArchiveEntry *entry = dir->entry(name);
                const QString full = prefix.isEmpty() ? name : prefix + QLatin1Char('/') + name;
                if (entry->isDirectory()) {
                    walk(static_cast<const KArchiveDirectory *>(entry), full);
                } else {
                    parts->insert(full, static_cast<const KArchiveFile *>(entry)->data());
                }
            }
        };
    walk(zip.directory(), QString());
    return true;
}

QString relsPathFor(const QString &part)
{
    const int slash = part.lastIndexOf(QLatin1Char('/'));
    return part.left(slash) + QStringLiteral("/_rels") + part.mid(slash)
        + QStringLiteral(".rels");
}

} // namespace

bool applyPatchManifest(const QString &path, const PatchManifest &manifest, QString *error)
{
    QHash<QString, QByteArray> parts;
    {
        KZip zip(path);
        if (!zip.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = QStringLiteral("post-pass cannot read %1").arg(path);
            }
            return false;
        }
        readAllEntries(zip, &parts);
        zip.close();
    }

    for (const PatchManifest::SheetPatch &patch : manifest.sheets) {
        if (!parts.contains(patch.sheetPart)) {
            if (error != nullptr) {
                *error = QStringLiteral("post-pass: %1 missing from the package")
                             .arg(patch.sheetPart);
            }
            return false;
        }
        if (!patch.autoFilterXml.isEmpty()) {
            // Schema order: autoFilter follows sheetData (the optional
            // sheetProtection/scenarios between them are things QXlsx never
            // writes), and </sheetData> is always there.
            if (!insertAfterTag(&parts[patch.sheetPart], QStringLiteral("sheetData"),
                                patch.autoFilterXml)) {
                if (error != nullptr) {
                    *error = QStringLiteral("post-pass: no </sheetData> in %1")
                                 .arg(patch.sheetPart);
                }
                return false;
            }
        }
        if (!patch.commentsPart.isEmpty()) {
            parts.insert(patch.commentsPart, patch.commentsXml);
            const QString relsPath = relsPathFor(patch.sheetPart);
            QByteArray relsXml = parts.value(relsPath);
            // The target is relative to the worksheet part's directory
            // (xl/worksheets) — comments parts live one level up in xl/.
            QString target = patch.commentsPart;
            const int slash = patch.sheetPart.lastIndexOf(QLatin1Char('/'));
            const QString ownerDir = patch.sheetPart.left(slash);
            if (target.startsWith(ownerDir + QLatin1Char('/'))) {
                target = target.mid(ownerDir.size() + 1);
            } else if (ownerDir == QLatin1String("xl/worksheets")
                       && target.startsWith(QLatin1String("xl/"))) {
                target = QStringLiteral("../") + target.mid(3);
            }
            const QString id = appendRelationship(
                &relsXml,
                QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/comments"),
                target);
            if (id.isEmpty()) {
                if (error != nullptr) {
                    *error = QStringLiteral("post-pass: broken rels part %1").arg(relsPath);
                }
                return false;
            }
            parts.insert(relsPath, relsXml);
            appendContentTypeOverride(&parts[QStringLiteral("[Content_Types].xml")],
                                      QStringLiteral("/") + patch.commentsPart,
                                      QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.comments+xml"));
        }
    }

    KZip out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        if (error != nullptr) {
            *error = QStringLiteral("post-pass cannot write %1").arg(path);
        }
        return false;
    }
    for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
        if (!out.writeFile(it.key(), it.value())) {
            if (error != nullptr) {
                *error = QStringLiteral("post-pass failed writing %1").arg(it.key());
            }
            return false;
        }
    }
    if (!out.close()) {
        if (error != nullptr) {
            *error = QStringLiteral("post-pass failed closing %1").arg(path);
        }
        return false;
    }
    return true;
}

} // namespace QindaOffice::Calc::IO
