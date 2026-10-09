// SPDX-License-Identifier: GPL-3.0-or-later
// PivotCaches (pivot.h, pivot_cache.h; M74, D-083): the items a field
// shows, records through groupings, Excel's natural item order and
// captions, the fingerprints a save compares, and reading a source range
// or table into a cache.

#include "pivot_cache.h"

#include "number_format.h"
#include "sheet.h"
#include "workbook.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QObject>

#include <algorithm>

namespace QindaOffice::Calc {

namespace {

// The shared item a value is: text without regard to case (Excel merges
// "East" and "east" into the first spelling it met), numbers by value.
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

int typeRank(const Value &value)
{
    if (isNumber(value)) {
        return 0;
    }
    if (isText(value)) {
        return 1;
    }
    if (isBool(value)) {
        return 2;
    }
    return isErr(value) ? 3 : 4;
}

void writeValue(QDataStream &out, const Value &value)
{
    out << qint32(value.index());
    if (const auto *n = std::get_if<double>(&value)) {
        out << *n;
    } else if (const auto *t = std::get_if<QString>(&value)) {
        out << *t;
    } else if (const auto *b = std::get_if<bool>(&value)) {
        out << *b;
    } else if (const auto *e = std::get_if<Err>(&value)) {
        out << qint32(*e);
    }
}

void writeRange(QDataStream &out, const CellRange &range)
{
    out << range.topLeft.row << range.topLeft.col << range.bottomRight.row << range.bottomRight.col;
}

QByteArray digest(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
}

} // namespace

QList<Value> pivotDisplayItems(const PivotCache &cache, qint32 field)
{
    if (field < 0 || field >= cache.fields.size()) {
        return {};
    }
    const PivotCacheField &f = cache.fields.at(field);
    if (f.grouped()) {
        QList<Value> names;
        names.reserve(f.group.names.size());
        for (const QString &name : f.group.names) {
            names.append(name);
        }
        return names;
    }
    return f.items;
}

QVector<qint32> pivotRecordItems(const PivotCache &cache, qint32 field)
{
    if (field < 0 || field >= cache.fields.size()) {
        return {};
    }
    const PivotCacheField &f = cache.fields.at(field);
    if (!f.grouped()) {
        return f.database ? f.records : QVector<qint32>(cache.recordCount, 0);
    }
    // A grouping reads its base field's RAW values (Years over a Date
    // field grouped by months still groups the dates).
    const qint32 base = f.group.base >= 0 && f.group.base < cache.fields.size() ? f.group.base : field;
    const PivotCacheField &b = cache.fields.at(base);
    QVector<qint32> out(cache.recordCount, 0);
    // One group index per shared item, then one lookup per record.
    QVector<qint32> perItem(b.items.size(), 0);
    for (qsizetype i = 0; i < b.items.size(); ++i) {
        perItem[i] = pivotGroupIndex(f.group, b.items.at(i), qint32(i));
    }
    for (qint32 r = 0; r < cache.recordCount && r < b.records.size(); ++r) {
        const qint32 raw = b.records.at(r);
        out[r] = raw >= 0 && raw < perItem.size() ? perItem.at(raw) : 0;
    }
    return out;
}

Value pivotRecordValue(const PivotCache &cache, qint32 field, qint32 record)
{
    if (field < 0 || field >= cache.fields.size()) {
        return {};
    }
    const PivotCacheField &f = cache.fields.at(field);
    if (f.database && !f.grouped()) {
        const qint32 item = record >= 0 && record < f.records.size() ? f.records.at(record) : -1;
        return item >= 0 && item < f.items.size() ? f.items.at(item) : Value{};
    }
    if (f.database && f.grouped() && (f.group.base < 0 || f.group.base == field)) {
        // A data field over a field grouped in place sums the raw values.
        const qint32 item = record >= 0 && record < f.records.size() ? f.records.at(record) : -1;
        return item >= 0 && item < f.items.size() ? f.items.at(item) : Value{};
    }
    const QVector<qint32> items = pivotRecordItems(cache, field);
    const qint32 item = record >= 0 && record < items.size() ? items.at(record) : -1;
    return item >= 0 && item < f.group.names.size() ? Value(f.group.names.at(item)) : Value{};
}

QList<qint32> pivotNaturalOrder(const PivotCache &cache, qint32 field)
{
    const QList<Value> items = pivotDisplayItems(cache, field);
    QList<qint32> order;
    order.reserve(items.size());
    for (qsizetype i = 0; i < items.size(); ++i) {
        order.append(qint32(i));
    }
    if (field >= 0 && field < cache.fields.size() && cache.fields.at(field).grouped()) {
        return order; // Jan before Feb, "<start" first: the group order
    }
    std::stable_sort(order.begin(), order.end(), [&items](qint32 a, qint32 b) {
        const Value &va = items.at(a);
        const Value &vb = items.at(b);
        const int ra = typeRank(va);
        const int rb = typeRank(vb);
        if (ra != rb) {
            return ra < rb;
        }
        if (ra == 1) {
            return std::get<QString>(va).compare(std::get<QString>(vb), Qt::CaseInsensitive) < 0;
        }
        return ra == 4 ? false : compareValues(va, vb) < 0;
    });
    return order;
}

QString pivotItemCaption(const PivotCache &cache, qint32 field, qint32 item)
{
    const QList<Value> items = pivotDisplayItems(cache, field);
    if (item < 0 || item >= items.size()) {
        return {};
    }
    const Value &value = items.at(item);
    if (isBlank(value)) {
        return QObject::tr("(blank)");
    }
    if (const auto *n = std::get_if<double>(&value)) {
        const QString format = cache.fields.at(field).numberFormat;
        return formatWithCode(*n, format.isEmpty() ? QStringLiteral("General") : format).text;
    }
    return toText(value);
}

QString pivotFieldCaption(const PivotCache &cache, const PivotTable &pivot, qint32 field)
{
    if (field == kPivotValuesField) {
        return pivot.options.dataCaption.isEmpty() ? QObject::tr("Values") : pivot.options.dataCaption;
    }
    if (field >= 0 && field < pivot.fields.size() && !pivot.fields.at(field).caption.isEmpty()) {
        return pivot.fields.at(field).caption;
    }
    return field >= 0 && field < cache.fields.size() ? cache.fields.at(field).name : QString();
}

QString pivotFunctionName(PivotFunction function)
{
    switch (function) {
    case PivotFunction::Sum: return QObject::tr("Sum");
    case PivotFunction::Count: return QObject::tr("Count");
    case PivotFunction::Average: return QObject::tr("Average");
    case PivotFunction::Max: return QObject::tr("Max");
    case PivotFunction::Min: return QObject::tr("Min");
    case PivotFunction::Product: return QObject::tr("Product");
    case PivotFunction::CountNums: return QObject::tr("Count");
    case PivotFunction::StdDev: return QObject::tr("StdDev");
    case PivotFunction::StdDevP: return QObject::tr("StdDevp");
    case PivotFunction::Var: return QObject::tr("Var");
    case PivotFunction::VarP: return QObject::tr("Varp");
    }
    return QObject::tr("Sum");
}

QString pivotDefaultDataName(const PivotCache &cache, const PivotTable &pivot, qint32 field, PivotFunction function)
{
    const QString source = field >= 0 && field < cache.fields.size() ? cache.fields.at(field).name : QString();
    const QString base = QObject::tr("%1 of %2").arg(pivotFunctionName(function), source);
    // Excel numbers a repeat: "Sum of Sales2". A data field's name may not
    // be a source field's either.
    const auto taken = [&](const QString &name) {
        for (const PivotDataField &data : pivot.dataFields) {
            if (data.name.compare(name, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        for (const PivotCacheField &f : cache.fields) {
            if (f.name.compare(name, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        return false;
    };
    QString name = base;
    for (int n = 2; taken(name); ++n) {
        name = base + QString::number(n);
    }
    return name;
}

QByteArray pivotCacheFingerprint(const PivotCache &cache)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << cache.source.sheet << cache.source.name << cache.recordCount << qint32(cache.fields.size());
    writeRange(out, cache.source.range);
    for (const PivotCacheField &f : cache.fields) {
        out << f.name << f.numberFormat << f.database << f.records << qint32(f.items.size());
        for (const Value &item : f.items) {
            writeValue(out, item);
        }
        const PivotGroup &g = f.group;
        out << qint32(g.by) << g.base << g.start << g.end << g.interval << g.autoStart << g.autoEnd << g.discrete
            << g.names;
    }
    return digest(bytes);
}

QByteArray pivotTableFingerprint(const PivotTable &pivot)
{
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out << pivot.name << pivot.cacheId << pivot.rowFields << pivot.colFields << pivot.pageFields;
    for (const PivotFieldSettings &f : pivot.fields) {
        QList<qint32> hidden = f.hidden.values();
        std::sort(hidden.begin(), hidden.end());
        out << f.caption << f.order << hidden << qint32(f.sort) << f.sortData << f.subtotals << f.subtotalTop
            << f.insertBlankRow << f.repeatLabels << f.showAllItems << f.multipleSelection << f.pageItem;
        for (const PivotFunction fn : f.customSubtotals) {
            out << qint32(fn);
        }
        for (const PivotFilter &filter : f.filters) {
            out << qint32(filter.kind) << qint32(filter.op) << filter.text1 << filter.text2 << filter.value1
                << filter.value2 << filter.dataField << filter.top << qint32(filter.topBy);
        }
    }
    for (const PivotDataField &d : pivot.dataFields) {
        out << d.name << d.field << qint32(d.function) << qint32(d.showAs) << d.baseField << d.baseItem
            << d.numberFormat;
    }
    const PivotOptions &o = pivot.options;
    out << qint32(o.form) << o.rowGrandTotals << o.colGrandTotals << o.showMissing << o.missingCaption
        << o.showHeaders << o.rowHeaderCaption << o.colHeaderCaption << o.grandTotalCaption << o.dataCaption
        << o.style.name << o.style.rowHeaders << o.style.colHeaders << o.style.rowStripes << o.style.colStripes
        << o.style.lastColumn;
    return digest(bytes);
}

std::optional<std::pair<qint32, CellRange>> pivotSourceRange(const Workbook &book, const PivotSource &source)
{
    if (!source.name.isEmpty()) {
        qint32 sheet = -1;
        if (const TableSpec *table = book.tableNamed(source.name, &sheet); table != nullptr && sheet >= 0) {
            CellRange range = table->range;
            if (table->totalsRow) {
                range.bottomRight.row -= 1;
            }
            return std::make_pair(sheet, range);
        }
        if (const auto named = book.namedRange(source.name)) {
            const qint32 sheet = book.sheetIndex(unquotedSheetName(named->sheet));
            if (sheet >= 0 && named->range.isValid()) {
                return std::make_pair(sheet, named->range);
            }
        }
        return std::nullopt;
    }
    const qint32 sheet = book.sheetIndex(unquotedSheetName(source.sheet));
    if (sheet < 0 || !source.range.isValid() || book.sheetAt(sheet).isChartSheet()) {
        return std::nullopt;
    }
    return std::make_pair(sheet, source.range);
}

std::optional<PivotCache> readPivotSource(const Workbook &book, const PivotSource &source, QString *error)
{
    const auto where = pivotSourceRange(book, source);
    if (!where.has_value()) {
        if (error != nullptr) {
            *error = QObject::tr("Reference isn't valid: the PivotTable's source data is gone.");
        }
        return std::nullopt;
    }
    const auto &[sheetIndex, range] = *where;
    const Sheet &sheet = book.sheetAt(sheetIndex);
    PivotCache cache;
    cache.source = source;
    const qint32 rows = range.bottomRight.row - range.topLeft.row; // data rows under the header
    cache.recordCount = qMax(0, rows);
    QSet<QString> names;
    for (qint32 col = range.topLeft.col; col <= range.bottomRight.col; ++col) {
        const Value header = book.cellValue(sheetIndex, range.topLeft.row, col);
        QString name = isBlank(header) ? QString() : toText(header).trimmed();
        if (const auto *n = std::get_if<double>(&header)) {
            const CellStyle *style = book.style(sheet.styleIdAt({range.topLeft.row, col}));
            name = formatWithCode(*n, style != nullptr ? style->numberFormat : QStringLiteral("General")).text;
        }
        if (name.isEmpty()) {
            if (error != nullptr) {
                *error = QObject::tr("The PivotTable field name is not valid. To create a PivotTable report, you "
                                     "must use data that is organized as a list with labeled columns.");
            }
            return std::nullopt;
        }
        // Excel numbers a repeated heading: Sales, Sales2.
        QString unique = name;
        for (int n = 2; names.contains(unique.toCaseFolded()); ++n) {
            unique = name + QString::number(n);
        }
        names.insert(unique.toCaseFolded());
        PivotCacheField field;
        field.name = unique;
        field.records.reserve(cache.recordCount);
        QHash<QString, qint32> index;
        for (qint32 r = 0; r < cache.recordCount; ++r) {
            const qint32 row = range.topLeft.row + 1 + r;
            const Value value = book.cellValue(sheetIndex, row, col);
            if (field.numberFormat.isEmpty() && isNumber(value)) {
                const CellStyle *style = book.style(sheet.styleIdAt({row, col}));
                field.numberFormat = style != nullptr && style->numberFormat != QLatin1String("General")
                    ? style->numberFormat
                    : QStringLiteral("General");
            }
            const QString key = itemKey(value);
            auto found = index.constFind(key);
            if (found == index.constEnd()) {
                found = index.insert(key, qint32(field.items.size()));
                field.items.append(value);
            }
            field.records.append(found.value());
        }
        if (field.numberFormat.isEmpty() || field.numberFormat == QLatin1String("General")) {
            field.numberFormat.clear();
        }
        cache.fields.append(field);
    }
    return cache;
}

bool pivotCacheCurrent(const Workbook &book, const PivotCache &cache)
{
    if (!cache.modelled) {
        return true;
    }
    const std::optional<PivotCache> fresh = readPivotSource(book, cache.source);
    if (!fresh.has_value()) {
        return false;
    }
    if (fresh->recordCount != cache.recordCount) {
        return false;
    }
    qsizetype database = 0;
    for (const PivotCacheField &field : cache.fields) {
        database += field.database ? 1 : 0;
    }
    if (database != fresh->fields.size()) {
        return false;
    }
    for (qsizetype f = 0, d = 0; f < cache.fields.size(); ++f) {
        const PivotCacheField &field = cache.fields.at(f);
        if (!field.database) {
            continue;
        }
        const PivotCacheField &now = fresh->fields.at(d++);
        if (now.name.compare(field.name, Qt::CaseInsensitive) != 0) {
            return false;
        }
        for (qint32 r = 0; r < cache.recordCount; ++r) {
            const Value a = pivotRecordValue(*fresh, qint32(d - 1), r);
            const qint32 item = r < field.records.size() ? field.records.at(r) : -1;
            const Value b = item >= 0 && item < field.items.size() ? field.items.at(item) : Value{};
            if (itemKey(a) != itemKey(b)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace QindaOffice::Calc
