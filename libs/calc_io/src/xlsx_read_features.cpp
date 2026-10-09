// SPDX-License-Identifier: GPL-3.0-or-later
// The read half of M66 (xlsx_read_features.h, D-075).

#include "xlsx_read_features.h"

#include "xlsx_parts.h"

#include "workbook.h"

#include <QXmlStreamReader>

#include <xlsxzipreader_p.h>

namespace QindaOffice::Calc::IO {

namespace {

const QString kRelNs = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");
const QString kSmlNs = QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main");

QString relsPathFor(const QString &part)
{
    const int slash = int(part.lastIndexOf(QLatin1Char('/')));
    return part.left(slash) + QStringLiteral("/_rels") + part.mid(slash) + QStringLiteral(".rels");
}

// An xsd:boolean attribute; `fallback` when absent.
bool flag(const QXmlStreamAttributes &attributes, QLatin1StringView name, bool fallback)
{
    if (!attributes.hasAttribute(name)) {
        return fallback;
    }
    const QStringView value = attributes.value(name);
    return value == QLatin1String("1") || value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

LineOutline lineFrom(const QXmlStreamAttributes &attributes)
{
    LineOutline line;
    line.level = quint8(qBound(0, attributes.value(QLatin1String("outlineLevel")).toInt(), int(kMaxOutlineLevel)));
    line.hidden = flag(attributes, QLatin1StringView("hidden"), false);
    line.collapsed = flag(attributes, QLatin1StringView("collapsed"), false);
    return line;
}

SheetProtection protectionFrom(const QXmlStreamAttributes &a)
{
    SheetProtection p;
    p.enabled = flag(a, QLatin1StringView("sheet"), false);
    p.algorithmName = a.value(QLatin1String("algorithmName")).toString();
    p.hashValue = a.value(QLatin1String("hashValue")).toString();
    p.saltValue = a.value(QLatin1String("saltValue")).toString();
    p.spinCount = a.value(QLatin1String("spinCount")).toInt();
    p.legacyHash = a.value(QLatin1String("password")).toString();
    p.objects = flag(a, QLatin1StringView("objects"), false);
    p.scenarios = flag(a, QLatin1StringView("scenarios"), false);
    p.formatCells = flag(a, QLatin1StringView("formatCells"), true);
    p.formatColumns = flag(a, QLatin1StringView("formatColumns"), true);
    p.formatRows = flag(a, QLatin1StringView("formatRows"), true);
    p.insertColumns = flag(a, QLatin1StringView("insertColumns"), true);
    p.insertRows = flag(a, QLatin1StringView("insertRows"), true);
    p.insertHyperlinks = flag(a, QLatin1StringView("insertHyperlinks"), true);
    p.deleteColumns = flag(a, QLatin1StringView("deleteColumns"), true);
    p.deleteRows = flag(a, QLatin1StringView("deleteRows"), true);
    p.selectLockedCells = flag(a, QLatin1StringView("selectLockedCells"), false);
    p.sort = flag(a, QLatin1StringView("sort"), true);
    p.autoFilter = flag(a, QLatin1StringView("autoFilter"), true);
    p.pivotTables = flag(a, QLatin1StringView("pivotTables"), true);
    p.selectUnlockedCells = flag(a, QLatin1StringView("selectUnlockedCells"), false);
    return p;
}

} // namespace

void applyXfFlags(quint8 flags, CellStyle *style)
{
    style->locked = (flags & 1) == 0;
    style->hiddenFormula = (flags & 2) != 0;
    if ((flags & 4) != 0) {
        style->alignment = 3; // Excel's Fill (M82, D-091)
    }
}

QVector<quint8> readXfProtection(QXlsx::ZipReader &zip)
{
    QVector<quint8> out;
    QXmlStreamReader reader(zip.fileData(QStringLiteral("xl/styles.xml")));
    bool inCellXfs = false;
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("cellXfs")) {
            break;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        if (reader.name() == QLatin1String("cellXfs")) {
            inCellXfs = true;
        } else if (inCellXfs && reader.name() == QLatin1String("xf")) {
            out.append(0);
        } else if (inCellXfs && reader.name() == QLatin1String("protection") && !out.isEmpty()) {
            const auto a = reader.attributes();
            out.last() |= quint8((flag(a, QLatin1StringView("locked"), true) ? 0 : 1)
                                 | (flag(a, QLatin1StringView("hidden"), false) ? 2 : 0));
        } else if (inCellXfs && reader.name() == QLatin1String("alignment") && !out.isEmpty()
                   && reader.attributes().value(QLatin1String("horizontal")) == QLatin1String("fill")) {
            // M82 (D-091): QXlsx writes horizontal="fill" but never reads it.
            out.last() |= quint8(4);
        }
    }
    return out;
}

QSet<int> readDynamicArrayCms(QXlsx::ZipReader &zip)
{
    QSet<int> out;
    QString part;
    for (const auto &[type, target] : readRels(zip, QStringLiteral("xl/_rels/workbook.xml.rels"))) {
        if (type.endsWith(QLatin1String("/sheetMetadata"))) {
            part = resolveTarget(QStringLiteral("xl"), target);
        }
    }
    if (part.isEmpty()) {
        return out;
    }
    QXmlStreamReader reader(zip.fileData(part));
    int typeIndex = 0;       // 1-based position in <metadataTypes>
    int xldapr = -1;         // XLDAPR's position
    QVector<bool> dynamic;   // XLDAPR future-metadata blocks: fDynamic
    bool inFuture = false;
    bool inCellMetadata = false;
    int block = 0;           // 1-based <bk> of <cellMetadata>
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        const QStringView name = reader.name();
        if (token == QXmlStreamReader::EndElement) {
            inFuture = inFuture && name != QLatin1String("futureMetadata");
            inCellMetadata = inCellMetadata && name != QLatin1String("cellMetadata");
            continue;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        const auto a = reader.attributes();
        if (name == QLatin1String("metadataType")) {
            ++typeIndex;
            if (a.value(QLatin1String("name")) == QLatin1String("XLDAPR")) {
                xldapr = typeIndex;
            }
        } else if (name == QLatin1String("futureMetadata")) {
            inFuture = a.value(QLatin1String("name")) == QLatin1String("XLDAPR");
        } else if (inFuture && name == QLatin1String("bk")) {
            dynamic.append(false);
        } else if (inFuture && name == QLatin1String("dynamicArrayProperties") && !dynamic.isEmpty()) {
            dynamic.last() = flag(a, QLatin1StringView("fDynamic"), false);
        } else if (name == QLatin1String("cellMetadata")) {
            inCellMetadata = true;
        } else if (inCellMetadata && name == QLatin1String("bk")) {
            ++block;
        } else if (inCellMetadata && name == QLatin1String("rc")) {
            const int t = a.value(QLatin1String("t")).toInt();
            const int v = a.value(QLatin1String("v")).toInt();
            if (t == xldapr && v >= 0 && v < dynamic.size() && dynamic.at(v)) {
                out.insert(block);
            }
        }
    }
    return out;
}

void readSheetFeatures(QXlsx::ZipReader &zip, const QString &sheetPart, Sheet *sheet)
{
    const QHash<QString, QPair<QString, QString>> rels = readRels(zip, relsPathFor(sheetPart));
    QXmlStreamReader reader(zip.fileData(sheetPart));
    SheetOutline outline;
    SheetSource source = sheet->source(); // the drawing reader filled modelledObjects
    source.part = sheetPart;
    QList<Hyperlink> links;
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView name = reader.name();
        const auto a = reader.attributes();
        if (name == QLatin1String("c")) {
            reader.skipCurrentElement(); // cell content is the cell walk's
        } else if (name == QLatin1String("sheetPr")) {
            source.codeName = a.value(QLatin1String("codeName")).toString();
        } else if (name == QLatin1String("outlinePr")) {
            outline.summaryBelow = flag(a, QLatin1StringView("summaryBelow"), true);
            outline.summaryRight = flag(a, QLatin1StringView("summaryRight"), true);
        } else if (reader.namespaceUri() != kSmlNs) {
            continue; // a control's <xdr:row>/<xdr:col> anchor is not a sheet row
        } else if (name == QLatin1String("row")) {
            outline.setLine(OutlineAxis::Rows, a.value(QLatin1String("r")).toInt() - 1, lineFrom(a));
        } else if (name == QLatin1String("col")) {
            const LineOutline line = lineFrom(a);
            const int last = qMin(a.value(QLatin1String("max")).toInt(), int(Sheet::kMaxCols));
            for (int col = qMax(1, a.value(QLatin1String("min")).toInt()); !line.isDefault() && col <= last; ++col) {
                outline.setLine(OutlineAxis::Columns, col - 1, line);
            }
        } else if (name == QLatin1String("sheetProtection")) {
            sheet->setProtection(protectionFrom(a));
        } else if (name == QLatin1String("hyperlink")) {
            Hyperlink link;
            link.range = rangeFromText(a.value(QLatin1String("ref")).toString()).value_or(CellRange{});
            link.display = a.value(QLatin1String("display")).toString();
            link.tooltip = a.value(QLatin1String("tooltip")).toString();
            const QString location = a.value(QLatin1String("location")).toString();
            const auto rel = rels.constFind(a.value(kRelNs, QLatin1String("id")).toString());
            if (rel != rels.constEnd() && !rel->second.isEmpty()) {
                // Excel keeps an address's #fragment in location.
                link.address = rel->second + (location.isEmpty() ? QString() : QLatin1Char('#') + location);
            } else {
                link.location = location;
            }
            if (link.range.isValid() && (!link.address.isEmpty() || !link.location.isEmpty())) {
                links.append(link);
            }
        }
    }
    sheet->setOutline(outline);
    sheet->setHyperlinks(links);
    sheet->setSource(source);
}

void markDynamicArrays(Workbook *wb, qint32 sheetIndex, const CellScan &scan, const QSet<int> &dynamicCms)
{
    Sheet &sheet = wb->sheetAt(sheetIndex);
    for (const CellScan::ArrayFormula &array : scan.arrays) {
        Cell *anchor = sheet.cellAt(array.anchor);
        if (anchor == nullptr || !anchor->ast) {
            continue;
        }
        if (array.cm > 0 && dynamicCms.contains(array.cm)) {
            anchor->dynamicArray = true;
        } else {
            // A legacy (Ctrl+Shift+Enter) array formula keeps its block
            // (M69, D-078): it evaluates as an array and fills the block
            // the file names; before M69 it became an ordinary formula.
            anchor->arrayRange = array.ref;
        }
        for (qint32 r = array.ref.topLeft.row; r <= array.ref.bottomRight.row; ++r) {
            for (qint32 c = array.ref.topLeft.col; c <= array.ref.bottomRight.col; ++c) {
                Cell *spilled = sheet.cellAt({r, c});
                if (spilled != nullptr && !spilled->ast && CellAddr{r, c} != array.anchor) {
                    spilled->spillAnchor = array.anchor;
                }
            }
        }
    }
}

void readWorkbookFeatures(QXlsx::ZipReader &zip, Workbook *wb)
{
    QXmlStreamReader reader(zip.fileData(QStringLiteral("xl/workbook.xml")));
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const auto a = reader.attributes();
        if (reader.name() == QLatin1String("workbookPr")) {
            wb->source.codeName = a.value(QLatin1String("codeName")).toString();
        } else if (reader.name() == QLatin1String("workbookProtection")) {
            WorkbookProtection &p = wb->protection;
            p.lockStructure = flag(a, QLatin1StringView("lockStructure"), false);
            p.lockWindows = flag(a, QLatin1StringView("lockWindows"), false);
            p.lockRevision = flag(a, QLatin1StringView("lockRevision"), false);
            p.algorithmName = a.value(QLatin1String("workbookAlgorithmName")).toString();
            p.hashValue = a.value(QLatin1String("workbookHashValue")).toString();
            p.saltValue = a.value(QLatin1String("workbookSaltValue")).toString();
            p.spinCount = a.value(QLatin1String("workbookSpinCount")).toInt();
            p.legacyHash = a.value(QLatin1String("workbookPassword")).toString();
            p.revisionsAlgorithmName = a.value(QLatin1String("revisionsAlgorithmName")).toString();
            p.revisionsHashValue = a.value(QLatin1String("revisionsHashValue")).toString();
            p.revisionsSaltValue = a.value(QLatin1String("revisionsSaltValue")).toString();
            p.revisionsSpinCount = a.value(QLatin1String("revisionsSpinCount")).toInt();
            p.revisionsLegacyHash = a.value(QLatin1String("revisionsPassword")).toString();
        } else if (reader.name() == QLatin1String("sheets")) {
            break; // both come before <sheets>
        }
    }
    for (const auto &[type, target] : readRels(zip, QStringLiteral("xl/_rels/workbook.xml.rels"))) {
        Q_UNUSED(target);
        wb->source.macroEnabled = wb->source.macroEnabled || type.endsWith(QLatin1String("/vbaProject"));
    }
}

} // namespace QindaOffice::Calc::IO
