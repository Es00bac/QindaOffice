// SPDX-License-Identifier: GPL-3.0-or-later
// Access's XML interchange: the data file and its annotated schema. See
// base_xml_export.h for the contract.
#include "base_xml_export.h"

#include "base_hyperlink.h"
#include "base_indexes.h"
#include "base_schema.h"

#include <QRegularExpression>
#include <QXmlStreamWriter>

#include <cmath>
#include <limits>

namespace QindaOffice::Base {

namespace {

QString s(const char *text)
{
    return QString::fromUtf8(text);
}

const QString kXsd = s("http://www.w3.org/2001/XMLSchema");
const QString kOd = s("urn:schemas-microsoft-com:officedata");
const QString kQb = QString::fromLatin1(kQindaBaseXmlNamespace);

bool nameStart(QChar c)
{
    return c.isLetter() || c == QLatin1Char('_');
}

bool nameChar(QChar c)
{
    return nameStart(c) || c.isDigit() || c == QLatin1Char('-') || c == QLatin1Char('.');
}

struct JetType {
    QString jet;
    QString sql;
    QString xsd;      // the element's type, or "" for a restricted simpleType
    QString base;     // the restriction's base
    qint64 maxLength = 0;
};

// Whether every value of `column` fits Access's 32-bit Long Integer.
bool fitsLong(const QList<QVariantList> &rows, int column)
{
    for (const QVariantList &row : rows) {
        const QVariant v = row.value(column);
        if (v.isValid() && !v.isNull()) {
            const qint64 n = v.toLongLong();
            if (n < std::numeric_limits<qint32>::min() || n > std::numeric_limits<qint32>::max()) {
                return false;
            }
        }
    }
    return true;
}

JetType jetType(const ColumnInfo &c, bool fitsInt32)
{
    switch (c.type) {
    case FieldType::AutoNumber: return {s("autonumber"), s("int"), s("xsd:int"), {}, 0};
    case FieldType::Text: return {s("text"), s("nvarchar"), {}, s("xsd:string"), kShortTextLimit};
    case FieldType::LongText: return {s("memo"), s("ntext"), {}, s("xsd:string"), 536870910};
    case FieldType::Integer:
        return fitsInt32 ? JetType{s("longinteger"), s("int"), s("xsd:int"), {}, 0}
                         : JetType{s("double"), s("float"), s("xsd:double"), {}, 0};
    case FieldType::Number: return {s("double"), s("float"), s("xsd:double"), {}, 0};
    case FieldType::Currency: return {s("currency"), s("money"), s("xsd:double"), {}, 0};
    case FieldType::YesNo: return {s("yesno"), s("bit"), s("xsd:boolean"), {}, 0};
    case FieldType::Date:
    case FieldType::DateTime: return {s("datetime"), s("datetime"), s("xsd:dateTime"), {}, 0};
    case FieldType::File: return {s("oleobject"), s("image"), {}, s("xsd:base64Binary"), 1476395008};
    case FieldType::Hyperlink: return {s("hyperlink"), s("ntext"), {}, s("xsd:string"), 536870910};
    case FieldType::LargeNumber: return {s("bigint"), s("bigint"), s("xsd:long"), {}, 0};
    case FieldType::Other: break;
    }
    return {s("memo"), s("ntext"), {}, s("xsd:string"), 536870910};
}

// A storage value as Access writes it in the data file ("" = left out).
QString xmlValue(const ColumnInfo &c, const QVariant &v, const CurrencyFormat &currency)
{
    if (!v.isValid() || v.isNull()) {
        return {};
    }
    switch (c.type) {
    case FieldType::AutoNumber:
    case FieldType::Integer:
    case FieldType::LargeNumber: return QString::number(v.toLongLong());
    case FieldType::Hyperlink: return parseHyperlink(v.toString()).accessText();
    case FieldType::Number: {
        const double d = v.toDouble();
        return std::isfinite(d) ? QString::number(d, 'g', 17) : QString();
    }
    case FieldType::Currency:
        return v.metaType().id() == QMetaType::Double ? QString::number(v.toDouble(), 'f', currency.decimals)
                                                      : formatMoney(v.toLongLong(), currency.decimals, QString(), false);
    case FieldType::YesNo: return v.toLongLong() != 0 ? s("1") : s("0");
    case FieldType::Date: return v.toString().left(10) + s("T00:00:00");
    case FieldType::DateTime: return v.toString().replace(QLatin1Char(' '), QLatin1Char('T'));
    case FieldType::File: return QString::fromLatin1(v.toByteArray().toBase64());
    default: return v.toString();
    }
}

void writeIndex(QXmlStreamWriter &w, const QString &name, const QStringList &fields, bool primary, bool unique)
{
    w.writeEmptyElement(kOd, s("index"));
    w.writeAttribute(s("index-name"), name);
    w.writeAttribute(s("index-key"), fields.join(QLatin1Char(' ')) + QLatin1Char(' '));
    w.writeAttribute(s("primary"), primary ? s("yes") : s("no"));
    w.writeAttribute(s("unique"), unique ? s("yes") : s("no"));
    w.writeAttribute(s("clustered"), s("no"));
    w.writeAttribute(s("order"), s("asc"));
}

} // namespace

QString nameFromXml(const QString &element)
{
    static const QRegularExpression escape(s("_x([0-9A-Fa-f]{4})_"));
    QString out;
    qsizetype last = 0;
    for (QRegularExpressionMatchIterator it = escape.globalMatch(element); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += element.mid(last, m.capturedStart() - last);
        out += QChar(char16_t(m.captured(1).toUShort(nullptr, 16)));
        last = m.capturedEnd();
    }
    return out + element.mid(last);
}

QString xmlName(const QString &name)
{
    QString out;
    for (int i = 0; i < name.size(); ++i) {
        const QChar c = name.at(i);
        if (i == 0 ? nameStart(c) : nameChar(c)) {
            out += c;
        } else {
            out += s("_x") + QString::number(c.unicode(), 16).toUpper().rightJustified(4, QLatin1Char('0')) + s("_");
        }
    }
    return out.isEmpty() ? s("_x0020_") : out;
}

AccessXml accessXml(const QString &name, const QList<ColumnInfo> &columns, const QList<QVariantList> &rows,
                    const CurrencyFormat &currency, const TableDef *design, const QString &schemaFile,
                    const QDateTime &generated)
{
    AccessXml out;
    const QString element = xmlName(name);
    const QString stamp = generated.toString(s("yyyy-MM-ddTHH:mm:ss"));
    {
        QXmlStreamWriter w(&out.data);
        w.setAutoFormatting(true);
        w.writeStartDocument();
        w.writeNamespace(kOd, s("od"));
        w.writeNamespace(s("http://www.w3.org/2001/XMLSchema-instance"), s("xsi"));
        w.writeStartElement(s("dataroot"));
        w.writeAttribute(s("http://www.w3.org/2001/XMLSchema-instance"), s("noNamespaceSchemaLocation"), schemaFile);
        w.writeAttribute(s("generated"), stamp);
        for (const QVariantList &row : rows) {
            w.writeStartElement(element);
            for (int i = 0; i < columns.size(); ++i) {
                const QString value = xmlValue(columns.at(i), row.value(i), currency);
                if (!value.isEmpty() || (columns.at(i).type == FieldType::Text && row.value(i).isValid()
                                         && !row.value(i).isNull())) {
                    w.writeTextElement(xmlName(columns.at(i).name), value);
                }
            }
            w.writeEndElement();
        }
        w.writeEndElement();
        w.writeEndDocument();
    }
    QXmlStreamWriter w(&out.schema);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeNamespace(kXsd, s("xsd"));
    w.writeNamespace(kOd, s("od"));
    w.writeNamespace(kQb, s("qb"));
    w.writeStartElement(kXsd, s("schema"));
    w.writeStartElement(kXsd, s("element"));
    w.writeAttribute(s("name"), s("dataroot"));
    w.writeStartElement(kXsd, s("complexType"));
    w.writeStartElement(kXsd, s("sequence"));
    w.writeEmptyElement(kXsd, s("element"));
    w.writeAttribute(s("ref"), element);
    w.writeAttribute(s("minOccurs"), s("0"));
    w.writeAttribute(s("maxOccurs"), s("unbounded"));
    w.writeEndElement(); // sequence
    w.writeEmptyElement(kXsd, s("attribute"));
    w.writeAttribute(s("name"), s("generated"));
    w.writeAttribute(s("type"), s("xsd:dateTime"));
    w.writeEndElement(); // complexType
    w.writeEndElement(); // element dataroot
    w.writeStartElement(kXsd, s("element"));
    w.writeAttribute(s("name"), element);
    if (design != nullptr) {
        QStringList key;
        for (const FieldDef &f : design->fields) {
            if (f.primaryKey) {
                key.append(xmlName(f.name));
            }
        }
        w.writeStartElement(kXsd, s("annotation"));
        w.writeStartElement(kXsd, s("appinfo"));
        if (!key.isEmpty()) {
            writeIndex(w, s("PrimaryKey"), key, true, true);
        }
        for (const FieldDef &f : design->fields) {
            if (!f.primaryKey && (f.unique || f.indexed)) {
                writeIndex(w, f.name, {xmlName(f.name)}, false, f.unique);
            }
        }
        for (const IndexDef &index : design->indexes) {
            QStringList fields;
            for (const IndexField &f : index.fields) {
                fields.append(xmlName(f.field));
            }
            writeIndex(w, index.name, fields, false, index.unique);
        }
        w.writeEndElement(); // appinfo
        w.writeEndElement(); // annotation
    }
    w.writeStartElement(kXsd, s("complexType"));
    w.writeStartElement(kXsd, s("sequence"));
    for (int i = 0; i < columns.size(); ++i) {
        const ColumnInfo &c = columns.at(i);
        const JetType t = jetType(c, fitsLong(rows, i));
        const bool required = c.required || c.type == FieldType::AutoNumber;
        const bool simple = t.xsd.isEmpty();
        if (simple) {
            w.writeStartElement(kXsd, s("element"));
        } else {
            w.writeEmptyElement(kXsd, s("element"));
        }
        w.writeAttribute(s("name"), xmlName(c.name));
        w.writeAttribute(s("minOccurs"), required ? s("1") : s("0"));
        w.writeAttribute(kOd, s("jetType"), t.jet);
        w.writeAttribute(kOd, s("sqlSType"), t.sql);
        const FieldDef *field = design != nullptr ? design->field(c.name) : nullptr;
        w.writeAttribute(kQb, s("type"), typeId(c.type));
        if (field != nullptr && field->calculated && !field->expression.isEmpty()) {
            w.writeAttribute(kQb, s("expression"), field->expression);
        }
        if (c.type == FieldType::AutoNumber) {
            w.writeAttribute(kOd, s("autoUnique"), s("yes"));
        }
        if (required) {
            w.writeAttribute(kOd, s("nonNullable"), s("yes"));
        }
        if (!simple) {
            w.writeAttribute(s("type"), t.xsd);
            continue;
        }
        w.writeStartElement(kXsd, s("simpleType"));
        w.writeStartElement(kXsd, s("restriction"));
        w.writeAttribute(s("base"), t.base);
        w.writeEmptyElement(kXsd, s("maxLength"));
        w.writeAttribute(s("value"), QString::number(t.maxLength));
        w.writeEndElement(); // restriction
        w.writeEndElement(); // simpleType
        w.writeEndElement(); // element
    }
    w.writeEndElement(); // sequence
    w.writeEndElement(); // complexType
    w.writeEndElement(); // element
    w.writeEndElement(); // schema
    w.writeEndDocument();
    return out;
}

} // namespace QindaOffice::Base
