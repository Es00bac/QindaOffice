// SPDX-License-Identifier: GPL-3.0-or-later
#include "xlsx_write_xml.h"

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
        writer.writeTextElement(QStringLiteral("author"), author);
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
        writer.writeTextElement(QStringLiteral("t"), comment.text);
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

} // namespace QindaOffice::Calc::IO
