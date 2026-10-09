// SPDX-License-Identifier: GPL-3.0-or-later
// Structured references (structured_ref.h, M69, D-078): parsing both of
// Excel's spellings, printing either, and resolving a reference to cells.
//
// AGENT-NOTE: the grammar is ECMA-376 Part 1 §18.17.6.11 ("structure-
// reference") plus the formula bar's '@' shorthand. Inside brackets a
// single quote escapes the next character ('[ '] '# '' and, at the start of
// a name, '@); everything else, spaces and commas included, is part of a
// column name. Excel matches table and column names without regard to
// case.

#include "structured_ref.h"

#include "functions/functions.h"
#include "sheet_rules.h"

namespace QindaOffice::Calc {

namespace {

bool isTableNameChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.') || c == QLatin1Char('\\');
}

void skipSpaces(QStringView text, qsizetype &pos)
{
    while (pos < text.size() && text.at(pos).isSpace()) {
        ++pos;
    }
}

// The content of the bracket group that opens at text[pos], escapes kept;
// pos moves past its ']'. nullopt when the group never closes.
std::optional<QString> readGroup(QStringView text, qsizetype &pos)
{
    if (pos >= text.size() || text.at(pos) != QLatin1Char('[')) {
        return std::nullopt;
    }
    int depth = 0;
    const qsizetype start = pos + 1;
    for (qsizetype i = pos; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\'')) {
            ++i; // the escaped character is part of a name
            continue;
        }
        if (c == QLatin1Char('[')) {
            ++depth;
        } else if (c == QLatin1Char(']') && --depth == 0) {
            pos = i + 1;
            return text.mid(start, i - start).toString();
        }
    }
    return std::nullopt;
}

// A column name with its escapes resolved; nullopt when an unescaped
// bracket says it is not a name.
std::optional<QString> columnName(QStringView raw)
{
    QString out;
    const QStringView text = raw.trimmed();
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\'') && i + 1 < text.size()) {
            out += text.at(++i);
        } else if (c == QLatin1Char('[') || c == QLatin1Char(']')) {
            return std::nullopt;
        } else {
            out += c;
        }
    }
    return out.isEmpty() ? std::nullopt : std::optional<QString>(out);
}

std::optional<quint8> keywordBit(QStringView word)
{
    const QString w = word.trimmed().toString().remove(QLatin1Char(' '));
    if (w.compare(QLatin1String("#All"), Qt::CaseInsensitive) == 0) {
        return TableItem::All;
    }
    if (w.compare(QLatin1String("#Data"), Qt::CaseInsensitive) == 0) {
        return TableItem::Data;
    }
    if (w.compare(QLatin1String("#Headers"), Qt::CaseInsensitive) == 0) {
        return TableItem::Headers;
    }
    if (w.compare(QLatin1String("#Totals"), Qt::CaseInsensitive) == 0) {
        return TableItem::Totals;
    }
    if (w.compare(QLatin1String("#ThisRow"), Qt::CaseInsensitive) == 0) {
        return TableItem::ThisRow;
    }
    return std::nullopt;
}

bool validItems(quint8 items)
{
    using namespace TableItem;
    switch (items) {
    case 0:
    case Data:
    case Headers:
    case Totals:
    case All:
    case ThisRow:
    case Headers | Data:
    case Data | Totals:
        return true;
    default:
        return false;
    }
}

// [A] or [A]:[B] at pos (the column part of a list, or after '@').
bool readColumns(QStringView text, qsizetype &pos, StructRef *ref)
{
    const std::optional<QString> first = readGroup(text, pos);
    const std::optional<QString> firstName = first ? columnName(*first) : std::nullopt;
    if (!firstName) {
        return false;
    }
    ref->firstColumn = *firstName;
    skipSpaces(text, pos);
    if (pos < text.size() && text.at(pos) == QLatin1Char(':')) {
        ++pos;
        skipSpaces(text, pos);
        const std::optional<QString> last = readGroup(text, pos);
        const std::optional<QString> lastName = last ? columnName(*last) : std::nullopt;
        if (!lastName) {
            return false;
        }
        ref->lastColumn = *lastName;
    }
    return true;
}

// The inner list: [#Headers],[#Data],[Sales]:[Cost] — keywords first, then
// at most one column part.
bool readList(QStringView list, StructRef *ref)
{
    qsizetype pos = 0;
    bool columns = false;
    while (true) {
        skipSpaces(list, pos);
        if (pos >= list.size()) {
            return false; // a trailing comma, or nothing at all
        }
        qsizetype probe = pos;
        const std::optional<QString> group = readGroup(list, probe);
        if (!group) {
            return false;
        }
        if (QStringView(*group).trimmed().startsWith(QLatin1Char('#'))) {
            const std::optional<quint8> bit = keywordBit(*group);
            if (columns || !bit || (ref->items & *bit) != 0) {
                return false;
            }
            ref->items |= *bit;
            pos = probe;
        } else {
            if (columns || !readColumns(list, pos, ref)) {
                return false;
            }
            columns = true;
        }
        skipSpaces(list, pos);
        if (pos >= list.size()) {
            return validItems(ref->items);
        }
        if (list.at(pos) != QLatin1Char(',')) {
            return false;
        }
        ++pos;
    }
}

// Characters that make Excel bracket a column name in the simple form
// (support.microsoft.com, "Using structured references with Excel tables").
// The ones that are escaped instead ([ ] # ' and a leading @) are not
// among them: Microsoft's own example is DeptSalesFYSummary['#OfItems].
// The '@' form brackets a name with a space too ([@[Unit Price]]).
bool needsInnerBrackets(const QString &name, bool atForm)
{
    static const QString kSpecial = QStringLiteral("\t\n\r,:.\"{}$^&*+=-><\\/!()%?`;~");
    if (name != name.trimmed() || (atForm && name.contains(QLatin1Char(' ')))) {
        return true;
    }
    for (const QChar c : name) {
        if (kSpecial.contains(c)) {
            return true;
        }
    }
    return false;
}

QString escaped(const QString &name)
{
    QString out;
    for (qsizetype i = 0; i < name.size(); ++i) {
        const QChar c = name.at(i);
        if (c == QLatin1Char('[') || c == QLatin1Char(']') || c == QLatin1Char('#') || c == QLatin1Char('\'')
            || (i == 0 && c == QLatin1Char('@'))) {
            out += QLatin1Char('\'');
        }
        out += c;
    }
    return out;
}

QString columnGroup(const StructRef &ref)
{
    QString out = QLatin1Char('[') + escaped(ref.firstColumn) + QLatin1Char(']');
    if (!ref.lastColumn.isEmpty()) {
        out += QStringLiteral(":[") + escaped(ref.lastColumn) + QLatin1Char(']');
    }
    return out;
}

QStringList keywordParts(quint8 items)
{
    QStringList parts;
    if (items & TableItem::All) {
        parts.append(QStringLiteral("[#All]"));
    }
    if (items & TableItem::Headers) {
        parts.append(QStringLiteral("[#Headers]"));
    }
    if (items & TableItem::Data) {
        parts.append(QStringLiteral("[#Data]"));
    }
    if (items & TableItem::Totals) {
        parts.append(QStringLiteral("[#Totals]"));
    }
    if (items & TableItem::ThisRow) {
        parts.append(QStringLiteral("[#This Row]"));
    }
    return parts;
}

// Everything after the table name, the way the list form writes it.
QString listBody(const StructRef &ref)
{
    if (ref.emptyBrackets || (ref.items == 0 && ref.firstColumn.isEmpty())) {
        return QStringLiteral("[]");
    }
    const QStringList keywords = keywordParts(ref.items);
    if (keywords.isEmpty() && ref.lastColumn.isEmpty()) {
        return needsInnerBrackets(ref.firstColumn, false)
            ? QStringLiteral("[[") + escaped(ref.firstColumn) + QStringLiteral("]]")
            : QLatin1Char('[') + escaped(ref.firstColumn) + QLatin1Char(']');
    }
    if (ref.firstColumn.isEmpty() && keywords.size() == 1) {
        return keywords.first();
    }
    QStringList parts = keywords;
    if (!ref.firstColumn.isEmpty()) {
        parts.append(columnGroup(ref));
    }
    return QLatin1Char('[') + parts.join(QLatin1Char(',')) + QLatin1Char(']');
}

} // namespace

std::optional<StructRef> parseStructRef(const QString &text)
{
    const qsizetype open = text.indexOf(QLatin1Char('['));
    if (open < 0) {
        return std::nullopt;
    }
    StructRef ref;
    ref.table = text.left(open);
    for (const QChar c : std::as_const(ref.table)) {
        if (!isTableNameChar(c)) {
            return std::nullopt;
        }
    }
    qsizetype pos = open;
    const std::optional<QString> outer = readGroup(text, pos);
    if (!outer || pos != text.size()) {
        return std::nullopt;
    }
    const QStringView body = QStringView(*outer).trimmed();
    if (body.isEmpty()) {
        ref.emptyBrackets = true;
        return ref;
    }
    if (body.startsWith(QLatin1Char('#'))) {
        const std::optional<quint8> bit = keywordBit(body);
        if (!bit) {
            return std::nullopt;
        }
        ref.items = *bit;
        return ref;
    }
    if (body.startsWith(QLatin1Char('@'))) {
        // The formula bar's this-row shorthand: [@], [@Sales], [@[Unit
        // Price]], [@[Jan]:[Mar]].
        ref.items = TableItem::ThisRow;
        const QStringView rest = body.mid(1).trimmed();
        if (rest.isEmpty()) {
            return ref;
        }
        if (rest.startsWith(QLatin1Char('['))) {
            qsizetype at = 0;
            if (!readColumns(rest, at, &ref)) {
                return std::nullopt;
            }
            skipSpaces(rest, at);
            return at == rest.size() ? std::optional<StructRef>(ref) : std::nullopt;
        }
        const std::optional<QString> name = columnName(rest);
        if (!name) {
            return std::nullopt;
        }
        ref.firstColumn = *name;
        return ref;
    }
    if (body.startsWith(QLatin1Char('['))) {
        if (!readList(body, &ref)) {
            return std::nullopt;
        }
        return ref;
    }
    const std::optional<QString> name = columnName(body);
    if (!name) {
        return std::nullopt;
    }
    ref.firstColumn = *name;
    return ref;
}

QString structRefText(const StructRef &ref, StructRefSpelling spelling, const QString &containingTable)
{
    if (spelling == StructRefSpelling::File) {
        const QString table = ref.table.isEmpty() ? containingTable : ref.table;
        if (table.isEmpty()) {
            return errText(Err::Ref);
        }
        return table + listBody(ref);
    }
    if (ref.items == TableItem::ThisRow) {
        if (ref.firstColumn.isEmpty()) {
            return ref.table + QStringLiteral("[@]");
        }
        if (ref.lastColumn.isEmpty() && !needsInnerBrackets(ref.firstColumn, true)) {
            return ref.table + QStringLiteral("[@") + escaped(ref.firstColumn) + QLatin1Char(']');
        }
        return ref.table + QStringLiteral("[@") + columnGroup(ref) + QLatin1Char(']');
    }
    return ref.table + listBody(ref);
}

qint32 tableColumnIndex(const TableSpec &table, const QString &name, const EvalHost &host, qint32 sheet)
{
    const qint32 width = table.range.bottomRight.col - table.range.topLeft.col + 1;
    const QString wanted = name.trimmed();
    for (qint32 i = 0; i < width; ++i) {
        QString column = table.columnNames.value(i);
        if (column.isEmpty() && table.headerRow) {
            column = toText(host.cellValue(sheet, table.range.topLeft.row, table.range.topLeft.col + i));
        }
        if (column.trimmed().compare(wanted, Qt::CaseInsensitive) == 0) {
            return i;
        }
    }
    return -1;
}

CellRange tableDataRows(const TableSpec &table)
{
    CellRange rows = table.range;
    rows.topLeft.row += table.headerRow ? 1 : 0;
    rows.bottomRight.row -= table.totalsRow ? 1 : 0;
    return rows;
}

StructTarget resolveStructRef(const StructRef &ref, const EvalHost &host, qint32 sheet, const CellAddr &here)
{
    StructTarget out;
    qint32 tableSheet = sheet;
    const TableSpec *table = ref.table.isEmpty() ? host.tableAt(sheet, here) : host.tableNamed(ref.table, &tableSheet);
    if (table == nullptr || !table->range.isValid()) {
        return out;
    }
    const CellRange &whole = table->range;
    qint32 c0 = whole.topLeft.col;
    qint32 c1 = whole.bottomRight.col;
    if (!ref.firstColumn.isEmpty()) {
        const qint32 a = tableColumnIndex(*table, ref.firstColumn, host, tableSheet);
        const qint32 b = ref.lastColumn.isEmpty() ? a : tableColumnIndex(*table, ref.lastColumn, host, tableSheet);
        if (a < 0 || b < 0) {
            return out;
        }
        c0 = whole.topLeft.col + qMin(a, b);
        c1 = whole.topLeft.col + qMax(a, b);
    }
    const CellRange data = tableDataRows(*table);
    const qint32 header = table->headerRow ? whole.topLeft.row : -1;
    const qint32 totals = table->totalsRow ? whole.bottomRight.row : -1;
    qint32 r0 = data.topLeft.row;
    qint32 r1 = data.bottomRight.row;
    switch (ref.items) {
    case 0:
    case TableItem::Data:
        break;
    case TableItem::All:
        r0 = whole.topLeft.row;
        r1 = whole.bottomRight.row;
        break;
    case TableItem::Headers:
        r0 = r1 = header;
        break;
    case TableItem::Totals:
        r0 = r1 = totals;
        break;
    case TableItem::Headers | TableItem::Data:
        r0 = header >= 0 ? header : data.topLeft.row;
        break;
    case TableItem::Data | TableItem::Totals:
        r1 = whole.bottomRight.row;
        break;
    case TableItem::ThisRow:
        // The formula's own row, when it is one of the table's data rows
        // (on any sheet: Excel intersects by row number).
        if (here.row < data.topLeft.row || here.row > data.bottomRight.row) {
            out.error = Err::Value;
            return out;
        }
        r0 = r1 = here.row;
        break;
    default:
        return out;
    }
    if (r0 < 0 || r1 < r0) {
        return out; // a part the table does not have
    }
    out.sheet = tableSheet;
    out.range = {{r0, c0}, {r1, c1}};
    return out;
}

} // namespace QindaOffice::Calc
