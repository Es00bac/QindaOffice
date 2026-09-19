// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_post.h"

#include "xlsx_write_charts.h"
#include "xlsx_write_xml.h"
#include "xlsx_xml.h"

#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KZip>

#include <QDir>
#include <QRegularExpression>
#include <QXmlStreamReader>

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

// The drawing part a sheet's rels point at, or empty.
QString drawingPartFor(const QByteArray &sheetRelsXml)
{
    QXmlStreamReader reader(sheetRelsXml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement
            || reader.name() != QLatin1String("Relationship")) {
            continue;
        }
        const auto attributes = reader.attributes();
        if (attributes.value(QLatin1String("Type")).toString()
            .endsWith(QLatin1String("/drawing"))) {
            // Targets here are relative to xl/worksheets.
            return QDir::cleanPath(QStringLiteral("xl/worksheets/")
                                   + attributes.value(QLatin1String("Target")).toString());
        }
    }
    return {};
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
        }
    }

    // Workbook-level print names: merge into xl/workbook.xml's
    // <definedNames> (created when QXlsx wrote none).
    if (!manifest.printAreas.isEmpty() || !manifest.printTitles.isEmpty()) {
        QByteArray &workbookXml = parts[QStringLiteral("xl/workbook.xml")];
        QByteArray entries;
        for (const auto &[sheetId, body] : manifest.printAreas) {
            entries += QByteArrayLiteral("<definedName name=\"_xlnm.Print_Area\" localSheetId=\"")
                + QByteArray::number(sheetId) + QByteArrayLiteral("\">") + body.toUtf8()
                + QByteArrayLiteral("</definedName>");
        }
        for (const auto &[sheetId, body] : manifest.printTitles) {
            entries += QByteArrayLiteral("<definedName name=\"_xlnm.Print_Titles\" localSheetId=\"")
                + QByteArray::number(sheetId) + QByteArrayLiteral("\">") + body.toUtf8()
                + QByteArrayLiteral("</definedName>");
        }
        if (workbookXml.contains("</definedNames>")) {
            workbookXml.replace("</definedNames>", entries + "</definedNames>");
        } else if (!insertAfterTag(&workbookXml, QStringLiteral("sheets"),
                                   QByteArrayLiteral("<definedNames>") + entries
                                       + QByteArrayLiteral("</definedNames>"))) {
            // <definedNames> follows <sheets> in the workbook schema.
            if (error != nullptr) {
                *error = QStringLiteral("post-pass: no </sheets> in xl/workbook.xml");
            }
            return false;
        }
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
    for (const PatchManifest::SheetPatch &patch : manifest.sheets) {
        if (!patch.tables.isEmpty()) {
            // One part per table (ids unique workbook-wide), sheet rels and
            // the <tableParts> element (last before </worksheet>).
            const QString relsPath = relsPathFor(patch.sheetPart);
            QByteArray relsXml = parts.value(relsPath);
            QStringList rids;
            for (const TableSpec &table : patch.tables) {
                const QString tablePart = QStringLiteral("xl/tables/table%1.xml").arg(++tablePartCount);
                parts.insert(tablePart, tablePartXml(table, tablePartCount));
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

        const QString sheetRelsPath = relsPathFor(patch.sheetPart);
        QByteArray sheetRelsXml = parts.value(sheetRelsPath);
        QString drawingPart = drawingPartFor(sheetRelsXml);
        if (drawingPart.isEmpty()) {
            drawingPart = QStringLiteral("xl/drawings/drawing%1.xml").arg(++drawingPartCount);
            parts.insert(drawingPart,
                         QByteArrayLiteral(
                             "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
                             "<xdr:wsDr xmlns:xdr=\"http://schemas.openxmlformats.org/drawingml/2006/spreadsheetDrawing\">"
                             "</xdr:wsDr>"));
            appendContentTypeOverride(
                &parts[QStringLiteral("[Content_Types].xml")], QStringLiteral("/") + drawingPart,
                QStringLiteral("application/vnd.openxmlformats-officedocument.drawing+xml"));
            const QString drawingRid = appendRelationship(
                &sheetRelsXml,
                QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships/drawing"),
                QStringLiteral("../drawings/") + drawingPart.mid(drawingPart.lastIndexOf(QLatin1Char('/')) + 1));
            if (drawingRid.isEmpty()) {
                if (error != nullptr) {
                    *error = QStringLiteral("post-pass: broken rels part %1").arg(sheetRelsPath);
                }
                return false;
            }
            // <drawing> precedes legacyDrawing/picture/tableParts/extLst in
            // the worksheet schema; the close tag is the fallback anchor.
            if (!insertBeforeFirst(&parts[patch.sheetPart],
                                   {QStringLiteral("<legacyDrawing"), QStringLiteral("<picture"),
                                    QStringLiteral("<tableParts"), QStringLiteral("<extLst"),
                                    QStringLiteral("</worksheet>")},
                                   QByteArrayLiteral("<drawing r:id=\"") + drawingRid.toUtf8()
                                       + QByteArrayLiteral("\"/>"))) {
                if (error != nullptr) {
                    *error = QStringLiteral("post-pass: no drawing anchor in %1")
                                 .arg(patch.sheetPart);
                }
                return false;
            }
        }
        parts.insert(sheetRelsPath, sheetRelsXml);

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
