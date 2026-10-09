// SPDX-License-Identifier: GPL-3.0-or-later
// CalcHost's field dialogs for PivotTables (M74, D-083): Value Field
// Settings (Summarize Values By, Show Values As with its base field and
// item, the number format, the name), Field Settings (subtotals, layout,
// sort), the item / label / value / Top 10 filter, and grouping (dates by
// parts, numbers by ranges, chosen items as a group). Each dialog asks the
// state as a map and answers with one; each answer is one undo step.

#include "calc_host.h"

#include "spreadsheet_item.h"

#include "number_format.h"
#include "pivot_cache.h"
#include "sheet.h"

#include <cmath>
#include <limits>

namespace QindaOffice::Apps::QindaCalc {

using namespace CalcEng;

namespace {

QStringList functionNames()
{
    return {QObject::tr("Sum"), QObject::tr("Count"), QObject::tr("Average"), QObject::tr("Max"), QObject::tr("Min"),
            QObject::tr("Product"), QObject::tr("Count Numbers"), QObject::tr("StdDev"), QObject::tr("StdDevp"),
            QObject::tr("Var"), QObject::tr("Varp")};
}

QStringList showAsNames()
{
    return {QObject::tr("No Calculation"), QObject::tr("% of Grand Total"), QObject::tr("% of Column Total"),
            QObject::tr("% of Row Total"), QObject::tr("% Of"), QObject::tr("% of Parent Row Total"),
            QObject::tr("% of Parent Column Total"), QObject::tr("% of Parent Total"), QObject::tr("Difference From"),
            QObject::tr("% Difference From"), QObject::tr("Running Total In"), QObject::tr("% Running Total In"),
            QObject::tr("Rank Smallest to Largest"), QObject::tr("Rank Largest to Smallest"), QObject::tr("Index")};
}

// The fields a base field can be: the row and column fields.
QList<qint32> baseFields(const PivotTable &pivot)
{
    QList<qint32> out;
    for (const qint32 f : pivot.rowFields + pivot.colFields) {
        if (f >= 0) {
            out.append(f);
        }
    }
    return out;
}

QVariantList dataNames(const PivotTable &pivot)
{
    QVariantList out;
    for (const PivotDataField &d : pivot.dataFields) {
        out.append(d.name);
    }
    return out;
}

double numberOrNaN(const QVariant &value)
{
    bool ok = false;
    const double n = value.toString().trimmed().toDouble(&ok);
    return ok ? n : std::numeric_limits<double>::quiet_NaN();
}

} // namespace

QVariantMap CalcHost::pivotValueFieldState(int dataIndex) const
{
    const PivotTable *pivot = pivotOf(*m_document->workbook(), currentPivot());
    const PivotCache *cache = pivot != nullptr ? pivotCacheOf(*m_document->workbook(), pivot->cacheId) : nullptr;
    if (cache == nullptr || dataIndex < 0 || dataIndex >= pivot->dataFields.size()) {
        return {};
    }
    const PivotDataField &d = pivot->dataFields.at(dataIndex);
    QVariantList bases;
    const QList<qint32> fields = baseFields(*pivot);
    for (const qint32 f : fields) {
        bases.append(QVariantMap{{QStringLiteral("field"), f}, {QStringLiteral("name"), pivotFieldCaption(*cache, *pivot, f)}});
    }
    int baseItem = 0;
    if (d.baseItem == kPivotNextItem) {
        baseItem = 1;
    } else if (d.baseItem >= 0 && d.baseField >= 0) {
        baseItem = 2 + int(pivot->fields.at(d.baseField).order.indexOf(d.baseItem));
    }
    return {{QStringLiteral("index"), dataIndex},
            {QStringLiteral("source"), d.field >= 0 && d.field < cache->fields.size() ? cache->fields.at(d.field).name : QString()},
            {QStringLiteral("name"), d.name},
            {QStringLiteral("function"), int(d.function)},
            {QStringLiteral("showAs"), int(d.showAs)},
            {QStringLiteral("baseField"), qMax(0, int(fields.indexOf(d.baseField)))},
            {QStringLiteral("baseItem"), baseItem},
            {QStringLiteral("numberFormat"), d.numberFormat.isEmpty() ? QStringLiteral("General") : d.numberFormat},
            {QStringLiteral("functions"), functionNames()},
            {QStringLiteral("showAsNames"), showAsNames()},
            {QStringLiteral("baseFields"), bases}};
}

QStringList CalcHost::pivotBaseItems(int field) const
{
    const PivotTable *pivot = pivotOf(*m_document->workbook(), currentPivot());
    const PivotCache *cache = pivot != nullptr ? pivotCacheOf(*m_document->workbook(), pivot->cacheId) : nullptr;
    QStringList items{tr("(previous)"), tr("(next)")};
    if (cache == nullptr || field < 0 || field >= pivot->fields.size()) {
        return items;
    }
    for (const qint32 item : pivot->fields.at(field).order) {
        items.append(pivotItemCaption(*cache, field, item));
    }
    return items;
}

void CalcHost::applyValueFieldSettings(int dataIndex, const QVariantMap &settings)
{
    changeCurrentPivot(
        [dataIndex, settings](PivotTable &p, const PivotCache &c) {
            if (dataIndex < 0 || dataIndex >= p.dataFields.size()) {
                return;
            }
            PivotDataField &d = p.dataFields[dataIndex];
            const PivotFunction function = PivotFunction(qBound(0, settings.value(QStringLiteral("function")).toInt(), 10));
            QString name = settings.value(QStringLiteral("name")).toString().trimmed();
            // A name Excel made follows the function, the way Excel renames it.
            const QString made = QObject::tr("%1 of %2").arg(pivotFunctionName(d.function),
                                                             d.field >= 0 && d.field < c.fields.size() ? c.fields.at(d.field).name
                                                                                                       : QString());
            if (function != d.function && (name.isEmpty() || name == d.name) && d.name.startsWith(made)) {
                PivotTable others = p;
                others.dataFields.removeAt(dataIndex);
                name = pivotDefaultDataName(c, others, d.field, function);
            }
            d.name = name.isEmpty() ? d.name : name;
            d.function = function;
            d.showAs = PivotShowAs(qBound(0, settings.value(QStringLiteral("showAs")).toInt(), 14));
            const QList<qint32> bases = baseFields(p);
            d.baseField = bases.value(settings.value(QStringLiteral("baseField")).toInt(), -1);
            const int item = settings.value(QStringLiteral("baseItem")).toInt();
            d.baseItem = item == 0 ? kPivotPreviousItem
                : item == 1        ? kPivotNextItem
                                   : (d.baseField >= 0 ? p.fields.at(d.baseField).order.value(item - 2, -1) : -1);
            const QString format = settings.value(QStringLiteral("numberFormat")).toString().trimmed();
            d.numberFormat = format.compare(QLatin1String("General"), Qt::CaseInsensitive) == 0 ? QString() : format;
        },
        tr("Value Field Settings"));
}

QVariantMap CalcHost::pivotFieldState(int field) const
{
    const PivotTable *pivot = pivotOf(*m_document->workbook(), currentPivot());
    const PivotCache *cache = pivot != nullptr ? pivotCacheOf(*m_document->workbook(), pivot->cacheId) : nullptr;
    if (cache == nullptr || field < 0 || field >= pivot->fields.size()) {
        return {};
    }
    const PivotFieldSettings &s = pivot->fields.at(field);
    QVariantList custom;
    for (int fn = 0; fn < 11; ++fn) {
        custom.append(s.customSubtotals.contains(PivotFunction(fn)));
    }
    return {{QStringLiteral("field"), field},
            {QStringLiteral("source"), cache->fields.at(field).name},
            {QStringLiteral("caption"), s.caption.isEmpty() ? cache->fields.at(field).name : s.caption},
            {QStringLiteral("subtotals"), !s.customSubtotals.isEmpty() ? 2 : (s.subtotals ? 0 : 1)},
            {QStringLiteral("custom"), custom},
            {QStringLiteral("functions"), functionNames()},
            {QStringLiteral("subtotalTop"), s.subtotalTop},
            {QStringLiteral("repeatLabels"), s.repeatLabels},
            {QStringLiteral("blankLine"), s.insertBlankRow},
            {QStringLiteral("showAll"), s.showAllItems},
            {QStringLiteral("sort"), int(s.sort)},
            {QStringLiteral("sortBy"), s.sortData + 1},
            {QStringLiteral("dataFields"), dataNames(*pivot)}};
}

void CalcHost::applyFieldSettings(int field, const QVariantMap &settings)
{
    changeCurrentPivot(
        [field, settings](PivotTable &p, const PivotCache &c) {
            if (field < 0 || field >= p.fields.size()) {
                return;
            }
            PivotFieldSettings &s = p.fields[field];
            const QString caption = settings.value(QStringLiteral("caption")).toString().trimmed();
            s.caption = caption == c.fields.at(field).name ? QString() : caption;
            const int subtotals = settings.value(QStringLiteral("subtotals")).toInt();
            s.subtotals = subtotals == 0;
            s.customSubtotals.clear();
            if (subtotals == 2) {
                const QVariantList custom = settings.value(QStringLiteral("custom")).toList();
                for (int fn = 0; fn < custom.size() && fn < 11; ++fn) {
                    if (custom.at(fn).toBool()) {
                        s.customSubtotals.append(PivotFunction(fn));
                    }
                }
            }
            s.subtotalTop = settings.value(QStringLiteral("subtotalTop")).toBool();
            s.repeatLabels = settings.value(QStringLiteral("repeatLabels")).toBool();
            s.insertBlankRow = settings.value(QStringLiteral("blankLine")).toBool();
            s.showAllItems = settings.value(QStringLiteral("showAll")).toBool();
            s.sort = PivotSort(qBound(0, settings.value(QStringLiteral("sort")).toInt(), 2));
            s.sortData = settings.value(QStringLiteral("sortBy")).toInt() - 1;
        },
        tr("Field Settings"));
}

QVariantMap CalcHost::pivotFilterState(int field) const
{
    const PivotTable *pivot = pivotOf(*m_document->workbook(), currentPivot());
    const PivotCache *cache = pivot != nullptr ? pivotCacheOf(*m_document->workbook(), pivot->cacheId) : nullptr;
    if (cache == nullptr || field < 0 || field >= pivot->fields.size()) {
        return {};
    }
    const PivotFieldSettings &s = pivot->fields.at(field);
    const bool page = pivot->pageFields.contains(field);
    QVariantList items;
    for (const qint32 item : s.order) {
        const bool shown = page && !s.multipleSelection && s.pageItem >= 0 ? item == s.pageItem : !s.hidden.contains(item);
        items.append(QVariantMap{{QStringLiteral("item"), item},
                                 {QStringLiteral("caption"), pivotItemCaption(*cache, field, item)},
                                 {QStringLiteral("checked"), shown}});
    }
    QVariantMap out{{QStringLiteral("field"), field},
                    {QStringLiteral("name"), pivotFieldCaption(*cache, *pivot, field)},
                    {QStringLiteral("items"), items},
                    {QStringLiteral("page"), page},
                    {QStringLiteral("kind"), 0},
                    {QStringLiteral("dataFields"), dataNames(*pivot)}};
    for (const PivotFilter &f : s.filters) {
        out.insert(QStringLiteral("kind"), int(f.kind) + 1);
        out.insert(QStringLiteral("op"), int(f.op));
        out.insert(QStringLiteral("text1"), f.kind == PivotFilter::Kind::Label ? f.text1 : QString::number(f.value1));
        out.insert(QStringLiteral("text2"), f.kind == PivotFilter::Kind::Label ? f.text2 : QString::number(f.value2));
        out.insert(QStringLiteral("dataField"), f.dataField);
        out.insert(QStringLiteral("top"), f.top);
        out.insert(QStringLiteral("topBy"), int(f.topBy));
    }
    return out;
}

void CalcHost::applyPivotFilter(int field, const QVariantMap &filter)
{
    changeCurrentPivot(
        [field, filter](PivotTable &p, const PivotCache &) {
            if (field < 0 || field >= p.fields.size()) {
                return;
            }
            PivotFieldSettings &s = p.fields[field];
            const int kind = filter.value(QStringLiteral("kind")).toInt();
            s.filters.clear();
            if (kind == 0) { // the item list
                QSet<qint32> hidden;
                QList<qint32> checked;
                for (const QVariant &entry : filter.value(QStringLiteral("items")).toList()) {
                    const QVariantMap item = entry.toMap();
                    const qint32 index = item.value(QStringLiteral("item")).toInt();
                    if (item.value(QStringLiteral("checked")).toBool()) {
                        checked.append(index);
                    } else {
                        hidden.insert(index);
                    }
                }
                const bool page = p.pageFields.contains(field);
                if (page && checked.size() == 1) {
                    s.pageItem = checked.first(); // a filter showing one item
                    s.multipleSelection = false;
                    s.hidden.clear();
                } else {
                    s.pageItem = -1;
                    s.multipleSelection = page && !hidden.isEmpty();
                    s.hidden = hidden;
                }
                return;
            }
            PivotFilter f;
            f.kind = PivotFilter::Kind(qBound(0, kind - 1, 2));
            f.op = PivotFilter::Op(qBound(0, filter.value(QStringLiteral("op")).toInt(), 13));
            f.text1 = filter.value(QStringLiteral("text1")).toString();
            f.text2 = filter.value(QStringLiteral("text2")).toString();
            f.value1 = f.text1.toDouble();
            f.value2 = f.text2.toDouble();
            f.dataField = qMax(0, filter.value(QStringLiteral("dataField")).toInt());
            f.top = filter.value(QStringLiteral("top"), true).toBool();
            f.topBy = PivotFilter::TopBy(qBound(0, filter.value(QStringLiteral("topBy")).toInt(), 2));
            s.filters = {f};
            s.hidden.clear();
        },
        tr("Filter"));
}

QVariantMap CalcHost::pivotGroupState(int field) const
{
    const PivotTable *pivot = pivotOf(*m_document->workbook(), currentPivot());
    const PivotCache *cache = pivot != nullptr ? pivotCacheOf(*m_document->workbook(), pivot->cacheId) : nullptr;
    if (cache == nullptr || field < 0 || field >= cache->fields.size()) {
        return {};
    }
    const PivotCacheField &f = cache->fields.at(field);
    double lo = std::numeric_limits<double>::max();
    double hi = std::numeric_limits<double>::lowest();
    for (const Value &item : f.items) {
        if (isNumber(item)) {
            lo = qMin(lo, std::get<double>(item));
            hi = qMax(hi, std::get<double>(item));
        }
    }
    const bool dates = f.numberFormat.contains(QLatin1Char('y'), Qt::CaseInsensitive)
        || f.numberFormat.contains(QLatin1Char('d'), Qt::CaseInsensitive);
    const auto text = [dates](double v) {
        return formatWithCode(v, dates ? QStringLiteral("m/d/yyyy") : QStringLiteral("General")).text;
    };
    return {{QStringLiteral("field"), field},
            {QStringLiteral("name"), f.name},
            {QStringLiteral("dates"), dates},
            {QStringLiteral("start"), lo <= hi ? text(dates ? std::floor(lo) : lo) : QString()},
            {QStringLiteral("end"), lo <= hi ? text(dates ? std::floor(hi) + 1 : hi) : QString()},
            {QStringLiteral("interval"), dates ? 1 : 10}};
}

void CalcHost::applyPivotGroup(int field, const QVariantMap &group)
{
    const bool dates = group.value(QStringLiteral("dates")).toBool();
    QList<PivotGroupBy> parts;
    const QVariantList chosen = group.value(QStringLiteral("parts")).toList(); // Seconds … Years
    const PivotGroupBy order[] = {PivotGroupBy::Seconds, PivotGroupBy::Minutes, PivotGroupBy::Hours, PivotGroupBy::Days,
                                  PivotGroupBy::Months, PivotGroupBy::Quarters, PivotGroupBy::Years};
    for (int i = 0; dates && i < chosen.size() && i < 7; ++i) {
        if (chosen.at(i).toBool()) {
            parts.append(order[i]);
        }
    }
    if (!dates) {
        parts = {PivotGroupBy::Range};
    }
    const auto bound = [dates](const QVariant &value) {
        if (!dates) {
            return numberOrNaN(value);
        }
        const QDate date = QDate::fromString(value.toString().trimmed(), QStringLiteral("M/d/yyyy"));
        return date.isValid() ? serialFromDate(date) : std::numeric_limits<double>::quiet_NaN();
    };
    const double start = group.value(QStringLiteral("autoStart")).toBool() ? std::numeric_limits<double>::quiet_NaN()
                                                                           : bound(group.value(QStringLiteral("start")));
    const double end = group.value(QStringLiteral("autoEnd")).toBool() ? std::numeric_limits<double>::quiet_NaN()
                                                                       : bound(group.value(QStringLiteral("end")));
    m_document->groupPivot(currentPivot(), field, parts, start, end, qMax(1e-9, group.value(QStringLiteral("interval")).toDouble()));
    refreshGrid();
    syncFromCursor();
    emit pivotChanged();
}

void CalcHost::groupAtCursor()
{
    const PivotRef ref = currentPivot();
    const PivotTable *pivot = pivotOf(*m_document->workbook(), ref);
    const PivotCache *cache = pivot != nullptr ? pivotCacheOf(*m_document->workbook(), pivot->cacheId) : nullptr;
    const qint32 field = pivotFieldAtCursor();
    if (cache == nullptr || field < 0) {
        showStatusMessage(tr("Put the cursor on a label of the field to group."), 6000);
        return;
    }
    bool numbers = !cache->fields.at(field).grouped() && cache->fields.at(field).database;
    for (const Value &item : cache->fields.at(field).items) {
        numbers = numbers && isNumber(item);
    }
    if (numbers) {
        emit pivotGroupRequested(field); // dates and numbers: the Grouping dialog
        return;
    }
    // Text: the selected items become a group of their own (Group Selection).
    QList<qint32> items;
    const CellRange selected = selection();
    for (qint32 row = selected.topLeft.row; row <= selected.bottomRight.row; ++row) {
        for (qint32 col = selected.topLeft.col; col <= selected.bottomRight.col; ++col) {
            const PivotCellInfo info = pivotCellInfo(*m_document->workbook(), ref, {row, col});
            if (info.label && info.field == field && !items.contains(info.item)) {
                items.append(info.item);
            }
        }
    }
    if (items.isEmpty()) {
        showStatusMessage(tr("Select the labels of the items to group."), 6000);
        return;
    }
    m_document->groupPivotItems(ref, field, items);
    refreshGrid();
    syncFromCursor();
    emit pivotChanged();
}

} // namespace QindaOffice::Apps::QindaCalc
