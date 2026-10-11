// SPDX-License-Identifier: GPL-3.0-or-later
// A pivotCacheDefinition and its pivotCacheRecords into a PivotCache
// (xlsx_read_pivot.h; M74, D-083): the worksheet source, every cacheField
// with its shared items, number format and grouping (fieldGroup: rangePr,
// discretePr, groupItems), and the records — item indexes and the values
// Excel writes inline for fields without shared items.

#include "xlsx_read_pivot.h"
#include "pivot_calculated.h"

#include "xlsx_parts.h"
#include "xlsx_pivot_names.h"
#include "xlsx_text.h"

#include "number_format.h"
#include "ooxml_namespaces.h"
#include "ooxml_package.h"

#include <QObject>
#include <QXmlStreamReader>

namespace QindaOffice::Calc::IO {

namespace {

const QString &sml()
{
    static const QString ns = QString::fromUtf16(Ooxml::Ns::S);
    return ns;
}

bool flag(const Ooxml::XmlElement &e, QStringView name, bool fallback)
{
    if (!e.hasAttribute(QStringView(), name)) {
        return fallback;
    }
    const QString v = e.attr(name);
    return v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

// One shared item (s, n, b, e, d, m) as an engine value.
Value itemValue(QStringView element, const QString &v)
{
    if (element == u"n") {
        return v.toDouble();
    }
    if (element == u"s") {
        return decodeXString(v);
    }
    if (element == u"b") {
        return v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
    }
    if (element == u"e") {
        return errorFromFile(v);
    }
    if (element == u"d") {
        return PivotNames::serialFromIso(v).value_or(0.0);
    }
    return {}; // m: a blank
}

QString itemKey(const Value &value)
{
    if (const auto *n = std::get_if<double>(&value)) {
        return QStringLiteral("n") + QString::number(*n, 'g', 17);
    }
    if (const auto *t = std::get_if<QString>(&value)) {
        return QStringLiteral("s") + t->toCaseFolded();
    }
    if (const auto *b = std::get_if<bool>(&value)) {
        return *b ? QStringLiteral("b1") : QStringLiteral("b0");
    }
    if (const auto *e = std::get_if<Err>(&value)) {
        return QStringLiteral("e") + errText(*e);
    }
    return QStringLiteral("m");
}

PivotGroup readGroup(const Ooxml::XmlElement &group, qint32 self)
{
    PivotGroup out;
    out.base = group.hasAttribute(QStringView(), u"base") ? group.attr(u"base").toInt() : self;
    if (const Ooxml::XmlElement *range = group.child(sml(), u"rangePr")) {
        out.by = PivotNames::groupByFrom(range->attr(u"groupBy", QStringLiteral("range")));
        out.autoStart = flag(*range, u"autoStart", true);
        out.autoEnd = flag(*range, u"autoEnd", true);
        if (out.by == PivotGroupBy::Range) {
            out.start = range->attr(u"startNum").toDouble();
            out.end = range->attr(u"endNum").toDouble();
        } else {
            out.start = PivotNames::serialFromIso(range->attr(u"startDate")).value_or(0.0);
            out.end = PivotNames::serialFromIso(range->attr(u"endDate")).value_or(0.0);
        }
        out.interval = range->attr(u"groupInterval", QStringLiteral("1")).toDouble();
    } else if (const Ooxml::XmlElement *discrete = group.child(sml(), u"discretePr")) {
        out.by = PivotGroupBy::Discrete;
        for (const Ooxml::XmlElement *x : discrete->elements(sml(), u"x")) {
            out.discrete.append(x->attr(u"v", QStringLiteral("0")).toInt());
        }
    }
    if (const Ooxml::XmlElement *items = group.child(sml(), u"groupItems")) {
        for (const Ooxml::XmlElement *item : items->elements()) {
            const QString v = item->attr(u"v");
            if (item->name == u"d") {
                out.names.append(formatWithCode(PivotNames::serialFromIso(v).value_or(0.0), QStringLiteral("m/d/yyyy")).text);
            } else if (item->name == u"m") {
                out.names.append(QObject::tr("(blank)"));
            } else {
                out.names.append(decodeXString(v));
            }
        }
    }
    return out;
}

// The records, streamed: each <r> holds one value per database field.
bool readRecords(const QByteArray &xml, PivotCache *cache, const QList<qint32> &database)
{
    QList<QHash<QString, qint32>> index;
    for (const qint32 f : database) {
        QHash<QString, qint32> keys;
        const QList<Value> &items = cache->fields.at(f).items;
        for (qsizetype i = 0; i < items.size(); ++i) {
            keys.insert(itemKey(items.at(i)), qint32(i));
        }
        index.append(keys);
    }
    QXmlStreamReader reader(xml);
    qint32 records = 0;
    qsizetype column = -1;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType token = reader.readNext();
        if (token == QXmlStreamReader::EndElement && reader.name() == QLatin1String("r")) {
            if (column + 1 != database.size()) {
                return false; // a record without a value for every field
            }
            ++records;
            continue;
        }
        if (token != QXmlStreamReader::StartElement) {
            continue;
        }
        const QStringView name = reader.name();
        if (name == QLatin1String("r")) {
            column = -1;
            continue;
        }
        if (name == QLatin1String("extLst") || name == QLatin1String("pivotCacheRecords")) {
            if (name == QLatin1String("extLst")) {
                reader.skipCurrentElement();
            }
            continue;
        }
        if (++column >= database.size()) {
            return false;
        }
        PivotCacheField &field = cache->fields[database.at(column)];
        const QString v = reader.attributes().value(QLatin1String("v")).toString();
        if (name == QLatin1String("x")) {
            const qint32 item = v.isEmpty() ? 0 : v.toInt();
            if (item < 0 || item >= field.items.size()) {
                return false;
            }
            field.records.append(item);
        } else {
            const Value value = itemValue(name, v);
            const QString key = itemKey(value);
            auto found = index[column].constFind(key);
            if (found == index[column].constEnd()) {
                found = index[column].insert(key, qint32(field.items.size()));
                field.items.append(value);
            }
            field.records.append(found.value());
        }
        reader.skipCurrentElement();
    }
    cache->recordCount = records;
    return !reader.hasError();
}

} // namespace

QHash<int, QString> readPivotNumberFormats(const Ooxml::Package &package)
{
    QHash<int, QString> formats;
    const QString workbook = package.mainPart();
    for (const Ooxml::Relationship &rel : package.relationships(workbook)) {
        if (!rel.type.endsWith(QLatin1String("/styles"))) {
            continue;
        }
        const Ooxml::XmlElementPtr root = package.xml(package.targetPart(workbook, rel));
        const Ooxml::XmlElement *list = root ? root->child(sml(), u"numFmts") : nullptr;
        for (const Ooxml::XmlElement *format : list != nullptr ? list->elements(sml(), u"numFmt")
                                                               : QList<const Ooxml::XmlElement *>()) {
            formats.insert(format->attr(u"numFmtId").toInt(), format->attr(u"formatCode"));
        }
    }
    return formats;
}

QString pivotNumberFormat(int id, const QHash<int, QString> &numberFormats)
{
    const QString code = numberFormats.contains(id) ? numberFormats.value(id) : NumberFormat::builtinCode(id);
    return code.compare(QLatin1String("General"), Qt::CaseInsensitive) == 0 ? QString() : code;
}

PivotCache readPivotCachePart(const Ooxml::Package &package, const QString &part,
                              const QHash<int, QString> &numberFormats, QString *reason)
{
    PivotCache cache;
    cache.sourcePart = part;
    const auto refuse = [&cache, reason](const QString &why) {
        cache.modelled = false;
        if (reason != nullptr && reason->isEmpty()) {
            *reason = why;
        }
    };
    const Ooxml::XmlElementPtr root = package.xml(part);
    if (!root) {
        refuse(QObject::tr("has a cache QindaCalc cannot read"));
        return cache;
    }
    cache.refreshOnLoad = flag(*root, u"refreshOnLoad", false);
    if (const Ooxml::XmlElement *ext = root->child(sml(), u"extLst")) {
        cache.extensions = Ooxml::serializeFragment(*ext).toUtf8();
    }
    const Ooxml::XmlElement *source = root->child(sml(), u"cacheSource");
    const QString type = source != nullptr ? source->attr(u"type", QStringLiteral("worksheet")) : QString();
    const Ooxml::XmlElement *sheet = source != nullptr ? source->child(sml(), u"worksheetSource") : nullptr;
    if (type != QLatin1String("worksheet") || sheet == nullptr
        || sheet->hasAttribute(QString::fromUtf16(Ooxml::Ns::R), u"id")) {
        refuse(type == QLatin1String("external") ? QObject::tr("reads an OLAP or Data Model connection")
               : type == QLatin1String("consolidation") ? QObject::tr("consolidates several ranges")
                                                        : QObject::tr("reads another workbook"));
    } else {
        cache.source.sheet = sheet->attr(u"sheet");
        cache.source.name = sheet->attr(u"name");
        if (const auto range = rangeFromText(sheet->attr(u"ref"))) {
            cache.source.range = *range;
        }
    }
    for (const char16_t *olap : {u"cacheHierarchies", u"kpis", u"tupleCache", u"calculatedItems", u"calculatedMembers",
                                 u"dimensions", u"measureGroups", u"maps"}) {
        if (root->child(sml(), olap) != nullptr) {
            refuse(QObject::tr("is an OLAP or Data Model PivotTable"));
        }
    }
    QList<qint32> database;
    const Ooxml::XmlElement *fields = root->child(sml(), u"cacheFields");
    for (const Ooxml::XmlElement *element : fields != nullptr ? fields->elements(sml(), u"cacheField")
                                                              : QList<const Ooxml::XmlElement *>()) {
        PivotCacheField field;
        const qint32 self = qint32(cache.fields.size());
        field.name = decodeXString(element->attr(u"name"));
        field.numberFormat = pivotNumberFormat(element->attr(u"numFmtId", QStringLiteral("0")).toInt(), numberFormats);
        field.database = flag(*element, u"databaseField", true);
        if (element->hasAttribute(QStringView(), u"formula")) {
            field.formula = decodeXString(element->attr(u"formula"));
            field.database = false;
        }
        if (const Ooxml::XmlElement *items = element->child(sml(), u"sharedItems")) {
            for (const Ooxml::XmlElement *item : items->elements()) {
                field.items.append(itemValue(item->name, item->attr(u"v")));
            }
        }
        if (const Ooxml::XmlElement *group = element->child(sml(), u"fieldGroup")) {
            const PivotGroup read = readGroup(*group, self);
            if (read.by != PivotGroupBy::None) {
                field.group = read;
            }
        }
        if (field.database) {
            database.append(self);
        } else if (!field.grouped() && field.formula.isEmpty()) {
            refuse(QObject::tr("has a field QindaCalc cannot read"));
        }
        cache.fields.append(field);
    }
    for (qsizetype f = 0; f < cache.fields.size(); ++f) {
        if (!cache.fields.at(f).formula.isEmpty() && !pivotCalculatedProblem(cache, qint32(f)).isEmpty())
            refuse(QObject::tr("has unsupported calculated fields"));
    }
    cache.recordCount = root->attr(u"recordCount").toInt();
    QString records;
    for (const Ooxml::Relationship &rel : package.relationships(part)) {
        if (rel.type.endsWith(QLatin1String("/pivotCacheRecords"))) {
            records = package.targetPart(part, rel);
        }
    }
    if (cache.modelled) {
        if (records.isEmpty() || !package.contains(records)) {
            // Excel's "Save source data with file" off: nothing to show until
            // a refresh, which refreshOnLoad asks for.
            if (!cache.refreshOnLoad) {
                refuse(QObject::tr("was saved without its data"));
            }
            for (PivotCacheField &field : cache.fields) {
                field.records = QVector<qint32>(cache.recordCount, 0);
            }
        } else if (!readRecords(package.data(records), &cache, database)) {
            refuse(QObject::tr("has records QindaCalc cannot read"));
        }
    }
    return cache;
}

} // namespace QindaOffice::Calc::IO
