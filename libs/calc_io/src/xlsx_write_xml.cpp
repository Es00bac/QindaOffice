// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_write_xml.h"

#include "xlsx_text.h"

#include "value.h"

#include <QXmlStreamWriter>

namespace QindaOffice::Calc::IO {

QByteArray commentsPartXml(const QHash<quint64, CellComment> &comments)
{
    // Authors deduplicated, comments in address order (deterministic output).
    QStringList authors;
    QList<std::pair<quint64, CellComment>> ordered;
    for (auto it = comments.constBegin(); it != comments.constEnd(); ++it) {
        if (!authors.contains(it.value().author)) {
            authors.append(it.value().author);
        }
        ordered.append({it.key(), it.value()});
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });

    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("comments"));
    writer.writeDefaultNamespace(
        QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main"));
    writer.writeStartElement(QStringLiteral("authors"));
    for (const QString &author : authors) {
        writer.writeTextElement(QStringLiteral("author"), encodeXString(author));
    }
    writer.writeEndElement(); // authors
    writer.writeStartElement(QStringLiteral("commentList"));
    for (const auto &[key, comment] : ordered) {
        const CellAddr addr{qint32(key & 0xffffffffu), qint32(key >> 32)};
        writer.writeStartElement(QStringLiteral("comment"));
        writer.writeAttribute(QStringLiteral("ref"), addrText(addr));
        writer.writeAttribute(QStringLiteral("authorId"),
                              QString::number(authors.indexOf(comment.author)));
        writer.writeStartElement(QStringLiteral("text"));
        writer.writeStartElement(QStringLiteral("r"));
        writer.writeTextElement(QStringLiteral("t"), encodeXString(comment.text));
        writer.writeEndElement(); // r
        writer.writeEndElement(); // text
        writer.writeEndElement(); // comment
    }
    writer.writeEndElement(); // commentList
    writer.writeEndElement(); // comments
    writer.writeEndDocument();
    return xml;
}

QByteArray autoFilterXml(const AutoFilter &filter)
{
    if (!filter.active || !filter.range.isValid()) {
        return {};
    }
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartElement(QStringLiteral("autoFilter"));
    writer.writeAttribute(QStringLiteral("ref"),
                          QStringLiteral("%1:%2").arg(addrText(filter.range.topLeft),
                                                      addrText(filter.range.bottomRight)));
    const QList<qint32> cols = filter.allowed.keys();
    for (const qint32 col : cols) {
        // colId is relative to the filter range's first column.
        writer.writeStartElement(QStringLiteral("filterColumn"));
        writer.writeAttribute(QStringLiteral("colId"),
                              QString::number(col - filter.range.topLeft.col));
        writer.writeStartElement(QStringLiteral("filters"));
        for (const QString &value : filter.allowed.value(col)) {
            writer.writeEmptyElement(QStringLiteral("filter"));
            writer.writeAttribute(QStringLiteral("val"), value);
        }
        writer.writeEndElement(); // filters
        writer.writeEndElement(); // filterColumn
    }
    writer.writeEndElement(); // autoFilter
    return xml;
}

QByteArray tablePartXml(const TableSpec &table, int tableId)
{
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("table"));
    writer.writeDefaultNamespace(
        QStringLiteral("http://schemas.openxmlformats.org/spreadsheetml/2006/main"));
    writer.writeAttribute(QStringLiteral("id"), QString::number(tableId));
    writer.writeAttribute(QStringLiteral("name"), table.name);
    writer.writeAttribute(QStringLiteral("displayName"), table.name);
    writer.writeAttribute(QStringLiteral("ref"),
                          QStringLiteral("%1:%2").arg(addrText(table.range.topLeft),
                                                      addrText(table.range.bottomRight)));
    writer.writeAttribute(QStringLiteral("headerRowCount"),
                          QString::number(table.headerRow ? 1 : 0));
    writer.writeAttribute(QStringLiteral("totalsRowCount"),
                          QString::number(table.totalsRow ? 1 : 0));
    writer.writeAttribute(QStringLiteral("totalsRowShown"),
                          QString::number(table.totalsRow ? 1 : 0));
    const int columns = table.range.bottomRight.col - table.range.topLeft.col + 1;
    writer.writeStartElement(QStringLiteral("tableColumns"));
    writer.writeAttribute(QStringLiteral("count"), QString::number(columns));
    for (int i = 0; i < columns; ++i) {
        writer.writeEmptyElement(QStringLiteral("tableColumn"));
        writer.writeAttribute(QStringLiteral("id"), QString::number(i + 1));
        writer.writeAttribute(QStringLiteral("name"),
                              table.columnNames.value(i, QStringLiteral("Column%1").arg(i + 1)));
    }
    writer.writeEndElement(); // tableColumns
    writer.writeStartElement(QStringLiteral("tableStyleInfo"));
    writer.writeAttribute(QStringLiteral("name"),
                          table.styleName.isEmpty() ? QStringLiteral("TableStyleMedium2")
                                                    : table.styleName);
    writer.writeAttribute(QStringLiteral("showFirstColumn"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("showLastColumn"), QStringLiteral("0"));
    writer.writeAttribute(QStringLiteral("showRowStripes"),
                          QString::number(table.rowStripes ? 1 : 0));
    writer.writeAttribute(QStringLiteral("showColumnStripes"),
                          QString::number(table.colStripes ? 1 : 0));
    writer.writeEndElement(); // tableStyleInfo
    writer.writeEndElement(); // table
    writer.writeEndDocument();
    return xml;
}

QByteArray commentsVmlXml(const QList<CellAddr> &cells, int number)
{
    // The shape Excel itself writes for a note: a hidden yellow text box
    // anchored up and to the right of its cell. The anchor is a hint (Excel
    // re-lays a note out when it is shown); Row/Column tie it to the cell.
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartElement(QStringLiteral("xml"));
    writer.writeAttribute(QStringLiteral("xmlns:v"), QStringLiteral("urn:schemas-microsoft-com:vml"));
    writer.writeAttribute(QStringLiteral("xmlns:o"), QStringLiteral("urn:schemas-microsoft-com:office:office"));
    writer.writeAttribute(QStringLiteral("xmlns:x"), QStringLiteral("urn:schemas-microsoft-com:office:excel"));
    writer.writeStartElement(QStringLiteral("o:shapelayout"));
    writer.writeAttribute(QStringLiteral("v:ext"), QStringLiteral("edit"));
    writer.writeEmptyElement(QStringLiteral("o:idmap"));
    writer.writeAttribute(QStringLiteral("v:ext"), QStringLiteral("edit"));
    QStringList blocks; // 1024 shape ids per block: "3" or "3,4" for 1500 notes
    for (int block = 0; block <= int(cells.size() / 1024); ++block) {
        blocks.append(QString::number(number + block));
    }
    writer.writeAttribute(QStringLiteral("data"), blocks.join(QLatin1Char(',')));
    writer.writeEndElement(); // o:shapelayout
    writer.writeStartElement(QStringLiteral("v:shapetype"));
    writer.writeAttribute(QStringLiteral("id"), QStringLiteral("_x0000_t202"));
    writer.writeAttribute(QStringLiteral("coordsize"), QStringLiteral("21600,21600"));
    writer.writeAttribute(QStringLiteral("o:spt"), QStringLiteral("202"));
    writer.writeAttribute(QStringLiteral("path"), QStringLiteral("m,l,21600r21600,l21600,xe"));
    writer.writeEmptyElement(QStringLiteral("v:stroke"));
    writer.writeAttribute(QStringLiteral("joinstyle"), QStringLiteral("miter"));
    writer.writeEmptyElement(QStringLiteral("v:path"));
    writer.writeAttribute(QStringLiteral("gradientshapeok"), QStringLiteral("t"));
    writer.writeAttribute(QStringLiteral("o:connecttype"), QStringLiteral("rect"));
    writer.writeEndElement(); // v:shapetype
    for (int i = 0; i < cells.size(); ++i) {
        const CellAddr &cell = cells.at(i);
        writer.writeStartElement(QStringLiteral("v:shape"));
        writer.writeAttribute(QStringLiteral("id"), QStringLiteral("_x0000_s%1").arg(number * 1024 + i + 1));
        writer.writeAttribute(QStringLiteral("type"), QStringLiteral("#_x0000_t202"));
        writer.writeAttribute(QStringLiteral("style"),
                              QStringLiteral("position:absolute;margin-left:59.25pt;margin-top:1.5pt;"
                                             "width:108pt;height:59.25pt;z-index:%1;visibility:hidden")
                                  .arg(i + 1));
        writer.writeAttribute(QStringLiteral("fillcolor"), QStringLiteral("#ffffe1"));
        writer.writeAttribute(QStringLiteral("o:insetmode"), QStringLiteral("auto"));
        writer.writeEmptyElement(QStringLiteral("v:fill"));
        writer.writeAttribute(QStringLiteral("color2"), QStringLiteral("#ffffe1"));
        writer.writeEmptyElement(QStringLiteral("v:shadow"));
        writer.writeAttribute(QStringLiteral("on"), QStringLiteral("t"));
        writer.writeAttribute(QStringLiteral("color"), QStringLiteral("black"));
        writer.writeAttribute(QStringLiteral("obscured"), QStringLiteral("t"));
        writer.writeEmptyElement(QStringLiteral("v:path"));
        writer.writeAttribute(QStringLiteral("o:connecttype"), QStringLiteral("none"));
        writer.writeStartElement(QStringLiteral("v:textbox"));
        writer.writeAttribute(QStringLiteral("style"), QStringLiteral("mso-direction-alt:auto"));
        writer.writeStartElement(QStringLiteral("div"));
        writer.writeAttribute(QStringLiteral("style"), QStringLiteral("text-align:left"));
        writer.writeCharacters(QString());
        writer.writeEndElement(); // div
        writer.writeEndElement(); // v:textbox
        writer.writeStartElement(QStringLiteral("x:ClientData"));
        writer.writeAttribute(QStringLiteral("ObjectType"), QStringLiteral("Note"));
        writer.writeEmptyElement(QStringLiteral("x:MoveWithCells"));
        writer.writeEmptyElement(QStringLiteral("x:SizeWithCells"));
        const int top = qMax(0, cell.row - 1);
        writer.writeTextElement(QStringLiteral("x:Anchor"), QStringLiteral("%1, 15, %2, 10, %3, 15, %4, 4")
                                                                .arg(cell.col + 1)
                                                                .arg(top)
                                                                .arg(cell.col + 3)
                                                                .arg(top + 4));
        writer.writeTextElement(QStringLiteral("x:AutoFill"), QStringLiteral("False"));
        writer.writeTextElement(QStringLiteral("x:Row"), QString::number(cell.row));
        writer.writeTextElement(QStringLiteral("x:Column"), QString::number(cell.col));
        writer.writeEndElement(); // x:ClientData
        writer.writeEndElement(); // v:shape
    }
    writer.writeEndElement(); // xml
    return xml;
}

} // namespace QindaOffice::Calc::IO
