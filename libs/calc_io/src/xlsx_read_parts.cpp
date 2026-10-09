// SPDX-License-Identifier: GPL-3.0-or-later
// Direct package readers for the autofilter, cell comments, tables and the
// sheet view (moved out of xlsx_read_xml.cpp in M35 to keep files inside the
// line budget; the header is xlsx_read_xml.h). Validations are
// xlsx_read_validation.cpp's since M78 (D-087).

#include "xlsx_read_xml.h"

#include "xlsx_formula.h"
#include "xlsx_parts.h"

#include "workbook.h"

#include <QRegularExpression>
#include <QXmlStreamReader>

#include <xlsxzipreader_p.h>

namespace QindaOffice::Calc::IO {

AutoFilter readAutoFilter(const QByteArray &xml)
{
    AutoFilter out;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement
            || reader.name() != QLatin1String("autoFilter")) {
            continue;
        }
        const std::optional<CellRange> range =
            rangeFromText(reader.attributes().value(QLatin1String("ref")).toString());
        if (!range.has_value()) {
            continue;
        }
        out.range = *range;
        out.active = true;
        while (!reader.atEnd()) {
            const auto token = reader.readNext();
            if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("autoFilter")) {
                break;
            }
            if (token != QXmlStreamReader::StartElement
                || reader.name() != QLatin1String("filterColumn")) {
                continue;
            }
            // colId is relative to the filter range's first column.
            const qint32 col = out.range.topLeft.col
                + reader.attributes().value(QLatin1String("colId")).toInt();
            QStringList values;
            while (!reader.atEnd()) {
                const auto inner = reader.readNext();
                if (inner == QXmlStreamReader::EndElement
                    && reader.name() == QLatin1String("filterColumn")) {
                    break;
                }
                if (inner == QXmlStreamReader::StartElement && reader.name() == QLatin1String("filter")) {
                    values.append(reader.attributes().value(QLatin1String("val")).toString());
                }
            }
            if (!values.isEmpty()) {
                out.allowed.insert(col, values);
            }
        }
    }
    return out;
}

QHash<quint64, CellComment> readComments(QXlsx::ZipReader &zip, const QString &sheetPart)
{
    QHash<quint64, CellComment> out;
    const int slash = sheetPart.lastIndexOf(QLatin1Char('/'));
    const QString ownerDir = sheetPart.left(slash);
    const QString relsPath = ownerDir + QStringLiteral("/_rels") + sheetPart.mid(slash)
        + QStringLiteral(".rels");
    const QHash<QString, QPair<QString, QString>> rels = readRels(zip, relsPath);
    QString commentsPart;
    for (auto it = rels.constBegin(); it != rels.constEnd(); ++it) {
        if (it.value().first.endsWith(QLatin1String("/comments"))) {
            commentsPart = resolveTarget(ownerDir, it.value().second);
            break;
        }
    }
    if (commentsPart.isEmpty()) {
        return out;
    }
    const QByteArray xml = zip.fileData(commentsPart);
    if (xml.isEmpty()) {
        return out;
    }
    QStringList authors;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token == QXmlStreamReader::StartElement && reader.name() == QLatin1String("author")) {
            authors.append(reader.readElementText());
            continue;
        }
        if (token != QXmlStreamReader::StartElement || reader.name() != QLatin1String("comment")) {
            continue;
        }
        const auto attributes = reader.attributes();
        const std::optional<CellAddr> addr = addrFromText(attributes.value(QLatin1String("ref")).toString());
        if (!addr.has_value()) {
            reader.skipCurrentElement();
            continue;
        }
        CellComment comment;
        comment.author = authors.value(attributes.value(QLatin1String("authorId")).toInt());
        // The text is one or more rich runs; concatenate the plain text.
        while (!reader.atEnd()) {
            const auto inner = reader.readNext();
            if (inner == QXmlStreamReader::EndElement && reader.name() == QLatin1String("comment")) {
                break;
            }
            if (inner == QXmlStreamReader::StartElement && reader.name() == QLatin1String("t")) {
                comment.text += reader.readElementText();
            }
        }
        out.insert(addr->key(), comment);
    }
    return out;
}

// One table part: attributes, columns, style info. Table-scoped filter
// criteria are NOT read (the sheet's AutoFilter stays singular — D-036);
// since M69 (D-078) the filter buttons and the calculated column formulas
// are (the totals row is the cells' business: calc_io derives its part
// attributes from them on save).
TableSpec readTablePart(const QByteArray &xml)
{
    TableSpec table;
    table.autoFilter = false; // a part without <autoFilter> shows no buttons
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        const auto token = reader.readNext();
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView name = reader.name();
        if (name == QLatin1String("table")) {
            const auto attributes = reader.attributes();
            table.name = attributes.value(QLatin1String("displayName")).toString();
            if (table.name.isEmpty()) {
                table.name = attributes.value(QLatin1String("name")).toString();
            }
            table.range = rangeFromText(attributes.value(QLatin1String("ref")).toString())
                              .value_or(CellRange{});
            // headerRowCount defaults to 1 when absent; totals to 0.
            const QStringView header = attributes.value(QLatin1String("headerRowCount"));
            table.headerRow = header.isEmpty() || header != QLatin1String("0");
            table.totalsRow = attributes.value(QLatin1String("totalsRowCount")).toInt() > 0;
        } else if (name == QLatin1String("tableColumn")) {
            table.columnNames.append(reader.attributes().value(QLatin1String("name")).toString());
            table.calculatedColumns.append(QString());
        } else if (name == QLatin1String("calculatedColumnFormula") && !table.calculatedColumns.isEmpty()) {
            // Excel's spelling (Table1[[#This Row],[Qty]]) → the engine's.
            table.calculatedColumns.last() = formulaFromFile(reader.readElementText().trimmed(), table.name);
        } else if (name == QLatin1String("autoFilter")) {
            table.autoFilter = true;
        } else if (name == QLatin1String("tableStyleInfo")) {
            const auto attributes = reader.attributes();
            table.styleName = attributes.value(QLatin1String("name")).toString();
            table.rowStripes = attributes.value(QLatin1String("showRowStripes")) != QLatin1String("0");
            table.colStripes = attributes.value(QLatin1String("showColumnStripes")) == QLatin1String("1");
        }
    }
    return table;
}

QList<TableSpec> readTables(QXlsx::ZipReader &zip, const QString &sheetPart)
{
    QList<TableSpec> out;
    const int slash = sheetPart.lastIndexOf(QLatin1Char('/'));
    const QString ownerDir = sheetPart.left(slash);
    const QString relsPath = ownerDir + QStringLiteral("/_rels") + sheetPart.mid(slash)
        + QStringLiteral(".rels");
    const QHash<QString, QPair<QString, QString>> rels = readRels(zip, relsPath);
    for (auto it = rels.constBegin(); it != rels.constEnd(); ++it) {
        if (!it.value().first.endsWith(QLatin1String("/table"))) {
            continue;
        }
        const QString part = resolveTarget(ownerDir, it.value().second);
        const QByteArray xml = zip.fileData(part);
        if (xml.isEmpty()) {
            continue;
        }
        TableSpec table = readTablePart(xml);
        table.sourcePart = part; // query tables travel with it (M66)
        if (!table.name.isEmpty() && table.range.isValid()) {
            out.append(table);
        }
    }
    return out;
}

// Sheet view state: the frozen pane (state="frozen" only — a plain "split"
// is an unfrozen split view, not a freeze) and the selection cell. The page
// setup is xlsx_print.h's (M78, D-087).
void readSheetViews(const QByteArray &xml, Sheet *sheet)
{
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView name = reader.name();
        const auto attributes = reader.attributes();
        if (name == QLatin1String("pane")) {
            const QStringView state = attributes.value(QLatin1String("state"));
            if (state == QLatin1String("frozen") || state == QLatin1String("frozenSplit")) {
                sheet->setFreeze(attributes.value(QLatin1String("ySplit")).toInt(),
                                 attributes.value(QLatin1String("xSplit")).toInt());
            }
        } else if (name == QLatin1String("selection")) {
            const std::optional<CellAddr> addr =
                addrFromText(attributes.value(QLatin1String("activeCell")).toString());
            if (addr.has_value()) {
                sheet->setCursor(*addr);
            }
        }
    }
}

} // namespace QindaOffice::Calc::IO
