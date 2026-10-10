// SPDX-License-Identifier: GPL-3.0-or-later
// External data in a Visio package: DataRecordSets and their ADO rowsets
// (visio_data.h).
#include "visio_data.h"

#include "visio_read_p.h"
#include "visio_source.h"
#include "visio_xml.h"

#include "ooxml_namespaces.h"
#include "ooxml_package.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>

namespace QindaOffice::Diagram::Visio {

namespace {

const QString kSchemaNs = QStringLiteral("uuid:BDC6E3F0-6DA3-11d1-A2A3-00AA00C14882");
const QString kRowsetNs = QStringLiteral("urn:schemas-microsoft-com:rowset");
const QString kRowNs = QStringLiteral("#RowsetSchema");
constexpr char16_t kRowIdName[] = u"_Visio_RowID_";

QString escaped(const QString &text)
{
    QString out = Ooxml::xmlSafeText(text).toHtmlEscaped();
    out.replace(QLatin1Char('\n'), QStringLiteral("&#10;"));
    out.replace(QLatin1Char('\r'), QStringLiteral("&#13;"));
    out.replace(QLatin1Char('\t'), QStringLiteral("&#9;"));
    return out;
}

// The rows of an ADO persisted rowset, in the columns' order; `rowIds`
// gets each row's _Visio_RowID_.
QVector<QStringList> rowsetRows(const XmlElement &root, const QStringList &columns, QList<int> *rowIds)
{
    // Attribute name per column: a plain name stands for itself, others are
    // "cN" with the name in rs:name.
    QHash<QString, QString> attributeOf; // column label → attribute
    QString rowIdAttribute;
    if (const XmlElement *schema = root.child(kSchemaNs, u"Schema")) {
        if (const XmlElement *type = schema->child(kSchemaNs, u"ElementType")) {
            for (const XmlElement *a : type->elements(kSchemaNs, u"AttributeType")) {
                const QString attribute = a->attribute({}, u"name");
                const QString label = a->attribute(kRowsetNs, u"name", attribute);
                if (label == QStringView(kRowIdName)) {
                    rowIdAttribute = attribute;
                } else {
                    attributeOf.insert(label, attribute);
                }
            }
        }
    }
    QVector<QStringList> rows;
    const XmlElement *data = root.child(kRowsetNs, u"data");
    for (const XmlElement *row : data != nullptr ? data->elements(kRowNs, u"row") : QList<const XmlElement *>()) {
        QStringList cells;
        for (const QString &column : columns) {
            cells.append(row->attribute({}, attributeOf.value(column, column)));
        }
        rows.append(cells);
        rowIds->append(row->attribute({}, rowIdAttribute.isEmpty() ? QStringLiteral("c0") : rowIdAttribute).toInt());
    }
    return rows;
}

QByteArray rowsetBytes(const DataRecordset &r)
{
    QString out = QStringLiteral("<xml xmlns:s=\"%1\" xmlns:dt=\"uuid:C2F41010-65B3-11d1-A29F-00AA00C14882\" "
                                 "xmlns:rs=\"%2\" xmlns:z=\"%3\">\n\t<s:Schema id=\"RowsetSchema\">\n"
                                 "\t\t<s:ElementType name=\"row\" content=\"eltOnly\" rs:updatable=\"true\">\n")
                      .arg(kSchemaNs, kRowsetNs, kRowNs);
    const QString flags = QStringLiteral("rs:nullable=\"true\" rs:maydefer=\"true\" rs:write=\"true\"");
    out += QStringLiteral("\t\t\t<s:AttributeType name=\"c0\" rs:name=\"_Visio_RowID_\" rs:number=\"1\" %1>\n"
                          "\t\t\t\t<s:datatype dt:type=\"int\" dt:maxLength=\"4\" rs:precision=\"0\" rs:fixedlength=\"true\"/>\n"
                          "\t\t\t</s:AttributeType>\n")
               .arg(flags);
    for (int c = 0; c < r.columns.size(); ++c) {
        qsizetype longest = 255;
        for (const QStringList &row : r.rows) {
            longest = std::max(longest, row.value(c).size());
        }
        out += QStringLiteral("\t\t\t<s:AttributeType name=\"c%1\" rs:name=\"%2\" rs:number=\"%3\" %4>\n"
                              "\t\t\t\t<s:datatype dt:type=\"string\" dt:maxLength=\"%5\" rs:precision=\"0\"/>\n"
                              "\t\t\t</s:AttributeType>\n")
                   .arg(c + 1)
                   .arg(escaped(r.columns.at(c)))
                   .arg(c + 2)
                   .arg(flags)
                   .arg(longest);
    }
    out += QStringLiteral("\t\t\t<s:extends type=\"rs:rowbase\"/>\n\t\t</s:ElementType>\n\t</s:Schema>\n\t<rs:data>\n");
    for (int i = 0; i < r.rows.size(); ++i) {
        out += QStringLiteral("\t\t<z:row c0=\"%1\"").arg(i + 1);
        for (int c = 0; c < r.columns.size(); ++c) {
            const QString value = r.rows.at(i).value(c);
            if (!value.isEmpty()) {
                out += QStringLiteral(" c%1=\"%2\"").arg(c + 1).arg(escaped(value));
            }
        }
        out += QStringLiteral("/>\n");
    }
    out += QStringLiteral("\t</rs:data>\n</xml>\n");
    return out.toUtf8();
}

QString isoTime(const QDateTime &when)
{
    return when.isValid() ? when.toUTC().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss")) : QString();
}

} // namespace

QString linkedRowName(const QString &column)
{
    static const QRegularExpression other(QStringLiteral("[^A-Za-z0-9]+"));
    QString name = column;
    name.replace(other, QStringLiteral("_"));
    while (name.startsWith(QLatin1Char('_'))) {
        name.remove(0, 1);
    }
    while (name.endsWith(QLatin1Char('_'))) {
        name.chop(1);
    }
    return QStringLiteral("_VisDM_") + (name.isEmpty() ? QStringLiteral("Column") : name);
}

QStringList linkedColumns(const Drawing &drawing, const Shape &shape)
{
    const DataRecordset *r = shape.dataLink.linked() ? drawing.recordset(shape.dataLink.recordset) : nullptr;
    return r != nullptr ? r->columns : QStringList();
}

int readRecordsets(ReadContext &ctx, const QString &documentPart)
{
    const auto rel = ctx.package->firstOfType(documentPart, QString::fromUtf16(kRecordsetsRelType));
    if (!rel) {
        return 0;
    }
    const QString part = ctx.package->targetPart(documentPart, *rel);
    const XmlElementPtr root = ctx.package->xml(part);
    if (!root) {
        return 0;
    }
    ctx.source->recordsetsPart = part;
    QHash<QString, int> pageIndex; // Visio page ID → model page
    for (int i = 0; i < ctx.drawing->pages.size(); ++i) {
        pageIndex.insert(ctx.drawing->pages.at(i).extra.value(QLatin1String("vsdx")).toObject().value(QLatin1String("page")).toString(), i);
    }
    int count = 0;
    for (const XmlElement *set : children(*root, u"DataRecordSet")) {
        DataRecordset r;
        r.id = ctx.drawing->newId(QStringLiteral("r"));
        r.name = set->attribute({}, u"Name");
        r.refreshed = QDateTime::fromString(set->attribute({}, u"TimeRefreshed"), Qt::ISODate);
        r.refreshed.setTimeZone(QTimeZone::UTC);
        if (const XmlElement *columns = child(*set, u"DataColumns")) {
            for (const XmlElement *c : children(*columns, u"DataColumn")) {
                r.columns.append(c->attribute({}, u"Label", c->attribute({}, u"ColumnNameID")));
            }
        }
        if (const XmlElement *key = child(*set, u"PrimaryKey")) {
            r.keyColumn = int(r.columns.indexOf(key->attribute({}, u"ColumnNameID")));
        }
        QList<int> rowIds;
        const XmlElement *relElement = child(*set, u"Rel");
        if (const auto rowsRel = ctx.package->relationship(part, relElement != nullptr ? relElement->attribute(Ooxml::Ns::R, u"id") : QString())) {
            if (const XmlElementPtr rowset = ctx.package->xml(ctx.package->targetPart(part, *rowsRel))) {
                r.rows = rowsetRows(*rowset, r.columns, &rowIds);
            }
        }
        r.extra.insert(QStringLiteral("vsdx"), QJsonObject{{QStringLiteral("connection"), set->attribute({}, u"ConnectionID")},
                                                           {QStringLiteral("command"), set->attribute({}, u"Command")},
                                                           {QStringLiteral("options"), set->attribute({}, u"Options")}});
        // RowMap: which shape shows which row.
        for (const XmlElement *map : children(*set, u"RowMap")) {
            const qsizetype row = rowIds.indexOf(map->attribute({}, u"RowID").toInt());
            const auto page = pageIndex.constFind(map->attribute({}, u"PageID"));
            const auto item = ctx.source->items.constFind(itemKey(map->attribute({}, u"PageID"), map->attribute({}, u"ShapeID").toInt()));
            if (row < 0 || page == pageIndex.constEnd() || item == ctx.source->items.constEnd() || !item->shape) {
                continue;
            }
            if (Shape *shape = ctx.drawing->pages[*page].shape(item->shape->id)) {
                shape->dataLink = DataLink{r.id, r.rowKey(int(row))};
                ctx.source->items[item->key].shape->dataLink = shape->dataLink; // the snapshot reads the same
            }
        }
        ctx.drawing->recordsets.append(r);
        ++count;
    }
    ctx.source->recordsets = ctx.drawing->recordsets;
    ctx.source->links = linksOf(*ctx.drawing);
    return count;
}

QHash<QString, DataLink> linksOf(const Drawing &drawing)
{
    QHash<QString, DataLink> out;
    for (const Page &page : drawing.pages) {
        for (const Shape &shape : page.shapes) {
            if (shape.dataLink.linked()) {
                out.insert(shape.id, shape.dataLink);
            }
        }
    }
    return out;
}

bool recordsetsAsRead(const Drawing &drawing, const Source &source)
{
    return !source.recordsetsPart.isEmpty() && drawing.recordsets == source.recordsets && linksOf(drawing) == source.links;
}

void writeRecordsets(Ooxml::PackageWriter &writer, const QString &documentPart, const Drawing &drawing,
                     const QHash<QString, QString> &pageIds, const QHash<QString, QHash<QString, int>> &shapeIds)
{
    if (drawing.recordsets.isEmpty()) {
        return;
    }
    const QString part = QStringLiteral("visio/data/recordsets.xml");
    XmlElementPtr root = makeElement(QStringLiteral("DataRecordSets"));
    root->setAttribute({}, QStringLiteral("NextID"), QString::number(drawing.recordsets.size() + 1));
    root->setAttribute({}, QStringLiteral("ActiveRecordsetID"), QStringLiteral("1"));
    QString order;
    for (int i = 0; i < drawing.recordsets.size(); ++i) {
        order += QString::number(i + 1) + QLatin1Char(';');
    }
    root->setAttribute({}, QStringLiteral("DataWindowOrder"), order);
    root->setAttribute(QString::fromUtf16(Ooxml::Ns::XML), QStringLiteral("space"), QStringLiteral("preserve"));
    for (int i = 0; i < drawing.recordsets.size(); ++i) {
        const DataRecordset &r = drawing.recordsets.at(i);
        const QString rowsPart = QStringLiteral("visio/data/recordset%1.xml").arg(i + 1);
        writer.addPart(rowsPart, rowsetBytes(r), QString::fromUtf16(kRecordsetType));
        const QString rel = writer.addRelationship(part, QString::fromUtf16(kRecordsetRelType), rowsPart);
        XmlElement &set = appendElement(*root, QStringLiteral("DataRecordSet"));
        set.setAttribute({}, QStringLiteral("ID"), QString::number(i + 1));
        // A connection Visio made stays named (connections.xml travels).
        const QJsonObject vsdx = r.extra.value(QLatin1String("vsdx")).toObject();
        const QString connection = vsdx.value(QLatin1String("connection")).toString();
        if (!connection.isEmpty()) {
            set.setAttribute({}, QStringLiteral("ConnectionID"), connection);
            set.setAttribute({}, QStringLiteral("Command"), vsdx.value(QLatin1String("command")).toString());
        }
        if (!vsdx.value(QLatin1String("options")).toString().isEmpty()) {
            set.setAttribute({}, QStringLiteral("Options"), vsdx.value(QLatin1String("options")).toString());
        }
        if (r.refreshed.isValid()) {
            set.setAttribute({}, QStringLiteral("TimeRefreshed"), isoTime(r.refreshed));
        }
        set.setAttribute({}, QStringLiteral("Name"), r.name);
        set.setAttribute({}, QStringLiteral("NextRowID"), QString::number(r.rows.size() + 1));
        if (r.keyColumn < 0) {
            set.setAttribute({}, QStringLiteral("RowOrder"), QStringLiteral("1"));
        }
        set.setAttribute({}, QStringLiteral("Checksum"), QStringLiteral("0"));
        appendElement(set, QStringLiteral("Rel")).setAttribute(QString::fromUtf16(Ooxml::Ns::R), QStringLiteral("id"), rel);
        XmlElement &columns = appendElement(set, QStringLiteral("DataColumns"));
        for (int c = 0; c < r.columns.size(); ++c) {
            XmlElement &column = appendElement(columns, QStringLiteral("DataColumn"));
            column.setAttribute({}, QStringLiteral("ColumnNameID"), r.columns.at(c));
            column.setAttribute({}, QStringLiteral("Name"), linkedRowName(r.columns.at(c)));
            column.setAttribute({}, QStringLiteral("Label"), r.columns.at(c));
            column.setAttribute({}, QStringLiteral("LangID"), QStringLiteral("en-US"));
            column.setAttribute({}, QStringLiteral("Currency"), QStringLiteral("11"));
            column.setAttribute({}, QStringLiteral("DisplayWidth"), QStringLiteral("75"));
            column.setAttribute({}, QStringLiteral("DisplayOrder"), QString::number(c));
        }
        if (r.keyColumn >= 0) {
            appendElement(set, QStringLiteral("PrimaryKey")).setAttribute({}, QStringLiteral("ColumnNameID"), r.columns.at(r.keyColumn));
        }
        // RowMap ([MS-VSDX] DataRecordSet_Type): the linked shapes.
        for (const Page &page : drawing.pages) {
            for (const Shape &shape : page.shapes) {
                const int row = shape.dataLink.recordset == r.id ? r.rowOf(shape.dataLink.row) : -1;
                const int visioId = shapeIds.value(page.id).value(shape.id, 0);
                if (row < 0 || visioId <= 0 || !pageIds.contains(page.id)) {
                    continue;
                }
                XmlElement &map = appendElement(set, QStringLiteral("RowMap"));
                map.setAttribute({}, QStringLiteral("RowID"), QString::number(row + 1));
                map.setAttribute({}, QStringLiteral("PageID"), pageIds.value(page.id));
                map.setAttribute({}, QStringLiteral("ShapeID"), QString::number(visioId));
            }
        }
    }
    writer.addXmlPart(part, *root, QString::fromUtf16(kRecordsetsType));
    writer.addRelationship(documentPart, QString::fromUtf16(kRecordsetsRelType), part);
}

} // namespace QindaOffice::Diagram::Visio
