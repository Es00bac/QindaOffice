// SPDX-License-Identifier: GPL-3.0-or-later
// A PivotTable as Excel's pivotTableDefinition (xlsx_write_pivot.h; M74,
// D-083): location, pivotFields with their items, the four areas, the
// rowItems / colItems lines of the layout the cells show, data fields with
// Show Values As and number formats, the style, and the filters.
//
// AGENT-GUARD: rowItems / colItems and location come from the same
// PivotRender the cells were written from (pivot_layout.cpp) — never
// recomputed here. Excel opens a pivot without a refresh only when they
// agree with each other and with its cache.

#include "xlsx_write_pivot.h"

#include "xlsx_pivot_names.h"
#include "pivot_dates.h"
#include "xlsx_text.h"
#include "xlsx_xml.h"

namespace QindaOffice::Calc::IO {

namespace {

QByteArray attr(const char *name, const QString &value)
{
    return QByteArray(" ") + name + "=\"" + xmlAttribute(encodeXString(value)) + '"';
}

QByteArray num(const char *name, qint64 value)
{
    return QByteArray(" ") + name + "=\"" + QByteArray::number(value) + '"';
}

QByteArray number(double value)
{
    QString text = QString::number(value, 'g', 15);
    if (text.toDouble() != value) {
        text = QString::number(value, 'g', 17);
    }
    return text.toUtf8();
}

QByteArray linesXml(const char *name, const QList<PivotLine> &lines)
{
    QByteArray xml = QByteArray("<") + name + num("count", lines.size()) + '>';
    for (const PivotLine &line : lines) {
        xml += "<i";
        switch (line.type) {
        case PivotLine::Type::Data: break;
        case PivotLine::Type::Subtotal:
            xml += attr("t", line.custom ? PivotNames::itemType(line.function) : QStringLiteral("default"));
            break;
        case PivotLine::Type::Grand: xml += " t=\"grand\""; break;
        case PivotLine::Type::Blank: xml += " t=\"blank\""; break;
        }
        if (line.repeat > 0) {
            xml += num("r", line.repeat);
        }
        if (line.dataIndex > 0) {
            xml += num("i", line.dataIndex);
        }
        if (line.items.isEmpty()) {
            xml += "/>";
            continue;
        }
        xml += '>';
        for (const qint32 x : line.items) {
            xml += x == 0 ? QByteArrayLiteral("<x/>") : QByteArrayLiteral("<x") + num("v", x) + "/>";
        }
        xml += "</i>";
    }
    return xml + "</" + name + '>';
}

QByteArray fieldsXml(const char *name, const QList<qint32> &fields)
{
    if (fields.isEmpty()) {
        return {};
    }
    QByteArray xml = QByteArray("<") + name + num("count", fields.size()) + '>';
    for (const qint32 f : fields) {
        xml += QByteArrayLiteral("<field") + num("x", f) + "/>";
    }
    return xml + "</" + name + '>';
}

QByteArray pivotFieldXml(const PivotTable &pivot, const PivotCache &cache, qint32 f)
{
    const PivotFieldSettings &s = pivot.fields.at(f);
    QByteArray xml = QByteArrayLiteral("<pivotField");
    if (!s.caption.isEmpty()) {
        xml += attr("name", s.caption);
    }
    const bool row = pivot.rowFields.contains(f);
    const bool col = pivot.colFields.contains(f);
    const bool page = pivot.pageFields.contains(f);
    if (row || col || page) {
        xml += row ? " axis=\"axisRow\"" : (col ? " axis=\"axisCol\"" : " axis=\"axisPage\"");
    }
    for (const PivotDataField &data : pivot.dataFields) {
        if (data.field == f) {
            xml += " dataField=\"1\"";
            break;
        }
    }
    if (pivot.options.form != PivotForm::Compact) {
        xml += " compact=\"0\"";
    }
    if (pivot.options.form == PivotForm::Tabular) {
        xml += " outline=\"0\"";
    }
    if (page && s.multipleSelection) {
        xml += " multipleItemSelectionAllowed=\"1\"";
    }
    if (!s.showAllItems) {
        xml += " showAll=\"0\"";
    }
    if (s.insertBlankRow) {
        xml += " insertBlankRow=\"1\"";
    }
    if (!s.subtotalTop) {
        xml += " subtotalTop=\"0\"";
    }
    if (!s.subtotals || !s.customSubtotals.isEmpty()) {
        xml += " defaultSubtotal=\"0\"";
    }
    const QList<PivotFunction> custom = [&s] {
        QList<PivotFunction> ordered;
        for (const PivotFunction function : PivotNames::excelSubtotalOrder()) {
            if (s.customSubtotals.contains(function)) {
                ordered.append(function);
            }
        }
        return ordered;
    }();
    for (const PivotFunction function : custom) {
        xml += QByteArray(" ") + PivotNames::subtotalAttribute(function).toUtf8() + "=\"1\"";
    }
    if (s.sort != PivotSort::Manual) {
        xml += s.sort == PivotSort::Ascending ? " sortType=\"ascending\"" : " sortType=\"descending\"";
    }
    const bool items = row || col || page || !s.hidden.isEmpty();
    const bool sorted = s.sortData >= 0 && s.sortData < pivot.dataFields.size();
    if (!items && !sorted && !s.repeatLabels) {
        return xml + "/>";
    }
    xml += '>';
    if (items) {
        const qsizetype subtotals = custom.isEmpty() ? (s.subtotals ? 1 : 0) : custom.size();
        xml += "<items" + num("count", s.order.size() + subtotals) + '>';
        for (const qint32 item : s.order) {
            xml += QByteArrayLiteral("<item") + (s.hidden.contains(item) ? QByteArrayLiteral(" h=\"1\"") : QByteArray())
                + num("x", item) + "/>";
        }
        if (custom.isEmpty() && s.subtotals) {
            xml += "<item t=\"default\"/>";
        }
        for (const PivotFunction function : custom) {
            xml += QByteArrayLiteral("<item") + attr("t", PivotNames::itemType(function)) + "/>";
        }
        xml += "</items>";
    }
    if (sorted) {
        xml += "<autoSortScope><pivotArea dataOnly=\"0\" outline=\"0\" fieldPosition=\"0\"><references count=\"1\">"
               "<reference field=\""
            + QByteArray::number(PivotNames::kValuesFieldUnsigned) + "\" count=\"1\" selected=\"0\"><x"
            + num("v", s.sortData) + "/></reference></references></pivotArea></autoSortScope>";
    }
    if (s.repeatLabels) {
        xml += QByteArrayLiteral("<extLst><ext") + attr("uri", QString::fromLatin1(PivotNames::kPivotFieldExt))
            + attr("xmlns:x14", QString::fromLatin1(PivotNames::kX14)) + "><x14:pivotField fillDownLabels=\"1\"/></ext></extLst>";
    }
    Q_UNUSED(cache);
    return xml + "</pivotField>";
}

QByteArray dataFieldsXml(const PivotTable &pivot, const PivotFormatId &formatId)
{
    if (pivot.dataFields.isEmpty()) {
        return {};
    }
    QByteArray xml = "<dataFields" + num("count", pivot.dataFields.size()) + '>';
    for (const PivotDataField &data : pivot.dataFields) {
        xml += QByteArrayLiteral("<dataField") + attr("name", data.name) + num("fld", data.field);
        if (data.function != PivotFunction::Sum) {
            xml += attr("subtotal", PivotNames::dataSubtotal(data.function));
        }
        const QString ecma = PivotNames::showDataAs(data.showAs);
        if (data.showAs != PivotShowAs::Normal && !ecma.isEmpty()) {
            xml += attr("showDataAs", ecma);
        }
        const qint32 base = data.baseField >= 0 && data.baseField < pivot.fields.size() ? data.baseField : 0;
        qint32 baseItem = 0;
        if (data.baseItem == kPivotPreviousItem || data.baseItem == kPivotNextItem) {
            baseItem = data.baseItem;
        } else if (data.baseItem >= 0) {
            baseItem = qint32(qMax(qsizetype(0), pivot.fields.at(base).order.indexOf(data.baseItem)));
        }
        xml += num("baseField", base) + num("baseItem", baseItem);
        if (const int id = formatId(data.numberFormat); id != 0) {
            xml += num("numFmtId", id);
        }
        const QString x14 = PivotNames::pivotShowAs(data.showAs);
        if (x14.isEmpty()) {
            xml += "/>";
            continue;
        }
        xml += QByteArrayLiteral("><extLst><ext") + attr("uri", QString::fromLatin1(PivotNames::kDataFieldExt))
            + attr("xmlns:x14", QString::fromLatin1(PivotNames::kX14)) + "><x14:dataField" + attr("pivotShowAs", x14)
            + "/></ext></extLst></dataField>";
    }
    return xml + "</dataFields>";
}

QByteArray customFiltersXml(const PivotFilter &f)
{
    using Op = PivotFilter::Op;
    const bool label = f.kind == PivotFilter::Kind::Label;
    const auto operand = [&](bool second) {
        return label ? (second ? f.text2 : f.text1) : QString::fromUtf8(number(second ? f.value2 : f.value1));
    };
    const auto one = [&](const char *op, const QString &value) {
        return QByteArrayLiteral("<customFilter") + (op[0] != '\0' ? QByteArray(" operator=\"") + op + '"' : QByteArray())
            + attr("val", value) + "/>";
    };
    switch (f.op) {
    case Op::Equal: return "<customFilters>" + one("", operand(false)) + "</customFilters>";
    case Op::NotEqual: return "<customFilters>" + one("notEqual", operand(false)) + "</customFilters>";
    case Op::BeginsWith: return "<customFilters>" + one("", operand(false) + QLatin1Char('*')) + "</customFilters>";
    case Op::NotBeginsWith:
        return "<customFilters>" + one("notEqual", operand(false) + QLatin1Char('*')) + "</customFilters>";
    case Op::EndsWith: return "<customFilters>" + one("", QLatin1Char('*') + operand(false)) + "</customFilters>";
    case Op::NotEndsWith:
        return "<customFilters>" + one("notEqual", QLatin1Char('*') + operand(false)) + "</customFilters>";
    case Op::Contains:
        return "<customFilters>" + one("", QLatin1Char('*') + operand(false) + QLatin1Char('*')) + "</customFilters>";
    case Op::NotContains:
        return "<customFilters>" + one("notEqual", QLatin1Char('*') + operand(false) + QLatin1Char('*'))
            + "</customFilters>";
    case Op::Greater: return "<customFilters>" + one("greaterThan", operand(false)) + "</customFilters>";
    case Op::GreaterOrEqual: return "<customFilters>" + one("greaterThanOrEqual", operand(false)) + "</customFilters>";
    case Op::Less: return "<customFilters>" + one("lessThan", operand(false)) + "</customFilters>";
    case Op::LessOrEqual: return "<customFilters>" + one("lessThanOrEqual", operand(false)) + "</customFilters>";
    case Op::Between:
        return "<customFilters and=\"1\">" + one("greaterThanOrEqual", operand(false))
            + one("lessThanOrEqual", operand(true)) + "</customFilters>";
    case Op::NotBetween:
        return "<customFilters>" + one("lessThan", operand(false)) + one("greaterThan", operand(true))
            + "</customFilters>";
    }
    return {};
}

QByteArray filtersXml(const PivotTable &pivot)
{
    QByteArray xml;
    int id = 0;
    for (qsizetype f = 0; f < pivot.fields.size(); ++f) {
        for (const PivotFilter &filter : pivot.fields.at(f).filters) {
            const QString type = PivotNames::filterType(filter);
            if (type.isEmpty()) {
                continue;
            }
            xml += QByteArrayLiteral("<filter") + num("fld", f) + attr("type", type) + " evalOrder=\"-1\"" + num("id", ++id);
            if (filter.kind == PivotFilter::Kind::Value || filter.kind == PivotFilter::Kind::Top) {
                xml += num("iMeasureFld", filter.dataField);
            }
            if (filter.kind == PivotFilter::Kind::Label || filter.kind == PivotFilter::Kind::Date) {
                xml += attr("stringValue1", filter.text1);
                if (filter.op == PivotFilter::Op::Between || filter.op == PivotFilter::Op::NotBetween || filter.kind == PivotFilter::Kind::Date) {
                    xml += attr("stringValue2", filter.text2);
                }
            }
            xml += "><autoFilter ref=\"A1\"><filterColumn colId=\"0\">";
            if (filter.kind == PivotFilter::Kind::Top) {
                xml += "<top10" + QByteArray(filter.top ? "" : " top=\"0\"")
                    + QByteArray(filter.topBy == PivotFilter::TopBy::Percent ? " percent=\"1\"" : "") + " val=\""
                    + number(filter.value1) + "\" filterVal=\"" + number(filter.value1) + "\"/>";
            } else if (filter.kind == PivotFilter::Kind::Date) {
                if (!filter.dateType.startsWith(QLatin1String("date"))) {
                    xml += "<dynamicFilter" + attr("type", filter.dateType) + "/>";
                } else {
                    PivotFilter dates = filter;
                    const QString &type = filter.dateType;
                    dates.value1 = PivotNames::serialFromIso(filter.text1).value_or(filter.value1);
                    dates.value2 = PivotNames::serialFromIso(filter.text2).value_or(filter.value2);
                    dates.op = type == QLatin1String("dateBetween") ? PivotFilter::Op::Between
                        : type == QLatin1String("dateNotBetween") ? PivotFilter::Op::NotBetween
                        : type == QLatin1String("dateOlderThan") ? PivotFilter::Op::Less
                        : type == QLatin1String("dateOlderThanOrEqual") ? PivotFilter::Op::LessOrEqual
                        : type == QLatin1String("dateNewerThan") ? PivotFilter::Op::Greater
                        : type == QLatin1String("dateNewerThanOrEqual") ? PivotFilter::Op::GreaterOrEqual
                        : type == QLatin1String("dateNotEqual") ? PivotFilter::Op::NotEqual : PivotFilter::Op::Equal;
                    xml += customFiltersXml(dates);
                }
            } else {
                xml += customFiltersXml(filter);
            }
            xml += "</filterColumn></autoFilter></filter>";
        }
    }
    return id == 0 ? QByteArray() : "<filters" + num("count", id) + '>' + xml + "</filters>";
}

} // namespace

QByteArray pivotTableXml(const PivotTable &pivot, const PivotCache &cache, const PivotFormatId &formatId)
{
    const PivotRender &render = *pivot.render;
    const PivotOptions &o = pivot.options;
    QByteArray xml = QByteArrayLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                                       "<pivotTableDefinition xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"")
        + attr("name", pivot.name) + num("cacheId", pivot.cacheId)
        + " applyNumberFormats=\"0\" applyBorderFormats=\"0\" applyFontFormats=\"0\" applyPatternFormats=\"0\""
          " applyAlignmentFormats=\"0\" applyWidthHeightFormats=\"1\""
        + attr("dataCaption", o.dataCaption.isEmpty() ? QStringLiteral("Values") : o.dataCaption);
    if (!o.grandTotalCaption.isEmpty()) {
        xml += attr("grandTotalCaption", o.grandTotalCaption);
    }
    if (!o.missingCaption.isEmpty()) {
        xml += attr("missingCaption", o.missingCaption);
    }
    if (!o.showMissing) {
        xml += " showMissing=\"0\"";
    }
    xml += " updatedVersion=\"6\" minRefreshableVersion=\"3\"";
    if (!o.showHeaders) {
        xml += " showHeaders=\"0\"";
    }
    xml += " useAutoFormatting=\"1\" itemPrintTitles=\"1\" createdVersion=\"6\" indent=\"0\"";
    if (o.form != PivotForm::Compact) {
        xml += " compact=\"0\" compactData=\"0\"";
    }
    if (o.form != PivotForm::Tabular) {
        xml += " outline=\"1\" outlineData=\"1\"";
    }
    if (!o.rowGrandTotals) {
        xml += " rowGrandTotals=\"0\"";
    }
    if (!o.colGrandTotals) {
        xml += " colGrandTotals=\"0\"";
    }
    if (!o.rowHeaderCaption.isEmpty()) {
        xml += attr("rowHeaderCaption", o.rowHeaderCaption);
    }
    if (!o.colHeaderCaption.isEmpty()) {
        xml += attr("colHeaderCaption", o.colHeaderCaption);
    }
    if (pivot.rowFields.contains(kPivotValuesField)) {
        xml += " dataOnRows=\"1\"";
    }
    xml += " multipleFieldFilters=\"0\"><location"
        + attr("ref", addrText(render.body.topLeft) + QLatin1Char(':') + addrText(render.body.bottomRight))
        + num("firstHeaderRow", render.firstHeaderRow) + num("firstDataRow", render.firstDataRow)
        + num("firstDataCol", render.firstDataCol);
    if (!pivot.pageFields.isEmpty()) {
        xml += num("rowPageCount", pivot.pageFields.size()) + " colPageCount=\"1\"";
    }
    xml += "/><pivotFields" + num("count", pivot.fields.size()) + '>';
    for (qsizetype f = 0; f < pivot.fields.size(); ++f) {
        xml += pivotFieldXml(pivot, cache, qint32(f));
    }
    xml += "</pivotFields>";
    if (!render.empty) {
        xml += fieldsXml("rowFields", pivot.rowFields) + linesXml("rowItems", render.rowLines)
            + fieldsXml("colFields", pivot.colFields);
        if (!render.colLines.isEmpty()) {
            xml += linesXml("colItems", render.colLines);
        }
    }
    if (!pivot.pageFields.isEmpty()) {
        xml += "<pageFields" + num("count", pivot.pageFields.size()) + '>';
        for (const qint32 f : pivot.pageFields) {
            const PivotFieldSettings &s = pivot.fields.at(f);
            xml += QByteArrayLiteral("<pageField") + num("fld", f);
            if (!s.multipleSelection && s.pageItem >= 0 && s.order.contains(s.pageItem)) {
                xml += num("item", s.order.indexOf(s.pageItem));
            }
            xml += " hier=\"-1\"/>";
        }
        xml += "</pageFields>";
    }
    xml += dataFieldsXml(pivot, formatId);
    xml += "<pivotTableStyleInfo";
    if (!o.style.name.isEmpty()) {
        xml += attr("name", o.style.name);
    }
    xml += num("showRowHeaders", o.style.rowHeaders) + num("showColHeaders", o.style.colHeaders)
        + num("showRowStripes", o.style.rowStripes) + num("showColStripes", o.style.colStripes)
        + num("showLastColumn", o.style.lastColumn) + "/>";
    xml += filtersXml(pivot);
    if (!pivot.extensions.isEmpty()) {
        xml += pivot.extensions;
    } else {
        // Excel 2010's own record of a pivot it made: no "Values" row, which
        // is the layout pivot_layout.cpp draws.
        xml += QByteArrayLiteral("<extLst><ext") + attr("uri", QString::fromLatin1(PivotNames::kPivotTableExt))
            + attr("xmlns:x14", QString::fromLatin1(PivotNames::kX14))
            + "><x14:pivotTableDefinition hideValuesRow=\"1\"/></ext></extLst>";
    }
    return xml + "</pivotTableDefinition>";
}

} // namespace QindaOffice::Calc::IO
