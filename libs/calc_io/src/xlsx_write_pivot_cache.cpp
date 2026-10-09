// SPDX-License-Identifier: GPL-3.0-or-later
// A PivotCache as Excel's pivotCacheDefinition and pivotCacheRecords
// (xlsx_write_pivot.h; M74, D-083).
//
// AGENT-NOTE: Excel's conventions, kept so its own reader takes the cache
// as one of its own: shared items are listed for every field a pivot puts
// on an axis and for every field with text (records then hold <x v>);
// other number and date fields keep their values inline in the records
// (<n>, <d>) and say only their range (minValue / minDate …). Dates are
// <d v="yyyy-mm-ddThh:mm:ss"> in a field whose number format is a date.
// A grouped field writes fieldGroup (rangePr / discretePr + groupItems);
// the fields grouping made follow the source columns with
// databaseField="0", chained by par from finer to coarser.

#include "xlsx_write_pivot.h"

#include "xlsx_pivot_names.h"
#include "xlsx_text.h"
#include "xlsx_xml.h"

#include "pivot_cache.h"

#include <cmath>

namespace QindaOffice::Calc::IO {

namespace {

const QByteArray kRoots = QByteArrayLiteral("xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                                            "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\"");

QByteArray attr(const char *name, const QString &value)
{
    return QByteArray(" ") + name + "=\"" + xmlAttribute(encodeXString(value)) + '"';
}

QByteArray number(double value)
{
    QString text = QString::number(value, 'g', 15);
    if (text.toDouble() != value) {
        text = QString::number(value, 'g', 17);
    }
    return text.toUtf8();
}

bool dateFormat(const QString &code)
{
    bool quoted = false;
    for (const QChar c : code) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (!quoted && (c.toLower() == QLatin1Char('d') || c.toLower() == QLatin1Char('y'))) {
            return true;
        }
    }
    return false;
}

QByteArray itemXml(const Value &value, bool date)
{
    if (const auto *n = std::get_if<double>(&value)) {
        return date ? QByteArrayLiteral("<d v=\"") + PivotNames::isoDate(*n).toUtf8() + "\"/>"
                    : QByteArrayLiteral("<n v=\"") + number(*n) + "\"/>";
    }
    if (const auto *t = std::get_if<QString>(&value)) {
        return QByteArrayLiteral("<s") + attr("v", *t) + "/>";
    }
    if (const auto *b = std::get_if<bool>(&value)) {
        return *b ? QByteArrayLiteral("<b v=\"1\"/>") : QByteArrayLiteral("<b v=\"0\"/>");
    }
    if (const auto *e = std::get_if<Err>(&value)) {
        return QByteArrayLiteral("<e") + attr("v", errorForFile(*e).isEmpty() ? QStringLiteral("#N/A") : errorForFile(*e))
            + "/>";
    }
    return QByteArrayLiteral("<m/>");
}

// <sharedItems …> with the attributes Excel derives from the values.
QByteArray sharedItemsXml(const PivotCacheField &field, bool listed)
{
    const bool dates = dateFormat(field.numberFormat);
    bool text = false;
    bool numbers = false;
    bool dated = false;
    bool blank = false;
    bool other = false;
    bool integers = true;
    bool longText = false;
    double lo = 0;
    double hi = 0;
    bool first = true;
    for (const Value &item : field.items) {
        if (const auto *n = std::get_if<double>(&item)) {
            (dates ? dated : numbers) = true;
            integers = integers && std::floor(*n) == *n;
            lo = first ? *n : qMin(lo, *n);
            hi = first ? *n : qMax(hi, *n);
            first = false;
        } else if (const auto *t = std::get_if<QString>(&item)) {
            text = true;
            longText = longText || t->size() > 255;
        } else if (isBlank(item)) {
            blank = true;
        } else {
            other = true;
        }
    }
    QByteArray xml = QByteArrayLiteral("<sharedItems");
    if (!text && !blank && !other) {
        xml += " containsSemiMixedTypes=\"0\"";
    }
    if (dated && !numbers && !text && !other) {
        xml += " containsNonDate=\"0\"";
    }
    if (dated) {
        xml += " containsDate=\"1\"";
    }
    if (!text) {
        xml += " containsString=\"0\"";
    }
    if (int(text) + int(numbers || dated) + int(other) > 1) {
        xml += " containsMixedTypes=\"1\"";
    }
    if (numbers) {
        xml += " containsNumber=\"1\"";
        if (integers) {
            xml += " containsInteger=\"1\"";
        }
        xml += " minValue=\"" + number(lo) + "\" maxValue=\"" + number(hi) + '"';
    } else if (dated) {
        xml += " minDate=\"" + PivotNames::isoDate(lo).toUtf8() + "\" maxDate=\"" + PivotNames::isoDate(hi).toUtf8() + '"';
    }
    if (blank) {
        xml += " containsBlank=\"1\"";
    }
    if (longText) {
        xml += " longText=\"1\"";
    }
    if (!listed) {
        return xml + "/>";
    }
    xml += " count=\"" + QByteArray::number(field.items.size()) + "\">";
    for (const Value &item : field.items) {
        xml += itemXml(item, dates);
    }
    return xml + "</sharedItems>";
}

int granularity(PivotGroupBy by)
{
    return int(by);
}

// Excel's par: each grouping of a field's dates names the next coarser one;
// the base of a manual grouping names the field that groups it.
QHash<qint32, qint32> parents(const PivotCache &cache)
{
    QHash<qint32, qint32> par;
    QHash<qint32, QList<qint32>> chains; // base → fields grouping its values by date part
    for (qsizetype f = 0; f < cache.fields.size(); ++f) {
        const PivotGroup &g = cache.fields.at(f).group;
        if (g.by == PivotGroupBy::Discrete) {
            if (g.base >= 0 && g.base < cache.fields.size() && !cache.fields.at(g.base).grouped()) {
                par.insert(g.base, qint32(f));
            }
        } else if (g.by != PivotGroupBy::None && g.by != PivotGroupBy::Range) {
            chains[g.base >= 0 ? g.base : qint32(f)].append(qint32(f));
        }
    }
    for (QList<qint32> &chain : chains) {
        std::sort(chain.begin(), chain.end(), [&cache](qint32 a, qint32 b) {
            return granularity(cache.fields.at(a).group.by) < granularity(cache.fields.at(b).group.by);
        });
        for (qsizetype i = 0; i + 1 < chain.size(); ++i) {
            par.insert(chain.at(i), chain.at(i + 1));
        }
    }
    return par;
}

QByteArray fieldGroupXml(const PivotCache &cache, qint32 f, const QHash<qint32, qint32> &par)
{
    const PivotCacheField &field = cache.fields.at(f);
    const PivotGroup &g = field.group;
    QByteArray xml = QByteArrayLiteral("<fieldGroup");
    if (par.contains(f)) {
        xml += " par=\"" + QByteArray::number(par.value(f)) + '"';
    }
    if (!field.grouped()) {
        return xml + "/>"; // the base of a manual grouping
    }
    xml += " base=\"" + QByteArray::number(g.base >= 0 ? g.base : f) + "\">";
    if (g.by == PivotGroupBy::Discrete) {
        xml += "<discretePr count=\"" + QByteArray::number(g.discrete.size()) + "\">";
        for (const qint32 x : g.discrete) {
            xml += "<x v=\"" + QByteArray::number(x) + "\"/>";
        }
        xml += "</discretePr>";
    } else {
        xml += "<rangePr";
        if (g.by != PivotGroupBy::Range) {
            xml += attr("groupBy", PivotNames::groupBy(g.by));
        }
        xml += g.autoStart ? QByteArray() : QByteArrayLiteral(" autoStart=\"0\"");
        xml += g.autoEnd ? QByteArray() : QByteArrayLiteral(" autoEnd=\"0\"");
        if (g.by == PivotGroupBy::Range) {
            xml += " startNum=\"" + number(g.start) + "\" endNum=\"" + number(g.end) + '"';
        } else {
            xml += " startDate=\"" + PivotNames::isoDate(g.start).toUtf8() + "\" endDate=\""
                + PivotNames::isoDate(g.end).toUtf8() + '"';
        }
        if (g.interval != 1 || g.by == PivotGroupBy::Range) {
            xml += " groupInterval=\"" + number(g.interval) + '"';
        }
        xml += "/>";
    }
    xml += "<groupItems count=\"" + QByteArray::number(g.names.size()) + "\">";
    for (const QString &name : g.names) {
        xml += QByteArrayLiteral("<s") + attr("v", name) + "/>";
    }
    return xml + "</groupItems></fieldGroup>";
}

} // namespace

QSet<qint32> pivotListedFields(const PivotCache &cache, const QList<const PivotTable *> &pivots)
{
    QSet<qint32> listed;
    for (const PivotTable *pivot : pivots) {
        for (const qint32 f : pivot->rowFields + pivot->colFields + pivot->pageFields) {
            listed.insert(f);
        }
    }
    for (qsizetype f = 0; f < cache.fields.size(); ++f) {
        const PivotCacheField &field = cache.fields.at(f);
        for (const Value &item : field.items) {
            if (!isNumber(item)) {
                listed.insert(qint32(f)); // text, booleans, errors, blanks: always listed
                break;
            }
        }
        if (field.grouped()) {
            listed.insert(field.group.base >= 0 ? field.group.base : qint32(f));
            listed.insert(qint32(f));
        }
    }
    return listed;
}

QByteArray pivotCacheDefinitionXml(const PivotCache &cache, const QSet<qint32> &listed, const PivotFormatId &formatId,
                                   const QString &recordsId)
{
    QByteArray xml = QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                       "<pivotCacheDefinition ")
        + kRoots + attr("r:id", recordsId)
        + " refreshedBy=\"QindaCalc\" createdVersion=\"6\" refreshedVersion=\"6\" minRefreshableVersion=\"3\""
        + (cache.refreshOnLoad ? QByteArrayLiteral(" refreshOnLoad=\"1\"") : QByteArray()) + " recordCount=\""
        + QByteArray::number(cache.recordCount) + "\"><cacheSource type=\"worksheet\"><worksheetSource";
    if (!cache.source.name.isEmpty()) {
        xml += attr("name", cache.source.name);
    } else {
        xml += attr("ref", addrText(cache.source.range.topLeft) + QLatin1Char(':') + addrText(cache.source.range.bottomRight))
            + attr("sheet", unquotedSheetName(cache.source.sheet));
    }
    xml += "/></cacheSource><cacheFields count=\"" + QByteArray::number(cache.fields.size()) + "\">";
    const QHash<qint32, qint32> par = parents(cache);
    for (qsizetype f = 0; f < cache.fields.size(); ++f) {
        const PivotCacheField &field = cache.fields.at(f);
        xml += QByteArrayLiteral("<cacheField") + attr("name", field.name) + " numFmtId=\""
            + QByteArray::number(formatId(field.numberFormat)) + '"'
            + (field.database ? QByteArray() : QByteArrayLiteral(" databaseField=\"0\"")) + '>';
        if (field.database) {
            xml += sharedItemsXml(field, listed.contains(qint32(f)));
        }
        if (field.grouped() || par.contains(qint32(f))) {
            xml += fieldGroupXml(cache, qint32(f), par);
        }
        xml += "</cacheField>";
    }
    xml += "</cacheFields>";
    xml += cache.extensions; // the source's extLst (slicers name its x14 id)
    return xml + "</pivotCacheDefinition>";
}

QByteArray pivotCacheRecordsXml(const PivotCache &cache, const QSet<qint32> &listed)
{
    QByteArray xml = QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                       "<pivotCacheRecords ")
        + kRoots + " count=\"" + QByteArray::number(cache.recordCount) + "\">";
    QList<qint32> database;
    QList<bool> dates;
    for (qsizetype f = 0; f < cache.fields.size(); ++f) {
        if (cache.fields.at(f).database) {
            database.append(qint32(f));
            dates.append(dateFormat(cache.fields.at(f).numberFormat));
        }
    }
    for (qint32 r = 0; r < cache.recordCount; ++r) {
        xml += "<r>";
        for (qsizetype d = 0; d < database.size(); ++d) {
            const PivotCacheField &field = cache.fields.at(database.at(d));
            const qint32 item = r < field.records.size() ? field.records.at(r) : 0;
            if (listed.contains(database.at(d))) {
                xml += "<x v=\"" + QByteArray::number(item) + "\"/>";
            } else {
                xml += itemXml(item >= 0 && item < field.items.size() ? field.items.at(item) : Value{}, dates.at(d));
            }
        }
        xml += "</r>";
    }
    return xml + "</pivotCacheRecords>";
}

} // namespace QindaOffice::Calc::IO
