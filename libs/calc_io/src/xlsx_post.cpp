// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_post.h"

#include "xlsx_post_book.h"
#include "xlsx_post_cells.h"
#include "xlsx_post_images.h"
#include "xlsx_post_sheet.h"
#include "xlsx_carry.h"
#include "xlsx_write_charts.h"
#include "xlsx_write_pivot.h"
#include "xlsx_write_xml.h"
#include "xlsx_xml.h"

#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KZip>

#include <QBuffer>
#include <QDir>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <algorithm>

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

// Inserts `fragment` before the first of the candidates (tag fragments like
// "<legacyDrawing" or "</worksheet>"). Returns false when none exists.
bool insertBeforeFirst(QByteArray *xml, const QStringList &candidates, const QByteArray &fragment)
{
    int earliest = -1;
    for (const QString &candidate : candidates) {
        const int found = xml->indexOf(candidate.toUtf8());
        if (found >= 0 && (earliest < 0 || found < earliest)) {
            earliest = found;
        }
    }
    if (earliest < 0) {
        return false;
    }
    xml->insert(earliest, fragment);
    return true;
}

// The largest id="N" attribute value in a drawing part (+1 for the next one).
int nextShapeId(const QByteArray &drawingXml)
{
    int maxId = 0;
    static const QRegularExpression idAttr(QStringLiteral("id=\"(\\d+)\""));
    auto it = idAttr.globalMatch(QString::fromUtf8(drawingXml));
    while (it.hasNext()) {
        maxId = qMax(maxId, it.next().captured(1).toInt());
    }
    return maxId + 1;
}

} // namespace

bool applyPatchManifest(QByteArray *package, const PatchManifest &manifest, QString *error)
{
    QHash<QString, QByteArray> parts;
    {
        QBuffer in(package);
        KZip zip(&in);
        if (!zip.open(QIODevice::ReadOnly)) {
            if (error != nullptr) {
                *error = QStringLiteral("post-pass cannot read the package QXlsx wrote");
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
        // Literal errors and typed formula results (D-069) first, while the
        // sheet is still exactly what QXlsx wrote.
        if (!retypeCells(&parts[patch.sheetPart], patch.cellTypes)) {
            if (error != nullptr) {
                *error = QStringLiteral("post-pass: a cell to retype is missing from %1").arg(patch.sheetPart);
            }
            return false;
        }
        openSheetData(&parts[patch.sheetPart]);
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
        // M66 (D-075): cm on dynamic arrays, protection, outline, sheetPr,
        // hyperlinks — after the autoFilter, so sheetProtection lands
        // before it, the way the schema orders them.
        if (!applySheetFeatures(&parts, patch, error)) {
            return false;
        }
        // Frozen panes and the selection cell live inside <sheetView> —
        // QXlsx writes it self-closed, so expand it.
        if (patch.freezeRows != 0 || patch.freezeCols != 0 || !patch.activeCell.isEmpty()) {
            QByteArray &xml = parts[patch.sheetPart];
            QByteArray view = QByteArrayLiteral("<sheetView showRuler=\"0\" workbookViewId=\"0\"/>");
            if (!xml.contains(view)) {
                if (error != nullptr) {
                    *error = QStringLiteral("post-pass: no stock <sheetView> in %1")
                                 .arg(patch.sheetPart);
                }
                return false;
            }
            QByteArray inner;
            if (patch.freezeRows != 0 || patch.freezeCols != 0) {
                // The pane's top-left cell is the first unfrozen one.
                const QString topLeft = addrText({patch.freezeRows, patch.freezeCols});
                inner = QByteArrayLiteral("<pane xSplit=\"") + QByteArray::number(patch.freezeCols)
                    + QByteArrayLiteral("\" ySplit=\"") + QByteArray::number(patch.freezeRows)
                    + QByteArrayLiteral("\" topLeftCell=\"") + topLeft.toUtf8()
                    + QByteArrayLiteral("\" activePane=\"bottomRight\" state=\"frozen\"/>");
                if (!patch.activeCell.isEmpty()) {
                    inner += QByteArrayLiteral("<selection pane=\"bottomRight\" activeCell=\"")
                        + patch.activeCell.toUtf8() + QByteArrayLiteral("\" sqref=\"")
                        + patch.activeCell.toUtf8() + QByteArrayLiteral("\"/>");
                }
            } else if (!patch.activeCell.isEmpty()) {
                inner = QByteArrayLiteral("<selection activeCell=\"") + patch.activeCell.toUtf8()
                    + QByteArrayLiteral("\" sqref=\"") + patch.activeCell.toUtf8()
                    + QByteArrayLiteral("\"/>");
            }
            xml.replace(view, QByteArrayLiteral("<sheetView showRuler=\"0\" workbookViewId=\"0\">")
                                  + inner + QByteArrayLiteral("</sheetView>"));
        }
        if (!patch.pageSetupXml.isEmpty()) {
            // Schema: pageSetup precedes headerFooter/drawing/tableParts.
            if (!insertBeforeFirst(&parts[patch.sheetPart],
                                   {QStringLiteral("<headerFooter"), QStringLiteral("<drawing"),
                                    QStringLiteral("<legacyDrawing"), QStringLiteral("<tableParts"),
                                    QStringLiteral("<extLst"), QStringLiteral("</worksheet>")},
                                   patch.pageSetupXml)) {
                if (error != nullptr) {
                    *error = QStringLiteral("post-pass: no pageSetup anchor in %1")
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
            if (!addCommentsVml(&parts, patch, error)) {
                return false;
            }
        }
    }

    // Cell formats whose protection QXlsx could not write (M66).
    QHash<int, quint8> xfProtection;
    for (const PatchManifest::SheetPatch &patch : manifest.sheets) {
        collectXfProtection(parts.value(patch.sheetPart), patch.protectionCells, &xfProtection);
    }
    if (!applyXfProtection(&parts[QStringLiteral("xl/styles.xml")], xfProtection)) {
        if (error != nullptr) {
            *error = QStringLiteral("post-pass: no <cellXfs> in xl/styles.xml");
        }
        return false;
    }

    // Workbook-level parts: defined names, calcPr, properties, theme.
    if (!applyWorkbookPatches(&parts, manifest, error)) {
        return false;
    }

    // ---- charts ------------------------------------------------------------
    int chartPartCount = 0;
    int drawingPartCount = 0;
    int tablePartCount = 0;
    for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
        if (it.key().startsWith(QLatin1String("xl/charts/chart"))) {
            ++chartPartCount;
        } else if (it.key().startsWith(QLatin1String("xl/drawings/drawing"))
                   && it.key().endsWith(QLatin1String(".xml"))) {
            ++drawingPartCount;
        } else if (it.key().startsWith(QLatin1String("xl/tables/table"))) {
            ++tablePartCount;
        }
    }
    QList<QPair<QString, QString>> tableSources; // written table part → the source part (carry stage)
    for (const PatchManifest::SheetPatch &patch : manifest.sheets) {
        if (!patch.tables.isEmpty()) {
            // One part per table (ids unique workbook-wide), sheet rels and
            // the <tableParts> element (last before </worksheet>).
            const QString relsPath = relsPathFor(patch.sheetPart);
            QByteArray relsXml = parts.value(relsPath);
            QStringList rids;
            for (const PatchManifest::TablePart &table : patch.tables) {
                const QString tablePart = QStringLiteral("xl/tables/table%1.xml").arg(++tablePartCount);
                parts.insert(tablePart, tablePartXml(table, tablePartCount));
                tableSources.append({tablePart, patch.tableSources.value(rids.size())});
                appendContentTypeOverride(
                    &parts[QStringLiteral("[Content_Types].xml")], QStringLiteral("/") + tablePart,
                    QStringLiteral("application/vnd.openxmlformats-officedocument.spreadsheetml.table+xml"));
                const QString rid = appendRelationship(
                    &relsXml,
                    QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/table"),
                    QStringLiteral("../tables/")
                        + tablePart.mid(tablePart.lastIndexOf(QLatin1Char('/')) + 1));
                if (rid.isEmpty()) {
                    if (error != nullptr) {
                        *error = QStringLiteral("post-pass: broken rels part %1").arg(relsPath);
                    }
                    return false;
                }
                rids.append(rid);
            }
            parts.insert(relsPath, relsXml);
            QByteArray tablePartsXml = QByteArrayLiteral("<tableParts count=\"")
                + QByteArray::number(rids.size()) + QByteArrayLiteral("\">");
            for (const QString &rid : rids) {
                tablePartsXml += QByteArrayLiteral("<tablePart r:id=\"") + rid.toUtf8()
                    + QByteArrayLiteral("\"/>");
            }
            tablePartsXml += QByteArrayLiteral("</tableParts>");
            if (!insertBeforeFirst(&parts[patch.sheetPart],
                                   {QStringLiteral("<extLst"), QStringLiteral("</worksheet>")},
                                   tablePartsXml)) {
                if (error != nullptr) {
                    *error = QStringLiteral("post-pass: no tableParts anchor in %1")
                                 .arg(patch.sheetPart);
                }
                return false;
            }
        }
    }

    for (const PatchManifest::ChartPatch &patch : manifest.charts) {
        if (!patch.generate) {
            // QXlsx wrote the chart but cannot say "stacked" (its grouping
            // serialization is commented out upstream).
            if (patch.stacked) {
                if (!parts.contains(patch.qxlsxChartPart)) {
                    if (error != nullptr) {
                        *error = QStringLiteral("post-pass: %1 missing").arg(patch.qxlsxChartPart);
                    }
                    return false;
                }
                QByteArray &xml = parts[patch.qxlsxChartPart];
                const QByteArray anchor = QByteArrayLiteral("<c:barDir val=\"col\"/>");
                if (!xml.contains(anchor)) {
                    if (error != nullptr) {
                        *error = QStringLiteral("post-pass: no barDir anchor in %1")
                                     .arg(patch.qxlsxChartPart);
                    }
                    return false;
                }
                xml.replace(anchor, anchor + QByteArrayLiteral("<c:grouping val=\"stacked\"/>"));
            }
            continue;
        }
        // A series-authored chart: a new chart part plus a graphicFrame
        // anchor merged into the sheet's drawing (created when absent).
        const QString chartPart = QStringLiteral("xl/charts/chart%1.xml").arg(++chartPartCount);
        parts.insert(chartPart, chartPartXml(patch.spec));
        appendContentTypeOverride(
            &parts[QStringLiteral("[Content_Types].xml")], QStringLiteral("/") + chartPart,
            QStringLiteral("application/vnd.openxmlformats-officedocument.drawingml.chart+xml"));

        QString drawingPart;
        if (!ensureDrawingPart(&parts, patch.sheetPart, &drawingPartCount, &drawingPart, error)) {
            return false;
        }
        const QString drawingRelsPath = relsPathFor(drawingPart);
        QByteArray drawingRelsXml = parts.value(drawingRelsPath);
        const QString chartRid = appendRelationship(
            &drawingRelsXml,
            QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart"),
            // Relative to xl/drawings, chart parts live one level up.
            QStringLiteral("../charts/") + chartPart.mid(chartPart.lastIndexOf(QLatin1Char('/')) + 1));
        if (chartRid.isEmpty()) {
            if (error != nullptr) {
                *error = QStringLiteral("post-pass: broken rels part %1").arg(drawingRelsPath);
            }
            return false;
        }
        parts.insert(drawingRelsPath, drawingRelsXml);

        QByteArray &drawingXml = parts[drawingPart];
        const QByteArray frame = graphicFrameAnchorXml(chartRid, nextShapeId(drawingXml),
                                                       patch.spec.anchorCol, patch.spec.anchorRow,
                                                       patch.toCol, patch.toRow);
        if (!insertBeforeFirst(&drawingXml, {QStringLiteral("</xdr:wsDr>")}, frame)) {
            if (error != nullptr) {
                *error = QStringLiteral("post-pass: malformed drawing %1").arg(drawingPart);
            }
            return false;
        }
    }

    int mediaCount = 0;
    for (auto it = parts.constBegin(); it != parts.constEnd(); ++it) {
        if (it.key().startsWith(QLatin1String("xl/media/"))) {
            ++mediaCount;
        }
    }
    if (!applyImagePatches(&parts, manifest.images, &drawingPartCount, &mediaCount, error)) {
        return false;
    }

    // M74 (D-083): PivotTables written from the model, before the carry
    // stage copies the source's untouched ones and merges their caches.
    if (!applyPivotParts(&parts, manifest, error)) {
        return false;
    }
    // M66 (D-075, D-068 rule 2): the parts the engine does not model, from
    // the package the workbook came from, wired to the parts written above.
    if (!applyCarry(&parts, manifest, tableSources, &drawingPartCount, error)) {
        return false;
    }

    // Deterministic part order: [Content_Types].xml first (the OPC
    // convention), the package relationships next, then by name.
    QStringList names = parts.keys();
    std::sort(names.begin(), names.end(), [](const QString &a, const QString &b) {
        const auto rank = [](const QString &name) {
            return name == QLatin1String("[Content_Types].xml") ? 0 : (name == QLatin1String("_rels/.rels") ? 1 : 2);
        };
        return rank(a) != rank(b) ? rank(a) < rank(b) : a < b;
    });
    QByteArray rewritten;
    {
        QBuffer outBuffer(&rewritten);
        KZip out(&outBuffer);
        if (!out.open(QIODevice::WriteOnly)) {
            if (error != nullptr) {
                *error = QStringLiteral("post-pass cannot build the package");
            }
            return false;
        }
        for (const QString &name : std::as_const(names)) {
            if (!out.writeFile(name, parts.value(name))) {
                if (error != nullptr) {
                    *error = QStringLiteral("post-pass failed writing %1").arg(name);
                }
                return false;
            }
        }
        if (!out.close()) {
            if (error != nullptr) {
                *error = QStringLiteral("post-pass failed finishing the package");
            }
            return false;
        }
    }
    *package = rewritten;
    return true;
}

} // namespace QindaOffice::Calc::IO
