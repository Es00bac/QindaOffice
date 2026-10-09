// SPDX-License-Identifier: GPL-3.0-or-later
// Gathering a sheet's M66 features for the post-pass (xlsx_write_features.h).

#include "xlsx_write_features.h"

#include <QXmlStreamWriter>

namespace QindaOffice::Calc::IO {

namespace {

// QXlsx's own column unit (xlsx_write.cpp uses the same conversion).
qreal pxToCharWidth(qreal px)
{
    return qMax(1.0, (px - 5.0) / 7.0);
}

// A <cols> covering every sized or outlined column, one <col> each.
QByteArray colsXml(const Sheet &sheet)
{
    QList<qint32> columns = sheet.sizedColumns();
    for (auto it = sheet.outline().cols.constBegin(); it != sheet.outline().cols.constEnd(); ++it) {
        if (!columns.contains(it.key())) {
            columns.append(it.key());
        }
    }
    std::sort(columns.begin(), columns.end());
    QByteArray xml = QByteArrayLiteral("<cols>");
    for (const qint32 col : std::as_const(columns)) {
        const QByteArray n = QByteArray::number(col + 1);
        xml += QByteArrayLiteral("<col min=\"") + n + QByteArrayLiteral("\" max=\"") + n + '"';
        if (sheet.columnWidths().contains(col)) {
            xml += QByteArrayLiteral(" width=\"") + QByteArray::number(pxToCharWidth(sheet.columnWidth(col)), 'g', 15)
                + QByteArrayLiteral("\" customWidth=\"1\"");
        }
        const LineOutline line = sheet.outline().line(OutlineAxis::Columns, col);
        if (line.hidden) {
            xml += QByteArrayLiteral(" hidden=\"1\"");
        }
        if (line.level > 0) {
            xml += QByteArrayLiteral(" outlineLevel=\"") + QByteArray::number(line.level) + '"';
        }
        if (line.collapsed) {
            xml += QByteArrayLiteral(" collapsed=\"1\"");
        }
        xml += QByteArrayLiteral("/>");
    }
    return xml + QByteArrayLiteral("</cols>");
}

void writeFlag(QXmlStreamWriter &writer, const char *name, bool value, bool schemaDefault)
{
    if (value != schemaDefault) {
        writer.writeAttribute(QString::fromLatin1(name), value ? QStringLiteral("1") : QStringLiteral("0"));
    }
}

void writeHash(QXmlStreamWriter &writer, const QString &prefix, const QString &algorithm, const QString &hash,
               const QString &salt, qint32 spinCount)
{
    if (hash.isEmpty()) {
        return;
    }
    const auto name = [&prefix](const char *local) {
        return prefix.isEmpty() ? QString::fromLatin1(local)
                                : prefix + QChar(QLatin1Char(local[0])).toUpper() + QString::fromLatin1(local + 1);
    };
    writer.writeAttribute(name("algorithmName"), algorithm);
    writer.writeAttribute(name("hashValue"), hash);
    writer.writeAttribute(name("saltValue"), salt);
    writer.writeAttribute(name("spinCount"), QString::number(spinCount));
}

} // namespace

QString rangeText(const CellRange &range)
{
    return range.topLeft == range.bottomRight
        ? addrText(range.topLeft)
        : addrText(range.topLeft) + QLatin1Char(':') + addrText(range.bottomRight);
}

QByteArray sheetProtectionXml(const SheetProtection &p)
{
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartElement(QStringLiteral("sheetProtection"));
    if (!p.legacyHash.isEmpty()) {
        writer.writeAttribute(QStringLiteral("password"), p.legacyHash);
    }
    writeHash(writer, QString(), p.algorithmName, p.hashValue, p.saltValue, p.spinCount);
    writer.writeAttribute(QStringLiteral("sheet"), QStringLiteral("1"));
    writeFlag(writer, "objects", p.objects, false);
    writeFlag(writer, "scenarios", p.scenarios, false);
    writeFlag(writer, "formatCells", p.formatCells, true);
    writeFlag(writer, "formatColumns", p.formatColumns, true);
    writeFlag(writer, "formatRows", p.formatRows, true);
    writeFlag(writer, "insertColumns", p.insertColumns, true);
    writeFlag(writer, "insertRows", p.insertRows, true);
    writeFlag(writer, "insertHyperlinks", p.insertHyperlinks, true);
    writeFlag(writer, "deleteColumns", p.deleteColumns, true);
    writeFlag(writer, "deleteRows", p.deleteRows, true);
    writeFlag(writer, "selectLockedCells", p.selectLockedCells, false);
    writeFlag(writer, "sort", p.sort, true);
    writeFlag(writer, "autoFilter", p.autoFilter, true);
    writeFlag(writer, "pivotTables", p.pivotTables, true);
    writeFlag(writer, "selectUnlockedCells", p.selectUnlockedCells, false);
    writer.writeEndElement();
    return xml;
}

QByteArray workbookProtectionXml(const WorkbookProtection &p)
{
    if (!p.isProtected() && !p.hasPassword() && p.revisionsHashValue.isEmpty() && p.revisionsLegacyHash.isEmpty()) {
        return {};
    }
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartElement(QStringLiteral("workbookProtection"));
    if (!p.legacyHash.isEmpty()) {
        writer.writeAttribute(QStringLiteral("workbookPassword"), p.legacyHash);
    }
    if (!p.revisionsLegacyHash.isEmpty()) {
        writer.writeAttribute(QStringLiteral("revisionsPassword"), p.revisionsLegacyHash);
    }
    writeFlag(writer, "lockStructure", p.lockStructure, false);
    writeFlag(writer, "lockWindows", p.lockWindows, false);
    writeFlag(writer, "lockRevision", p.lockRevision, false);
    writeHash(writer, QStringLiteral("revisions"), p.revisionsAlgorithmName, p.revisionsHashValue, p.revisionsSaltValue,
              p.revisionsSpinCount);
    writeHash(writer, QStringLiteral("workbook"), p.algorithmName, p.hashValue, p.saltValue, p.spinCount);
    writer.writeEndElement();
    return xml;
}

void fillSheetFeatures(const Workbook &wb, const Sheet &sheet, PatchManifest::SheetPatch *patch)
{
    patch->name = sheet.name();
    patch->source = sheet.source();
    const SheetOutline &outline = sheet.outline();
    patch->summaryBelow = outline.summaryBelow;
    patch->summaryRight = outline.summaryRight;
    patch->rowOutline = outline.rows;
    patch->outlineLevelRow = outline.maxLevel(OutlineAxis::Rows);
    patch->outlineLevelCol = outline.maxLevel(OutlineAxis::Columns);
    if (!outline.cols.isEmpty()) {
        patch->colsXml = colsXml(sheet);
    }
    if (sheet.protection().enabled) {
        patch->sheetProtectionXml = sheetProtectionXml(sheet.protection());
    }
    for (const Hyperlink &link : sheet.hyperlinks()) {
        if (!link.range.isValid()) {
            continue;
        }
        PatchManifest::SheetPatch::Link out{rangeText(link.range), {}, link.location, link.display, link.tooltip};
        if (!link.address.isEmpty()) {
            // Excel keeps an address's #fragment in the location attribute.
            const qsizetype hash = link.address.indexOf(QLatin1Char('#'));
            out.target = hash < 0 ? link.address : link.address.left(hash);
            out.location = hash < 0 ? QString() : link.address.mid(hash + 1);
        }
        if (!out.target.isEmpty() || !out.location.isEmpty()) {
            patch->hyperlinks.append(out);
        }
    }
    // One cell for each cell format whose protection is not the default.
    QSet<qint32> seen;
    for (auto it = sheet.styleIds().constBegin(); it != sheet.styleIds().constEnd(); ++it) {
        const CellStyle *style = wb.style(it.value());
        if (style == nullptr || (style->locked && !style->hiddenFormula) || seen.contains(it.value())) {
            continue;
        }
        seen.insert(it.value());
        const CellAddr addr{qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)};
        patch->protectionCells.insert(addrText(addr), quint8((style->locked ? 0 : 1) | (style->hiddenFormula ? 2 : 0)));
    }
    for (const TableSpec &table : sheet.tables()) {
        patch->tableSources.append(table.sourcePart);
    }
}

} // namespace QindaOffice::Calc::IO
