// SPDX-License-Identifier: GPL-3.0-or-later
// PivotTables and caches in the recovery payload (calc_recovery_pivots.h;
// M74, D-083). Every member has a key; absent keys read as the model's
// defaults, so a payload written before M74 restores with no pivots.

#include "calc_recovery_pivots.h"

#include <QJsonObject>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

QJsonValue valueToJson(const Value &value)
{
    if (const auto *n = std::get_if<double>(&value)) {
        return QJsonObject{{QStringLiteral("n"), *n}};
    }
    if (const auto *t = std::get_if<QString>(&value)) {
        return QJsonObject{{QStringLiteral("t"), *t}};
    }
    if (const auto *b = std::get_if<bool>(&value)) {
        return QJsonObject{{QStringLiteral("b"), *b}};
    }
    if (const auto *e = std::get_if<Err>(&value)) {
        return QJsonObject{{QStringLiteral("e"), errText(*e)}};
    }
    return QJsonObject{};
}

Value valueFromJson(const QJsonObject &o)
{
    if (o.contains(QStringLiteral("n"))) {
        return o.value(QStringLiteral("n")).toDouble();
    }
    if (o.contains(QStringLiteral("t"))) {
        return o.value(QStringLiteral("t")).toString();
    }
    if (o.contains(QStringLiteral("b"))) {
        return o.value(QStringLiteral("b")).toBool();
    }
    if (o.contains(QStringLiteral("e"))) {
        return errFromExport(o.value(QStringLiteral("e")).toString());
    }
    return {};
}

template <typename T>
QJsonArray ints(const T &list)
{
    QJsonArray out;
    for (const auto v : list) {
        out.append(qint64(v));
    }
    return out;
}

QList<qint32> intList(const QJsonValue &value)
{
    QList<qint32> out;
    for (const QJsonValue &v : value.toArray()) {
        out.append(v.toInt());
    }
    return out;
}

QJsonArray rangeToJson(const CellRange &r)
{
    return QJsonArray{r.topLeft.row, r.topLeft.col, r.bottomRight.row, r.bottomRight.col};
}

CellRange rangeFromJson(const QJsonValue &value)
{
    const QJsonArray a = value.toArray();
    return a.size() == 4 ? CellRange{{a.at(0).toInt(), a.at(1).toInt()}, {a.at(2).toInt(), a.at(3).toInt()}}
                         : CellRange{};
}

QString bytes(const QByteArray &data)
{
    return QString::fromLatin1(data.toBase64());
}

QByteArray fromBytes(const QJsonValue &value)
{
    return QByteArray::fromBase64(value.toString().toLatin1());
}

QJsonObject filterToJson(const PivotFilter &f)
{
    return {{QStringLiteral("kind"), int(f.kind)}, {QStringLiteral("op"), int(f.op)},
            {QStringLiteral("text1"), f.text1},    {QStringLiteral("text2"), f.text2},
            {QStringLiteral("value1"), f.value1},  {QStringLiteral("value2"), f.value2},
            {QStringLiteral("data"), f.dataField}, {QStringLiteral("top"), f.top},
            {QStringLiteral("topBy"), int(f.topBy)}};
}

PivotFilter filterFromJson(const QJsonObject &o)
{
    PivotFilter f;
    f.kind = PivotFilter::Kind(o.value(QStringLiteral("kind")).toInt());
    f.op = PivotFilter::Op(o.value(QStringLiteral("op")).toInt());
    f.text1 = o.value(QStringLiteral("text1")).toString();
    f.text2 = o.value(QStringLiteral("text2")).toString();
    f.value1 = o.value(QStringLiteral("value1")).toDouble();
    f.value2 = o.value(QStringLiteral("value2")).toDouble();
    f.dataField = o.value(QStringLiteral("data")).toInt();
    f.top = o.value(QStringLiteral("top")).toBool(true);
    f.topBy = PivotFilter::TopBy(o.value(QStringLiteral("topBy")).toInt());
    return f;
}

QJsonObject settingsToJson(const PivotFieldSettings &s)
{
    QJsonArray filters;
    for (const PivotFilter &f : s.filters) {
        filters.append(filterToJson(f));
    }
    QList<qint32> hidden = s.hidden.values();
    std::sort(hidden.begin(), hidden.end());
    return {{QStringLiteral("caption"), s.caption},
            {QStringLiteral("order"), ints(s.order)},
            {QStringLiteral("hidden"), ints(hidden)},
            {QStringLiteral("sort"), int(s.sort)},
            {QStringLiteral("sortData"), s.sortData},
            {QStringLiteral("subtotals"), s.subtotals},
            {QStringLiteral("custom"), ints([&s] {
                 QList<qint32> out;
                 for (const PivotFunction fn : s.customSubtotals) {
                     out.append(qint32(fn));
                 }
                 return out;
             }())},
            {QStringLiteral("subtotalTop"), s.subtotalTop},
            {QStringLiteral("blankRow"), s.insertBlankRow},
            {QStringLiteral("repeat"), s.repeatLabels},
            {QStringLiteral("showAll"), s.showAllItems},
            {QStringLiteral("multiple"), s.multipleSelection},
            {QStringLiteral("pageItem"), s.pageItem},
            {QStringLiteral("filters"), filters}};
}

PivotFieldSettings settingsFromJson(const QJsonObject &o)
{
    PivotFieldSettings s;
    s.caption = o.value(QStringLiteral("caption")).toString();
    s.order = intList(o.value(QStringLiteral("order")));
    for (const qint32 item : intList(o.value(QStringLiteral("hidden")))) {
        s.hidden.insert(item);
    }
    s.sort = PivotSort(o.value(QStringLiteral("sort")).toInt());
    s.sortData = o.value(QStringLiteral("sortData")).toInt(-1);
    s.subtotals = o.value(QStringLiteral("subtotals")).toBool(true);
    for (const qint32 fn : intList(o.value(QStringLiteral("custom")))) {
        s.customSubtotals.append(PivotFunction(fn));
    }
    s.subtotalTop = o.value(QStringLiteral("subtotalTop")).toBool(true);
    s.insertBlankRow = o.value(QStringLiteral("blankRow")).toBool(false);
    s.repeatLabels = o.value(QStringLiteral("repeat")).toBool(false);
    s.showAllItems = o.value(QStringLiteral("showAll")).toBool(false);
    s.multipleSelection = o.value(QStringLiteral("multiple")).toBool(false);
    s.pageItem = o.value(QStringLiteral("pageItem")).toInt(-1);
    for (const QJsonValue &f : o.value(QStringLiteral("filters")).toArray()) {
        s.filters.append(filterFromJson(f.toObject()));
    }
    return s;
}

QJsonObject optionsToJson(const PivotOptions &o)
{
    return {{QStringLiteral("form"), int(o.form)},
            {QStringLiteral("rowGrand"), o.rowGrandTotals},
            {QStringLiteral("colGrand"), o.colGrandTotals},
            {QStringLiteral("showMissing"), o.showMissing},
            {QStringLiteral("missing"), o.missingCaption},
            {QStringLiteral("headers"), o.showHeaders},
            {QStringLiteral("rowCaption"), o.rowHeaderCaption},
            {QStringLiteral("colCaption"), o.colHeaderCaption},
            {QStringLiteral("grandCaption"), o.grandTotalCaption},
            {QStringLiteral("dataCaption"), o.dataCaption},
            {QStringLiteral("style"), o.style.name},
            {QStringLiteral("styleFlags"), QJsonArray{o.style.rowHeaders, o.style.colHeaders, o.style.rowStripes,
                                                      o.style.colStripes, o.style.lastColumn}}};
}

PivotOptions optionsFromJson(const QJsonObject &o)
{
    PivotOptions out;
    out.form = PivotForm(o.value(QStringLiteral("form")).toInt());
    out.rowGrandTotals = o.value(QStringLiteral("rowGrand")).toBool(true);
    out.colGrandTotals = o.value(QStringLiteral("colGrand")).toBool(true);
    out.showMissing = o.value(QStringLiteral("showMissing")).toBool(true);
    out.missingCaption = o.value(QStringLiteral("missing")).toString();
    out.showHeaders = o.value(QStringLiteral("headers")).toBool(true);
    out.rowHeaderCaption = o.value(QStringLiteral("rowCaption")).toString();
    out.colHeaderCaption = o.value(QStringLiteral("colCaption")).toString();
    out.grandTotalCaption = o.value(QStringLiteral("grandCaption")).toString();
    out.dataCaption = o.value(QStringLiteral("dataCaption")).toString(QStringLiteral("Values"));
    out.style.name = o.value(QStringLiteral("style")).toString(QStringLiteral("PivotStyleLight16"));
    const QJsonArray flags = o.value(QStringLiteral("styleFlags")).toArray();
    if (flags.size() == 5) {
        out.style.rowHeaders = flags.at(0).toBool();
        out.style.colHeaders = flags.at(1).toBool();
        out.style.rowStripes = flags.at(2).toBool();
        out.style.colStripes = flags.at(3).toBool();
        out.style.lastColumn = flags.at(4).toBool();
    }
    return out;
}

} // namespace

QJsonArray pivotsToJson(const QList<PivotTable> &pivots)
{
    QJsonArray out;
    for (const PivotTable &p : pivots) {
        QJsonArray fields;
        for (const PivotFieldSettings &s : p.fields) {
            fields.append(settingsToJson(s));
        }
        QJsonArray data;
        for (const PivotDataField &d : p.dataFields) {
            data.append(QJsonObject{{QStringLiteral("name"), d.name},           {QStringLiteral("field"), d.field},
                                    {QStringLiteral("function"), int(d.function)}, {QStringLiteral("showAs"), int(d.showAs)},
                                    {QStringLiteral("baseField"), d.baseField},  {QStringLiteral("baseItem"), d.baseItem},
                                    {QStringLiteral("format"), d.numberFormat}});
        }
        out.append(QJsonObject{{QStringLiteral("name"), p.name},
                               {QStringLiteral("cacheId"), p.cacheId},
                               {QStringLiteral("location"), QJsonArray{p.location.row, p.location.col}},
                               {QStringLiteral("fields"), fields},
                               {QStringLiteral("rows"), ints(p.rowFields)},
                               {QStringLiteral("cols"), ints(p.colFields)},
                               {QStringLiteral("pages"), ints(p.pageFields)},
                               {QStringLiteral("data"), data},
                               {QStringLiteral("options"), optionsToJson(p.options)},
                               {QStringLiteral("modelled"), p.modelled},
                               {QStringLiteral("note"), p.note},
                               {QStringLiteral("extent"), rangeToJson(p.extent)},
                               {QStringLiteral("sourcePart"), p.sourcePart},
                               {QStringLiteral("fingerprint"), bytes(p.readFingerprint)},
                               {QStringLiteral("extensions"), bytes(p.extensions)},
                               {QStringLiteral("customFormats"), p.customFormats}});
    }
    return out;
}

QList<PivotTable> pivotsFromJson(const QJsonArray &array)
{
    QList<PivotTable> out;
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        PivotTable p;
        p.name = o.value(QStringLiteral("name")).toString();
        p.cacheId = o.value(QStringLiteral("cacheId")).toInt();
        const QJsonArray location = o.value(QStringLiteral("location")).toArray();
        p.location = {location.at(0).toInt(), location.at(1).toInt()};
        for (const QJsonValue &f : o.value(QStringLiteral("fields")).toArray()) {
            p.fields.append(settingsFromJson(f.toObject()));
        }
        p.rowFields = intList(o.value(QStringLiteral("rows")));
        p.colFields = intList(o.value(QStringLiteral("cols")));
        p.pageFields = intList(o.value(QStringLiteral("pages")));
        for (const QJsonValue &d : o.value(QStringLiteral("data")).toArray()) {
            const QJsonObject dj = d.toObject();
            PivotDataField field;
            field.name = dj.value(QStringLiteral("name")).toString();
            field.field = dj.value(QStringLiteral("field")).toInt();
            field.function = PivotFunction(dj.value(QStringLiteral("function")).toInt());
            field.showAs = PivotShowAs(dj.value(QStringLiteral("showAs")).toInt());
            field.baseField = dj.value(QStringLiteral("baseField")).toInt(-1);
            field.baseItem = dj.value(QStringLiteral("baseItem")).toInt(-1);
            field.numberFormat = dj.value(QStringLiteral("format")).toString();
            p.dataFields.append(field);
        }
        p.options = optionsFromJson(o.value(QStringLiteral("options")).toObject());
        p.modelled = o.value(QStringLiteral("modelled")).toBool(true);
        p.note = o.value(QStringLiteral("note")).toString();
        p.extent = rangeFromJson(o.value(QStringLiteral("extent")));
        p.sourcePart = o.value(QStringLiteral("sourcePart")).toString();
        p.readFingerprint = fromBytes(o.value(QStringLiteral("fingerprint")));
        p.extensions = fromBytes(o.value(QStringLiteral("extensions")));
        p.customFormats = o.value(QStringLiteral("customFormats")).toBool();
        out.append(p);
    }
    return out;
}

QJsonArray pivotCachesToJson(const QList<PivotCache> &caches)
{
    QJsonArray out;
    for (const PivotCache &c : caches) {
        QJsonArray fields;
        for (const PivotCacheField &f : c.fields) {
            QJsonArray items;
            for (const Value &item : f.items) {
                items.append(valueToJson(item));
            }
            const PivotGroup &g = f.group;
            fields.append(QJsonObject{
                {QStringLiteral("name"), f.name},
                {QStringLiteral("format"), f.numberFormat},
                {QStringLiteral("database"), f.database},
                {QStringLiteral("items"), items},
                {QStringLiteral("records"), ints(f.records)},
                {QStringLiteral("group"), QJsonObject{{QStringLiteral("by"), int(g.by)}, {QStringLiteral("base"), g.base},
                                                      {QStringLiteral("start"), g.start}, {QStringLiteral("end"), g.end},
                                                      {QStringLiteral("interval"), g.interval},
                                                      {QStringLiteral("autoStart"), g.autoStart},
                                                      {QStringLiteral("autoEnd"), g.autoEnd},
                                                      {QStringLiteral("discrete"), ints(g.discrete)},
                                                      {QStringLiteral("names"), QJsonArray::fromStringList(g.names)}}}});
        }
        out.append(QJsonObject{{QStringLiteral("id"), c.id},
                               {QStringLiteral("sheet"), c.source.sheet},
                               {QStringLiteral("range"), rangeToJson(c.source.range)},
                               {QStringLiteral("sourceName"), c.source.name},
                               {QStringLiteral("fields"), fields},
                               {QStringLiteral("records"), c.recordCount},
                               {QStringLiteral("refreshOnLoad"), c.refreshOnLoad},
                               {QStringLiteral("modelled"), c.modelled},
                               {QStringLiteral("sourcePart"), c.sourcePart},
                               {QStringLiteral("fingerprint"), bytes(c.readFingerprint)},
                               {QStringLiteral("extensions"), bytes(c.extensions)}});
    }
    return out;
}

QList<PivotCache> pivotCachesFromJson(const QJsonArray &array)
{
    QList<PivotCache> out;
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        PivotCache c;
        c.id = o.value(QStringLiteral("id")).toInt();
        c.source.sheet = o.value(QStringLiteral("sheet")).toString();
        c.source.range = rangeFromJson(o.value(QStringLiteral("range")));
        c.source.name = o.value(QStringLiteral("sourceName")).toString();
        for (const QJsonValue &fv : o.value(QStringLiteral("fields")).toArray()) {
            const QJsonObject fo = fv.toObject();
            PivotCacheField f;
            f.name = fo.value(QStringLiteral("name")).toString();
            f.numberFormat = fo.value(QStringLiteral("format")).toString();
            f.database = fo.value(QStringLiteral("database")).toBool(true);
            for (const QJsonValue &item : fo.value(QStringLiteral("items")).toArray()) {
                f.items.append(valueFromJson(item.toObject()));
            }
            const QList<qint32> records = intList(fo.value(QStringLiteral("records")));
            f.records = QVector<qint32>(records.begin(), records.end());
            const QJsonObject g = fo.value(QStringLiteral("group")).toObject();
            f.group.by = PivotGroupBy(g.value(QStringLiteral("by")).toInt());
            f.group.base = g.value(QStringLiteral("base")).toInt(-1);
            f.group.start = g.value(QStringLiteral("start")).toDouble();
            f.group.end = g.value(QStringLiteral("end")).toDouble();
            f.group.interval = g.value(QStringLiteral("interval")).toDouble(1);
            f.group.autoStart = g.value(QStringLiteral("autoStart")).toBool(true);
            f.group.autoEnd = g.value(QStringLiteral("autoEnd")).toBool(true);
            f.group.discrete = intList(g.value(QStringLiteral("discrete")));
            for (const QJsonValue &name : g.value(QStringLiteral("names")).toArray()) {
                f.group.names.append(name.toString());
            }
            c.fields.append(f);
        }
        c.recordCount = o.value(QStringLiteral("records")).toInt();
        c.refreshOnLoad = o.value(QStringLiteral("refreshOnLoad")).toBool(false);
        c.modelled = o.value(QStringLiteral("modelled")).toBool(true);
        c.sourcePart = o.value(QStringLiteral("sourcePart")).toString();
        c.readFingerprint = fromBytes(o.value(QStringLiteral("fingerprint")));
        c.extensions = fromBytes(o.value(QStringLiteral("extensions")));
        out.append(c);
    }
    return out;
}

} // namespace QindaOffice::Apps::QindaCalc
