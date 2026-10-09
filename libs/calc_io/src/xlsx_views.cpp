// SPDX-License-Identifier: GPL-3.0-or-later
// Excel's Custom Views in the package (xlsx_views.h, M82, D-091).

#include "xlsx_views.h"

#include "xlsx_parts.h"
#include "xlsx_print.h"
#include "xlsx_read_xml.h"
#include "xlsx_write_xml.h"
#include "xlsx_xml.h"

#include "custom_view.h"

#include "ooxml_xml.h"

#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

#include <xlsxzipreader_p.h>

namespace QindaOffice::Calc::IO {

namespace {

const QRegularExpression &viewName()
{
    static const QRegularExpression re(QStringLiteral(R"(^Z_([0-9A-Fa-f]{8})_([0-9A-Fa-f]{4})_([0-9A-Fa-f]{4})_([0-9A-Fa-f]{4})_([0-9A-Fa-f]{12})_\.wvu\.(\w+)$)"));
    return re;
}

QString nameStem(const QString &guid)
{
    QString core = guid;
    core.remove(QLatin1Char('{')).remove(QLatin1Char('}')).replace(QLatin1Char('-'), QLatin1Char('_'));
    return QStringLiteral("Z_") + core.toUpper() + QStringLiteral("_.wvu.");
}

bool flagValue(const QString &value, bool fallback)
{
    return value.isEmpty() ? fallback : value == u"1" || value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

// The areas of a name's body, sheet qualifiers dropped: "'A, B'!$3:$5,
// 'A, B'!$9:$9" → {"$3:$5", "$9:$9"} (a comma inside quotes is the sheet's).
QStringList areasOf(const QString &body)
{
    QStringList out;
    QString current;
    bool quoted = false;
    for (const QChar c : body) {
        if (c == QLatin1Char('\'')) {
            quoted = !quoted;
        }
        if (c == QLatin1Char(',') && !quoted) {
            out.append(current);
            current.clear();
        } else {
            current.append(c);
        }
    }
    out.append(current);
    for (QString &area : out) {
        const qsizetype bang = area.lastIndexOf(QLatin1Char('!'));
        area = (bang >= 0 ? area.mid(bang + 1) : area).remove(QLatin1Char('$')).trimmed();
    }
    return out;
}

// "3:5" → rows 2..4; "C:D" → columns 2..3; nothing for a cell area.
bool linesOf(const QString &area, bool rows, QList<qint32> *into)
{
    const QStringList ends = area.split(QLatin1Char(':'));
    if (ends.size() != 2) {
        return false;
    }
    if (rows) {
        bool a = false;
        bool b = false;
        const int first = ends.at(0).toInt(&a);
        const int last = ends.at(1).toInt(&b);
        for (int line = first; a && b && line <= last && line - first < Sheet::kMaxRows; ++line) {
            into->append(line - 1);
        }
        return a && b;
    }
    const std::optional<CellRange> first = rangeFromText(ends.at(0) + QStringLiteral("1"));
    const std::optional<CellRange> last = rangeFromText(ends.at(1) + QStringLiteral("1"));
    for (qint32 col = first ? first->topLeft.col : 1; first && last && col <= last->topLeft.col; ++col) {
        into->append(col);
    }
    return first.has_value() && last.has_value();
}

// Whole rows or columns as a name's areas: runs of lines.
QString linesText(const QString &sheet, const QList<qint32> &lines, bool rows)
{
    QStringList areas;
    const QString quoted = quotedSheetName(sheet);
    for (qsizetype i = 0; i < lines.size();) {
        qsizetype j = i;
        while (j + 1 < lines.size() && lines.at(j + 1) == lines.at(j) + 1) {
            ++j;
        }
        const QString a = rows ? QString::number(lines.at(i) + 1) : colName(lines.at(i));
        const QString b = rows ? QString::number(lines.at(j) + 1) : colName(lines.at(j));
        areas.append(QStringLiteral("%1!$%2:$%3").arg(quoted, a, b));
        i = j + 1;
    }
    return areas.join(QLatin1Char(','));
}

// Lines first..last (whole rows or columns) as one area.
QString spanText(const QString &sheet, qint32 first, qint32 last, bool rows)
{
    const QString a = rows ? QString::number(first + 1) : colName(first);
    const QString b = rows ? QString::number(last + 1) : colName(last);
    return QStringLiteral("%1!$%2:$%3").arg(quotedSheetName(sheet), a, b);
}

QString areaText(const QString &sheet, const CellRange &range)
{
    return QStringLiteral("%1!%2:%3").arg(quotedSheetName(sheet), addrText(range.topLeft, true, true),
                                         addrText(range.bottomRight, true, true));
}

QString attribute(const Ooxml::XmlElement &e, QStringView name)
{
    return e.attr(name);
}

void readSheetPart(const Ooxml::XmlElement &view, Sheet::CustomView *part)
{
    static const QStringList known{QStringLiteral("guid"), QStringLiteral("scale"), QStringLiteral("topLeftCell"),
                                   QStringLiteral("state"), QStringLiteral("hiddenRows"), QStringLiteral("hiddenColumns"),
                                   QStringLiteral("filter"), QStringLiteral("showAutoFilter"), QStringLiteral("printArea"),
                                   QStringLiteral("fitToPage")};
    part->guid = attribute(view, u"guid").toUpper();
    part->scale = attribute(view, u"scale").isEmpty() ? 100 : attribute(view, u"scale").toInt();
    if (const auto cell = rangeFromText(attribute(view, u"topLeftCell"))) {
        part->topLeft = cell->topLeft;
    }
    part->sheetHidden = attribute(view, u"state") == u"hidden" || attribute(view, u"state") == u"veryHidden";
    part->filter = flagValue(attribute(view, u"filter"), false);
    for (const Ooxml::XmlAttribute &a : view.attributes) {
        if (a.ns.isEmpty() && !known.contains(a.name)) {
            part->attributes.append({a.name, a.value});
        }
    }
    if (const Ooxml::XmlElement *pane = view.child(view.ns, u"pane")) {
        const QString state = attribute(*pane, u"state");
        if (state == u"frozen" || state == u"frozenSplit") {
            part->freezeRows = qint32(attribute(*pane, u"ySplit").toDouble());
            part->freezeCols = qint32(attribute(*pane, u"xSplit").toDouble());
        } else {
            part->splitX = attribute(*pane, u"xSplit").toDouble();
            part->splitY = attribute(*pane, u"ySplit").toDouble();
        }
        if (const auto cell = rangeFromText(attribute(*pane, u"topLeftCell"))) {
            part->splitTopLeft = cell->topLeft;
        }
    }
    if (const Ooxml::XmlElement *selection = view.child(view.ns, u"selection")) {
        if (const auto cell = rangeFromText(attribute(*selection, u"activeCell"))) {
            part->activeCell = cell->topLeft;
        }
    }
    const QByteArray xml = Ooxml::serializeFragment(view).toUtf8();
    // The view's print elements are a worksheet's (the reader takes the
    // view as the root and reads its direct children).
    if (view.child(view.ns, u"pageSetup") != nullptr || view.child(view.ns, u"pageMargins") != nullptr) {
        Sheet scratch(QStringLiteral("view"));
        readSheetPrint(xml, &scratch);
        part->printSettings = true;
        part->pageSetup = scratch.pageSetup();
        part->pageSetup.fitToPage = flagValue(attribute(view, u"fitToPage"), false);
    }
    if (const Ooxml::XmlElement *filter = view.child(view.ns, u"autoFilter")) {
        part->autoFilter = readAutoFilter(Ooxml::serializeFragment(*filter).toUtf8());
        part->filter = part->filter || part->autoFilter.active;
    }
}

// The worksheet's <customSheetViews>, parsed with the root's namespaces.
QList<Sheet::CustomView> readSheetParts(const QByteArray &sheetXml)
{
    QList<Sheet::CustomView> parts;
    const qsizetype open = sheetXml.indexOf("<customSheetViews");
    const qsizetype close = sheetXml.indexOf("</customSheetViews>");
    if (open < 0 || close < open) {
        return parts;
    }
    // The root's start tag carries the namespace declarations the slice
    // needs (the default one at least).
    const qsizetype rootStart = sheetXml.indexOf("<worksheet");
    const qsizetype rootEnd = rootStart < 0 ? -1 : sheetXml.indexOf('>', rootStart);
    if (rootEnd < 0) {
        return parts;
    }
    QByteArray rootTag = sheetXml.mid(rootStart, rootEnd - rootStart + 1);
    if (rootTag.endsWith("/>")) {
        rootTag.chop(2);
        rootTag += '>';
    }
    const QByteArray wrapped = rootTag + sheetXml.mid(open, close + 19 - open) + QByteArrayLiteral("</worksheet>");
    const Ooxml::XmlElementPtr root = Ooxml::parseXml(wrapped);
    const Ooxml::XmlElement *list = root ? root->child(root->ns, u"customSheetViews") : nullptr;
    for (const Ooxml::XmlElement *view : list != nullptr ? list->elements(root->ns, u"customSheetView")
                                                         : QList<const Ooxml::XmlElement *>()) {
        Sheet::CustomView part;
        readSheetPart(*view, &part);
        parts.append(part);
    }
    return parts;
}

} // namespace

bool isCustomViewName(const QString &name)
{
    return viewName().match(name).hasMatch();
}

qsizetype pivotCachesAnchor(const QByteArray &workbookXml)
{
    const qsizetype views = workbookXml.indexOf("</customWorkbookViews>");
    if (views >= 0) {
        return views + 22;
    }
    const qsizetype calc = workbookXml.indexOf("<calcPr");
    const qsizetype calcEnd = calc < 0 ? -1 : workbookXml.indexOf('>', calc);
    return calcEnd < 0 ? -1 : calcEnd + 1;
}

void readCustomViews(QXlsx::ZipReader &zip, Workbook *wb)
{
    const QByteArray workbookXml = zip.fileData(QStringLiteral("xl/workbook.xml"));
    if (!workbookXml.contains("customWorkbookView") && !workbookXml.contains(".wvu.")) {
        return;
    }
    QHash<QString, QString> sheetIds; // sheetId → name
    QList<CustomViewSpec> views;
    QXmlStreamReader reader(workbookXml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const auto a = reader.attributes();
        if (reader.name() == QLatin1String("sheet")) {
            sheetIds.insert(a.value(QLatin1String("sheetId")).toString(), a.value(QLatin1String("name")).toString());
        } else if (reader.name() == QLatin1String("customWorkbookView")) {
            CustomViewSpec view;
            for (const QXmlStreamAttribute &attr : a) {
                const QString name = attr.name().toString();
                const QString value = attr.value().toString();
                if (name == u"name") {
                    view.name = value;
                } else if (name == u"guid") {
                    view.guid = value.toUpper();
                } else if (name == u"includePrintSettings") {
                    view.includePrintSettings = flagValue(value, true);
                } else if (name == u"includeHiddenRowCol") {
                    view.includeHiddenRowCol = flagValue(value, true);
                } else if (name == u"activeSheetId") {
                    view.activeSheet = value; // a sheetId until the sheets are known
                } else {
                    view.attributes.append({name, value});
                }
            }
            views.append(view);
        }
    }
    QSet<QString> guids;
    for (CustomViewSpec &view : views) {
        view.activeSheet = sheetIds.value(view.activeSheet);
        guids.insert(view.guid);
    }
    wb->customViews = views;
    // Each sheet's part (a part whose view the workbook does not list is
    // no view at all, and is dropped), then the names that list its hidden
    // lines, print area and titles.
    const QVector<QString> parts = sheetPartMap(zip);
    for (qint32 s = 0; s < wb->sheetCount() && s < parts.size(); ++s) {
        if (!parts.at(s).isEmpty()) {
            QList<Sheet::CustomView> read = readSheetParts(zip.fileData(parts.at(s)));
            read.removeIf([&guids](const Sheet::CustomView &part) { return !guids.contains(part.guid); });
            wb->sheetAt(s).setCustomViews(read);
        }
    }
    QXmlStreamReader names(workbookXml);
    while (!names.atEnd()) {
        if (names.readNext() != QXmlStreamReader::StartElement || names.name() != QLatin1String("definedName")) {
            continue;
        }
        const QRegularExpressionMatch match = viewName().match(names.attributes().value(QLatin1String("name")).toString());
        const qint32 sheet = names.attributes().value(QLatin1String("localSheetId")).toInt();
        const QString body = names.readElementText().trimmed();
        if (!match.hasMatch() || sheet < 0 || sheet >= wb->sheetCount()) {
            continue;
        }
        const QString guid = QStringLiteral("{%1-%2-%3-%4-%5}")
                                 .arg(match.captured(1), match.captured(2), match.captured(3), match.captured(4), match.captured(5))
                                 .toUpper();
        QList<Sheet::CustomView> sheetParts = wb->sheetAt(sheet).customViews();
        for (Sheet::CustomView &part : sheetParts) {
            if (part.guid != guid) {
                continue;
            }
            const QString kind = match.captured(6);
            const QStringList areas = areasOf(body);
            if (kind == u"Rows" || kind == u"Cols") {
                for (const QString &area : areas) {
                    linesOf(area, kind == u"Rows", kind == u"Rows" ? &part.hiddenRows : &part.hiddenColumns);
                }
            } else if (kind == u"PrintArea") {
                part.printArea = rangeFromText(areas.value(0));
            } else if (kind == u"PrintTitles") {
                for (const QString &area : areas) {
                    QList<qint32> rows;
                    QList<qint32> cols;
                    if (linesOf(area, true, &rows) && !rows.isEmpty()) {
                        part.printTitleRows = CellRange{{rows.first(), 0}, {rows.last(), Sheet::kMaxCols - 1}};
                    } else if (linesOf(area, false, &cols) && !cols.isEmpty()) {
                        part.printTitleCols = CellRange{{0, cols.first()}, {Sheet::kMaxRows - 1, cols.last()}};
                    }
                }
            } else if (kind == u"FilterData" && !part.autoFilter.range.isValid()) {
                if (const auto range = rangeFromText(areas.value(0))) {
                    part.autoFilter.range = *range;
                }
            }
        }
        wb->sheetAt(sheet).setCustomViews(sheetParts);
    }
}

// ---- writing --------------------------------------------------------------------

QByteArray customWorkbookViewsXml(const Workbook &wb)
{
    if (wb.customViews.isEmpty()) {
        return {};
    }
    QByteArray xml = QByteArrayLiteral("<customWorkbookViews>");
    for (const CustomViewSpec &view : wb.customViews) {
        const qint32 active = qMax(0, wb.sheetIndex(view.activeSheet));
        xml += QByteArrayLiteral("<customWorkbookView name=\"") + xmlAttribute(view.name) + QByteArrayLiteral("\" guid=\"")
            + xmlAttribute(view.guid) + '"';
        if (!view.includePrintSettings) {
            xml += QByteArrayLiteral(" includePrintSettings=\"0\"");
        }
        if (!view.includeHiddenRowCol) {
            xml += QByteArrayLiteral(" includeHiddenRowCol=\"0\"");
        }
        bool width = false;
        bool height = false;
        for (const auto &[name, value] : view.attributes) {
            if (name == u"activeSheetId") {
                continue;
            }
            width = width || name == u"windowWidth";
            height = height || name == u"windowHeight";
            xml += ' ' + name.toUtf8() + QByteArrayLiteral("=\"") + xmlAttribute(value) + '"';
        }
        // windowWidth, windowHeight and activeSheetId are required.
        if (!width) {
            xml += QByteArrayLiteral(" windowWidth=\"1920\"");
        }
        if (!height) {
            xml += QByteArrayLiteral(" windowHeight=\"1000\"");
        }
        xml += QByteArrayLiteral(" activeSheetId=\"") + QByteArray::number(active + 1) + QByteArrayLiteral("\"/>");
    }
    return xml + QByteArrayLiteral("</customWorkbookViews>");
}

QByteArray customSheetViewsXml(const Workbook &wb, qint32 sheetIndex)
{
    const Sheet &sheet = wb.sheetAt(sheetIndex);
    if (sheet.customViews().isEmpty()) {
        return {};
    }
    QByteArray xml = QByteArrayLiteral("<customSheetViews>");
    for (const Sheet::CustomView &view : sheet.customViews()) {
        xml += QByteArrayLiteral("<customSheetView guid=\"") + xmlAttribute(view.guid) + '"';
        if (view.scale != 100) {
            xml += QByteArrayLiteral(" scale=\"") + QByteArray::number(qBound(10, view.scale, 400)) + '"';
        }
        for (const auto &[name, value] : view.attributes) {
            xml += ' ' + name.toUtf8() + QByteArrayLiteral("=\"") + xmlAttribute(value) + '"';
        }
        if (view.printSettings && view.pageSetup.fitToPage) {
            xml += QByteArrayLiteral(" fitToPage=\"1\"");
        }
        if (view.printSettings && view.printArea.has_value()) {
            xml += QByteArrayLiteral(" printArea=\"1\"");
        }
        if (view.filter) {
            xml += QByteArrayLiteral(" filter=\"1\" showAutoFilter=\"1\"");
        }
        if (!view.hiddenRows.isEmpty()) {
            xml += QByteArrayLiteral(" hiddenRows=\"1\"");
        }
        if (!view.hiddenColumns.isEmpty()) {
            xml += QByteArrayLiteral(" hiddenColumns=\"1\"");
        }
        if (view.sheetHidden) {
            xml += QByteArrayLiteral(" state=\"hidden\"");
        }
        xml += QByteArrayLiteral(" topLeftCell=\"") + addrText(view.topLeft).toUtf8() + QByteArrayLiteral("\">");
        // The pane the selection is in is the one that scrolls both ways
        // (bottomRight), or the one that scrolls at all.
        const bool frozen = view.freezeRows > 0 || view.freezeCols > 0;
        const bool across = frozen ? view.freezeCols > 0 : view.splitX > 0;
        const bool down = frozen ? view.freezeRows > 0 : view.splitY > 0;
        const QByteArray activePane = across && down ? "bottomRight" : down ? "bottomLeft" : "topRight";
        QByteArray pane;
        if (frozen) {
            // The scrolling pane's first cell, at or past the frozen lines.
            const CellAddr far{qMax(view.splitTopLeft.row, view.freezeRows), qMax(view.splitTopLeft.col, view.freezeCols)};
            pane = QByteArrayLiteral("<pane") + (across ? " xSplit=\"" + QByteArray::number(view.freezeCols) + '"' : QByteArray())
                + (down ? " ySplit=\"" + QByteArray::number(view.freezeRows) + '"' : QByteArray())
                + QByteArrayLiteral(" topLeftCell=\"") + addrText(far).toUtf8()
                + QByteArrayLiteral("\" activePane=\"") + activePane + QByteArrayLiteral("\" state=\"frozen\"/>");
        } else if (across || down) {
            pane = QByteArrayLiteral("<pane") + (across ? " xSplit=\"" + QByteArray::number(qRound(view.splitX)) + '"' : QByteArray())
                + (down ? " ySplit=\"" + QByteArray::number(qRound(view.splitY)) + '"' : QByteArray())
                + QByteArrayLiteral(" topLeftCell=\"") + addrText(view.splitTopLeft).toUtf8()
                + QByteArrayLiteral("\" activePane=\"") + activePane + QByteArrayLiteral("\" state=\"split\"/>");
        }
        xml += pane;
        const QByteArray active = addrText(view.activeCell).toUtf8();
        xml += QByteArrayLiteral("<selection") + (pane.isEmpty() ? QByteArray() : " pane=\"" + activePane + '"')
            + QByteArrayLiteral(" activeCell=\"") + active + QByteArrayLiteral("\" sqref=\"") + active + QByteArrayLiteral("\"/>");
        if (view.printSettings) {
            xml += viewPrintXml(view.pageSetup);
        }
        if (view.filter && view.autoFilter.range.isValid()) {
            AutoFilter filter = view.autoFilter;
            filter.active = true;
            xml += autoFilterXml(filter);
        }
        xml += QByteArrayLiteral("</customSheetView>");
    }
    return xml + QByteArrayLiteral("</customSheetViews>");
}

QList<PatchManifest::DefinedNameEntry> customViewNames(const Workbook &wb)
{
    QList<PatchManifest::DefinedNameEntry> out;
    for (qint32 s = 0; s < wb.sheetCount(); ++s) {
        const Sheet &sheet = wb.sheetAt(s);
        for (const Sheet::CustomView &view : sheet.customViews()) {
            const QString stem = nameStem(view.guid);
            if (view.printSettings && view.printArea.has_value()) {
                out.append({stem + QStringLiteral("PrintArea"), s, areaText(sheet.name(), *view.printArea), true, {}});
            }
            if (view.filter && view.autoFilter.range.isValid()) {
                out.append({stem + QStringLiteral("FilterData"), s, areaText(sheet.name(), view.autoFilter.range), true, {}});
            }
            if (view.printSettings && (view.printTitleRows.has_value() || view.printTitleCols.has_value())) {
                QStringList titles;
                if (view.printTitleCols.has_value()) {
                    titles.append(spanText(sheet.name(), view.printTitleCols->topLeft.col, view.printTitleCols->bottomRight.col, false));
                }
                if (view.printTitleRows.has_value()) {
                    titles.append(spanText(sheet.name(), view.printTitleRows->topLeft.row, view.printTitleRows->bottomRight.row, true));
                }
                out.append({stem + QStringLiteral("PrintTitles"), s, titles.join(QLatin1Char(',')), true, {}});
            }
            if (!view.hiddenColumns.isEmpty()) {
                out.append({stem + QStringLiteral("Cols"), s, linesText(sheet.name(), view.hiddenColumns, false), true, {}});
            }
            if (!view.hiddenRows.isEmpty()) {
                out.append({stem + QStringLiteral("Rows"), s, linesText(sheet.name(), view.hiddenRows, true), true, {}});
            }
        }
    }
    return out;
}

} // namespace QindaOffice::Calc::IO
