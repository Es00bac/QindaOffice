// SPDX-License-Identifier: GPL-3.0-or-later
// Excel PivotTables into the model on open (xlsx_read_pivot.h; M74,
// D-083): each sheet's pivotTableDefinition parts, their caches, and the
// check that the model lays each one out exactly as the file shows it.
//
// AGENT-NOTE: the check compares the model's layout with the cells Excel
// wrote — the location, the header rows, every value and item label. Text
// the layout makes up ("Row Labels", "Grand Total", "(All)") only has to be
// there: Excel writes it in the user's language. A pivot that does not
// match is kept as read (read-only, carried), never re-drawn differently.

#include "xlsx_read_pivot.h"

#include "xlsx_parts.h"
#include "xlsx_pivot_names.h"
#include "xlsx_text.h"

#include "ooxml_namespaces.h"
#include "ooxml_package.h"
#include "pivot_cache.h"
#include "pivot_edit.h"
#include "sheet.h"
#include "workbook.h"

#include <QObject>

#include <cmath>

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

struct ReadPivot {
    PivotTable pivot;
    QString reason;
    CellRange location;
    qint32 firstHeaderRow = 1;
    qint32 firstDataRow = 1;
    qint32 firstDataCol = 1;
    qint32 rowPageCount = 0;
};

QList<qint32> axisFields(const Ooxml::XmlElement &root, QStringView name)
{
    QList<qint32> fields;
    if (const Ooxml::XmlElement *list = root.child(sml(), name)) {
        for (const Ooxml::XmlElement *field : list->elements(sml(), u"field")) {
            fields.append(field->attr(u"x").toInt());
        }
    }
    return fields;
}

void readFilter(const Ooxml::XmlElement &element, ReadPivot &read)
{
    const qint32 f = element.attr(u"fld").toInt();
    std::optional<PivotFilter> filter = PivotNames::filterFrom(element.attr(u"type"));
    if (!filter.has_value() || f < 0 || f >= read.pivot.fields.size()) {
        read.reason = QObject::tr("uses a filter QindaCalc does not have (dates)");
        return;
    }
    filter->dataField = element.attr(u"iMeasureFld", QStringLiteral("0")).toInt();
    filter->text1 = decodeXString(element.attr(u"stringValue1"));
    filter->text2 = decodeXString(element.attr(u"stringValue2"));
    QList<double> operands;
    if (const Ooxml::XmlElement *auto_ = element.child(sml(), u"autoFilter")) {
        if (const Ooxml::XmlElement *column = auto_->child(sml(), u"filterColumn")) {
            if (const Ooxml::XmlElement *top = column->child(sml(), u"top10")) {
                filter->top = flag(*top, u"top", true);
                operands.append(top->attr(u"val").toDouble());
            }
            if (const Ooxml::XmlElement *custom = column->child(sml(), u"customFilters")) {
                for (const Ooxml::XmlElement *one : custom->elements(sml(), u"customFilter")) {
                    operands.append(one->attr(u"val").toDouble());
                }
            }
        }
    }
    bool ok = false;
    const double first = filter->text1.toDouble(&ok);
    filter->value1 = ok ? first : operands.value(0);
    const double second = filter->text2.toDouble(&ok);
    filter->value2 = ok ? second : operands.value(1);
    read.pivot.fields[f].filters.append(*filter);
}

ReadPivot readDefinition(const Ooxml::XmlElement &root, const PivotCache &cache, const QHash<int, QString> &formats)
{
    ReadPivot read;
    PivotTable &p = read.pivot;
    const auto refuse = [&read](const QString &why) {
        if (read.reason.isEmpty()) {
            read.reason = why;
        }
    };
    p.name = decodeXString(root.attr(u"name"));
    p.cacheId = root.attr(u"cacheId").toInt();
    PivotOptions &o = p.options;
    o.dataCaption = decodeXString(root.attr(u"dataCaption", QStringLiteral("Values")));
    o.grandTotalCaption = decodeXString(root.attr(u"grandTotalCaption"));
    o.rowHeaderCaption = decodeXString(root.attr(u"rowHeaderCaption"));
    o.colHeaderCaption = decodeXString(root.attr(u"colHeaderCaption"));
    o.missingCaption = decodeXString(root.attr(u"missingCaption"));
    o.showMissing = flag(root, u"showMissing", true);
    o.rowGrandTotals = flag(root, u"rowGrandTotals", true);
    o.colGrandTotals = flag(root, u"colGrandTotals", true);
    o.showHeaders = flag(root, u"showHeaders", true);
    if (const Ooxml::XmlElement *location = root.child(sml(), u"location")) {
        read.location = rangeFromText(location->attr(u"ref")).value_or(CellRange{});
        read.firstHeaderRow = location->attr(u"firstHeaderRow").toInt();
        read.firstDataRow = location->attr(u"firstDataRow").toInt();
        read.firstDataCol = location->attr(u"firstDataCol").toInt();
        read.rowPageCount = location->attr(u"rowPageCount").toInt();
    }
    p.location = read.location.topLeft;
    const QList<PivotFunction> order = PivotNames::excelSubtotalOrder();
    QList<QPair<bool, bool>> layout; // per field: compact, outline
    const Ooxml::XmlElement *fields = root.child(sml(), u"pivotFields");
    for (const Ooxml::XmlElement *element : fields != nullptr ? fields->elements(sml(), u"pivotField")
                                                              : QList<const Ooxml::XmlElement *>()) {
        const qint32 f = qint32(p.fields.size());
        PivotFieldSettings settings;
        settings.caption = decodeXString(element->attr(u"name"));
        settings.showAllItems = flag(*element, u"showAll", true);
        settings.subtotals = flag(*element, u"defaultSubtotal", true);
        settings.subtotalTop = flag(*element, u"subtotalTop", true);
        settings.insertBlankRow = flag(*element, u"insertBlankRow", false);
        settings.multipleSelection = flag(*element, u"multipleItemSelectionAllowed", false);
        const QString sort = element->attr(u"sortType");
        settings.sort = sort == QLatin1String("ascending") ? PivotSort::Ascending
            : sort == QLatin1String("descending")          ? PivotSort::Descending
                                                           : PivotSort::Manual;
        for (const PivotFunction function : order) {
            if (flag(*element, PivotNames::subtotalAttribute(function), false)) {
                settings.customSubtotals.append(function);
            }
        }
        if (!settings.customSubtotals.isEmpty()) {
            settings.subtotals = false;
        }
        const qsizetype count = f < cache.fields.size() ? pivotDisplayItems(cache, f).size() : 0;
        if (const Ooxml::XmlElement *items = element->child(sml(), u"items")) {
            for (const Ooxml::XmlElement *item : items->elements(sml(), u"item")) {
                const QString type = item->attr(u"t", QStringLiteral("data"));
                if (type != QLatin1String("data")) {
                    continue; // the subtotal items: the field's flags say them
                }
                const qint32 x = item->attr(u"x", QStringLiteral("-1")).toInt();
                if (x < 0 || x >= count || item->hasAttribute(QStringView(), u"f")) {
                    refuse(QObject::tr("has calculated items"));
                    continue;
                }
                if (item->hasAttribute(QStringView(), u"n")) {
                    refuse(QObject::tr("has renamed items"));
                }
                if (!flag(*item, u"sd", true)) {
                    refuse(QObject::tr("has collapsed items"));
                }
                settings.order.append(x);
                if (flag(*item, u"h", false) || flag(*item, u"m", false)) {
                    settings.hidden.insert(x);
                }
            }
        }
        if (const Ooxml::XmlElement *scope = element->child(sml(), u"autoSortScope")) {
            const Ooxml::XmlElement *reference = scope->descendant(sml(), u"reference");
            const Ooxml::XmlElement *x = reference != nullptr ? reference->child(sml(), u"x") : nullptr;
            if (reference != nullptr && reference->attr(u"field").toUInt() == PivotNames::kValuesFieldUnsigned) {
                settings.sortData = x != nullptr ? x->attr(u"v", QStringLiteral("0")).toInt() : 0;
            }
        }
        if (const Ooxml::XmlElement *ext = element->child(sml(), u"extLst")) {
            const Ooxml::XmlElement *x14 = ext->descendant(QString::fromLatin1(PivotNames::kX14), u"pivotField");
            settings.repeatLabels = x14 != nullptr && flag(*x14, u"fillDownLabels", false);
        }
        layout.append({flag(*element, u"compact", true), flag(*element, u"outline", true)});
        p.fields.append(settings);
    }
    if (p.fields.size() != cache.fields.size()) {
        refuse(QObject::tr("does not match its cache"));
        return read;
    }
    p.rowFields = axisFields(root, u"rowFields");
    p.colFields = axisFields(root, u"colFields");
    // The form: the first row field's compact and outline flags (Excel sets
    // every field's when Report Layout changes), else the table's own.
    bool compact = flag(root, u"compact", true);
    bool outline = flag(root, u"outline", false);
    for (const qint32 f : std::as_const(p.rowFields)) {
        if (f >= 0 && f < layout.size()) {
            compact = layout.at(f).first;
            outline = layout.at(f).second;
            break;
        }
    }
    o.form = !outline ? PivotForm::Tabular : (compact ? PivotForm::Compact : PivotForm::Outline);
    if (const Ooxml::XmlElement *pages = root.child(sml(), u"pageFields")) {
        for (const Ooxml::XmlElement *page : pages->elements(sml(), u"pageField")) {
            const qint32 f = page->attr(u"fld").toInt();
            if (f < 0 || f >= p.fields.size()) {
                refuse(QObject::tr("does not match its cache"));
                continue;
            }
            p.pageFields.append(f);
            if (page->hasAttribute(QStringView(), u"item")) {
                p.fields[f].pageItem = p.fields.at(f).order.value(page->attr(u"item").toInt(), -1);
            }
        }
    }
    if (const Ooxml::XmlElement *data = root.child(sml(), u"dataFields")) {
        for (const Ooxml::XmlElement *element : data->elements(sml(), u"dataField")) {
            PivotDataField field;
            field.name = decodeXString(element->attr(u"name"));
            field.field = element->attr(u"fld").toInt();
            field.function = PivotNames::dataSubtotalFrom(element->attr(u"subtotal", QStringLiteral("sum")))
                                 .value_or(PivotFunction::Sum);
            field.showAs = PivotNames::showAsFrom(element->attr(u"showDataAs", QStringLiteral("normal")))
                               .value_or(PivotShowAs::Normal);
            if (const Ooxml::XmlElement *ext = element->child(sml(), u"extLst")) {
                const Ooxml::XmlElement *x14 = ext->descendant(QString::fromLatin1(PivotNames::kX14), u"dataField");
                if (x14 != nullptr && x14->hasAttribute(QStringView(), u"pivotShowAs")) {
                    field.showAs = PivotNames::showAsFrom(x14->attr(u"pivotShowAs")).value_or(field.showAs);
                }
            }
            field.baseField = element->attr(u"baseField", QStringLiteral("0")).toInt();
            const qint32 baseItem = element->attr(u"baseItem", QStringLiteral("0")).toInt();
            const QList<qint32> baseOrder = field.baseField >= 0 && field.baseField < p.fields.size()
                ? p.fields.at(field.baseField).order
                : QList<qint32>();
            field.baseItem = baseItem == kPivotPreviousItem || baseItem == kPivotNextItem ? baseItem
                                                                                          : baseOrder.value(baseItem, -1);
            if (element->hasAttribute(QStringView(), u"numFmtId")) {
                field.numberFormat = pivotNumberFormat(element->attr(u"numFmtId").toInt(), formats);
            }
            if (field.field < 0 || field.field >= cache.fields.size()) {
                refuse(QObject::tr("has a calculated data field"));
                continue;
            }
            p.dataFields.append(field);
        }
    }
    if (const Ooxml::XmlElement *style = root.child(sml(), u"pivotTableStyleInfo")) {
        o.style.name = style->attr(u"name");
        o.style.rowHeaders = flag(*style, u"showRowHeaders", true);
        o.style.colHeaders = flag(*style, u"showColHeaders", true);
        o.style.rowStripes = flag(*style, u"showRowStripes", false);
        o.style.colStripes = flag(*style, u"showColStripes", false);
        o.style.lastColumn = flag(*style, u"showLastColumn", true);
    } else {
        o.style.name.clear();
    }
    if (const Ooxml::XmlElement *filters = root.child(sml(), u"filters")) {
        for (const Ooxml::XmlElement *filter : filters->elements(sml(), u"filter")) {
            readFilter(*filter, read);
        }
    }
    for (const char16_t *olap : {u"calculatedItems", u"calculatedMembers", u"pivotHierarchies",
                                 u"rowHierarchiesUsage", u"colHierarchiesUsage"}) {
        if (root.child(sml(), olap) != nullptr) {
            refuse(QObject::tr("has calculated items or OLAP hierarchies"));
        }
    }
    if (const Ooxml::XmlElement *ext = root.child(sml(), u"extLst")) {
        p.extensions = Ooxml::serializeFragment(*ext).toUtf8();
    }
    p.customFormats = root.child(sml(), u"formats") != nullptr || root.child(sml(), u"conditionalFormats") != nullptr
        || root.child(sml(), u"chartFormats") != nullptr;
    // Σ Values lives in rowFields / colFields as -2; dataOnRows says where
    // when a producer left it out.
    if (p.dataFields.size() >= 2 && !p.rowFields.contains(kPivotValuesField) && !p.colFields.contains(kPivotValuesField)) {
        (flag(root, u"dataOnRows", false) ? p.rowFields : p.colFields).append(kPivotValuesField);
    }
    return read;
}

// What a pivot covers in the file: its location and the filters above it.
CellRange fileExtent(const ReadPivot &read)
{
    const qint32 pageRows = read.pivot.pageFields.isEmpty() ? 0 : qMax(read.rowPageCount, 1) + 1;
    return {{qMax(0, read.location.topLeft.row - pageRows), read.location.topLeft.col},
            {read.location.bottomRight.row,
             qMax(read.location.bottomRight.col, read.location.topLeft.col + (pageRows > 0 ? 1 : 0))}};
}

bool sameValue(const Value &mine, const Value &file)
{
    if (isNumber(mine) && isNumber(file)) {
        const double a = std::get<double>(mine);
        const double b = std::get<double>(file);
        return qAbs(a - b) <= 1e-9 * qMax(1.0, qAbs(a));
    }
    if (isBlank(mine) || isBlank(file)) {
        return isBlank(mine) && isBlank(file);
    }
    return toText(mine).trimmed().compare(toText(file).trimmed(), Qt::CaseInsensitive) == 0;
}

// The model's layout agrees with what the file shows.
bool matchesFile(const Workbook &book, qint32 sheet, const PivotTable &pivot, const ReadPivot &read)
{
    const PivotRender *render = pivot.render.get();
    if (render == nullptr || render->body != read.location) {
        return false;
    }
    if (!render->empty && (render->firstHeaderRow != read.firstHeaderRow || render->firstDataRow != read.firstDataRow
                           || render->firstDataCol != read.firstDataCol)) {
        return false;
    }
    for (auto it = render->cells.constBegin(); it != render->cells.constEnd(); ++it) {
        const CellAddr addr{qint32(it.key() & 0xffffffffu), qint32(it.key() >> 32)};
        const Value file = book.cellValue(sheet, addr.row, addr.col);
        if (it->caption ? isBlank(file) != isBlank(it->value) : !sameValue(it->value, file)) {
            return false;
        }
    }
    for (qint32 row = read.location.topLeft.row; !render->empty && row <= read.location.bottomRight.row; ++row) {
        for (qint32 col = read.location.topLeft.col; col <= read.location.bottomRight.col; ++col) {
            if (!render->cells.contains(CellAddr{row, col}.key()) && !isBlank(book.cellValue(sheet, row, col))) {
                return false; // the file shows something the model does not
            }
        }
    }
    return true;
}

} // namespace

void readPivotTables(const QString &packagePath, Workbook *out, QStringList *warnings)
{
    QString error;
    const std::shared_ptr<const Ooxml::Package> package = Ooxml::Package::load(packagePath, &error);
    if (!package || out == nullptr) {
        return;
    }
    const QString workbookPart = package->mainPart();
    const QHash<int, QString> formats = readPivotNumberFormats(*package);
    QHash<QString, qint32> cacheIds; // cache definition part → its workbook cacheId
    if (const Ooxml::XmlElementPtr root = package->xml(workbookPart)) {
        if (const Ooxml::XmlElement *caches = root->child(sml(), u"pivotCaches")) {
            for (const Ooxml::XmlElement *cache : caches->elements(sml(), u"pivotCache")) {
                const auto rel = package->relationship(workbookPart, cache->attribute(QString::fromUtf16(Ooxml::Ns::R), u"id"));
                if (rel.has_value()) {
                    cacheIds.insert(package->targetPart(workbookPart, *rel), cache->attr(u"cacheId").toInt());
                }
            }
        }
    }
    QHash<QString, qint32> cacheIndex; // part → index in out->pivotCaches
    QHash<qint32, QString> cacheReason; // cache id → why it is not modelled
    QList<QPair<PivotRef, ReadPivot>> read;
    for (qint32 s = 0; s < out->sheetCount(); ++s) {
        const QString sheetPart = out->sheetAt(s).source().part;
        for (const Ooxml::Relationship &rel : sheetPart.isEmpty() ? QList<Ooxml::Relationship>()
                                                                   : package->relationships(sheetPart)) {
            if (rel.external || !rel.type.endsWith(QLatin1String("/pivotTable"))) {
                continue;
            }
            const QString pivotPart = package->targetPart(sheetPart, rel);
            const Ooxml::XmlElementPtr root = package->xml(pivotPart);
            QString cachePart;
            for (const Ooxml::Relationship &inner : package->relationships(pivotPart)) {
                if (inner.type.endsWith(QLatin1String("/pivotCacheDefinition"))) {
                    cachePart = package->targetPart(pivotPart, inner);
                }
            }
            if (!root || cachePart.isEmpty()) {
                continue; // carried as it was (M66)
            }
            if (!cacheIndex.contains(cachePart)) {
                QString reason;
                PivotCache cache = readPivotCachePart(*package, cachePart, formats, &reason);
                cache.id = cacheIds.contains(cachePart) ? cacheIds.value(cachePart) : nextPivotCacheId(*out);
                cache.readFingerprint = pivotCacheFingerprint(cache);
                if (!cache.modelled) {
                    cacheReason.insert(cache.id, reason);
                }
                cacheIndex.insert(cachePart, qint32(out->pivotCaches.size()));
                out->pivotCaches.append(cache);
            }
            const PivotCache &cache = out->pivotCaches.at(cacheIndex.value(cachePart));
            ReadPivot one = readDefinition(*root, cache, formats);
            PivotTable &pivot = one.pivot;
            pivot.cacheId = cache.id;
            pivot.sourcePart = pivotPart;
            if (one.reason.isEmpty()) {
                pivotNormalize(pivot, cache); // the model's spelling of what Excel leaves implicit
            }
            pivot.readFingerprint = pivotTableFingerprint(pivot);
            pivot.extent = fileExtent(one);
            if (!one.reason.isEmpty() && !cacheReason.contains(cache.id)) {
                cacheReason.insert(cache.id, one.reason); // the whole cache's pivots stay as read
            }
            const PivotRef ref{s, qint32(out->sheetAt(s).pivots().size())};
            out->sheetAt(s).mutablePivots().append(pivot);
            out->source.pivotParts.append(pivotPart);
            read.append({ref, one});
        }
    }
    // A cache marked refreshOnLoad is refreshed, as Excel does on open
    // (a cache Excel saved without its records is shown only so).
    for (qsizetype i = 0; i < out->pivotCaches.size(); ++i) {
        const PivotCache cache = out->pivotCaches.at(i);
        if (cache.modelled && cache.refreshOnLoad && !cacheReason.contains(cache.id)) {
            static_cast<void>(refreshPivotCache(*out, cache.id));
        }
    }
    for (const auto &[ref, one] : std::as_const(read)) {
        const PivotTable *pivot = pivotOf(*out, ref);
        if (cacheReason.contains(pivot->cacheId) || pivot->render) {
            continue;
        }
        rebuildPivotRender(*out, ref);
        if (!matchesFile(*out, ref.sheet, *pivotOf(*out, ref), one)) {
            cacheReason.insert(pivot->cacheId, QObject::tr("is laid out in a way QindaCalc does not reproduce"));
        }
    }
    QSet<CellKey> output;
    for (const auto &[ref, one] : std::as_const(read)) {
        PivotTable &pivot = out->sheetAt(ref.sheet).mutablePivots()[ref.index];
        if (cacheReason.contains(pivot.cacheId)) {
            pivot.modelled = false;
            pivot.note = cacheReason.value(pivot.cacheId);
            pivot.render.reset();
            pivot.result.reset();
            pivot.location = one.location.topLeft;
            pivot.extent = fileExtent(one);
            if (warnings != nullptr) {
                warnings->append(QObject::tr("PivotTable “%1” on “%2” %3; QindaCalc keeps it as it is, read-only.")
                                     .arg(pivot.name, out->sheetName(ref.sheet), pivot.note));
            }
        }
        for (qint32 row = pivot.extent.topLeft.row; row <= pivot.extent.bottomRight.row; ++row) {
            for (qint32 col = pivot.extent.topLeft.col; col <= pivot.extent.bottomRight.col; ++col) {
                output.insert(CellKey(ref.sheet, row, col));
            }
        }
    }
    for (PivotCache &cache : out->pivotCaches) {
        cache.modelled = cache.modelled && !cacheReason.contains(cache.id);
    }
    // GETPIVOTDATA formulas computed during the load found no pivot yet.
    out->recalc(output);
}

} // namespace QindaOffice::Calc::IO
