// SPDX-License-Identifier: GPL-3.0-or-later
// Table Design's Table Name and Resize Table (table_design.h, M90, D-098).

#include "table_design.h"

#include "pivot.h"
#include "sheet.h"
#include "table_edit.h"
#include "table_refs.h"
#include "workbook.h"

#include <QCoreApplication>
#include <QRegularExpression>
#include <QSet>

namespace QindaOffice::Calc {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("Tables", text);
}

bool overlaps(const CellRange &a, const CellRange &b)
{
    return a.topLeft.row <= b.bottomRight.row && a.bottomRight.row >= b.topLeft.row && a.topLeft.col <= b.bottomRight.col
        && a.bottomRight.col >= b.topLeft.col;
}

// The table's column names, header cells standing in for missing ones.
QStringList namesOf(const Workbook &book, qint32 sheet, const TableSpec &table)
{
    QStringList names;
    for (qint32 c = table.range.topLeft.col; c <= table.range.bottomRight.col; ++c) {
        QString name = table.columnNames.value(c - table.range.topLeft.col);
        if (name.isEmpty() && table.headerRow) {
            name = toText(book.cellValue(sheet, table.range.topLeft.row, c));
        }
        names.append(name);
    }
    return names;
}

qint32 indexOf(const QStringList &names, const QString &name)
{
    for (qsizetype i = 0; i < names.size(); ++i) {
        if (names.at(i).compare(name, Qt::CaseInsensitive) == 0) {
            return qint32(i);
        }
    }
    return -1;
}

} // namespace

QString tableNameProblem(const Workbook &book, const QString &name, const QString &current)
{
    const QString invalid = tr("The name that you entered is not valid.");
    if (name.isEmpty() || name.size() > 255) {
        return invalid;
    }
    const QChar first = name.at(0);
    if (!(first.isLetter() || first == QLatin1Char('_') || first == QLatin1Char('\\'))) {
        return invalid + QLatin1Char(' ') + tr("A name begins with a letter, an underscore or a backslash.");
    }
    for (const QChar c : name) {
        if (!(c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.') || c == QLatin1Char('\\'))) {
            return invalid + QLatin1Char(' ') + tr("A name has no spaces or other symbols.");
        }
    }
    static const QRegularExpression r1c1(QStringLiteral("^[Rr][0-9]*[Cc][0-9]*$|^[RrCc]$"));
    const ParseResult parsed = parseFormula(name);
    if (r1c1.match(name).hasMatch() || (parsed.root && parsed.root->kind == Node::Kind::Ref)) {
        return invalid + QLatin1Char(' ') + tr("A name cannot be a cell reference.");
    }
    if (name.compare(current, Qt::CaseInsensitive) == 0) {
        return {};
    }
    qint32 sheet = -1;
    bool taken = book.tableNamed(name, &sheet) != nullptr || book.namedRange(name).has_value();
    for (const DefinedName &defined : book.definedNames()) {
        taken = taken || defined.name.compare(name, Qt::CaseInsensitive) == 0;
    }
    return taken ? tr("The name entered already exists. Enter a unique name.") : QString();
}

bool renameTable(Workbook &book, qint32 sheet, qsizetype index, const QString &name, QString *error)
{
    if (sheet < 0 || sheet >= book.sheetCount() || index < 0 || index >= book.sheetAt(sheet).tables().size()) {
        return false;
    }
    const QString old = book.sheetAt(sheet).tables().at(index).name;
    if (const QString problem = tableNameProblem(book, name, old); !problem.isEmpty()) {
        if (error != nullptr) {
            *error = problem;
        }
        return false;
    }
    // Every reference that names the table follows it; an unqualified
    // [@Sales] inside the table needs nothing.
    static_cast<void>(rewriteStructRefs(book, [&old, &name](const StructRef &ref, const QString &tableName) -> NodePtr {
        if (ref.table.isEmpty() || tableName.compare(old, Qt::CaseInsensitive) != 0) {
            return nullptr;
        }
        StructRef renamed = ref;
        renamed.table = name;
        return Node::makeStructRef(renamed);
    }));
    for (PivotCache &cache : book.pivotCaches) {
        if (cache.source.name.compare(old, Qt::CaseInsensitive) == 0) {
            cache.source.name = name;
        }
    }
    QList<TableSpec> tables = book.sheetAt(sheet).tables();
    tables[index].name = name;
    book.sheetAt(sheet).setTables(tables);
    book.refreshStructuredRefs();
    return true;
}

bool resizeTable(Workbook &book, qint32 sheet, qsizetype index, const CellRange &range, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (sheet < 0 || sheet >= book.sheetCount() || index < 0 || index >= book.sheetAt(sheet).tables().size()) {
        return false;
    }
    const Sheet &where = book.sheetAt(sheet);
    const TableSpec table = where.tables().at(index);
    if (!range.isValid()) {
        return fail(tr("The reference isn't valid."));
    }
    if ((table.headerRow && range.topLeft.row != table.range.topLeft.row) || !overlaps(range, table.range)) {
        return fail(tr("The headers must remain in the same row and the resulting table range must overlap the "
                       "original table range."));
    }
    if (table.headerRow && range.bottomRight.row <= range.topLeft.row) {
        return fail(tr("A table needs a header row and at least one row of data."));
    }
    if (table.totalsRow && range.bottomRight.row != table.range.bottomRight.row) {
        return fail(tr("Turn off the Total Row before you change the table's rows."));
    }
    for (qsizetype t = 0; t < where.tables().size(); ++t) {
        if (t != index && overlaps(where.tables().at(t).range, range)) {
            return fail(tr("A table cannot overlap another table."));
        }
    }
    for (const CellRange &merge : where.merges()) {
        if (overlaps(merge, range)) {
            return fail(tr("A table cannot contain merged cells."));
        }
    }
    for (qint32 r = range.topLeft.row; r <= range.bottomRight.row; ++r) {
        for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
            const Cell *cell = where.cellAt({r, c});
            if (!table.range.contains({r, c}) && cell != nullptr
                && (cell->spillAnchor.isValid() || cell->arrayRange.isValid() || book.spillAt(sheet, {r, c}).has_value())) {
                return fail(tr("A table cannot contain an array formula or a spill."));
            }
        }
    }
    // Columns that leave: references to them are #REF! (a column range
    // keeps what remains of it).
    const QStringList names = namesOf(book, sheet, table);
    QSet<qint32> gone;
    for (qint32 i = 0; i < names.size(); ++i) {
        const qint32 col = table.range.topLeft.col + i;
        if (col < range.topLeft.col || col > range.bottomRight.col) {
            gone.insert(i);
        }
    }
    if (!gone.isEmpty()) {
        static_cast<void>(rewriteStructRefs(book, [&](const StructRef &ref, const QString &tableName) -> NodePtr {
            if (tableName.compare(table.name, Qt::CaseInsensitive) != 0 || ref.firstColumn.isEmpty()) {
                return nullptr;
            }
            const qint32 a = indexOf(names, ref.firstColumn);
            const qint32 b = ref.lastColumn.isEmpty() ? a : indexOf(names, ref.lastColumn);
            if (a < 0 || b < 0 || (!gone.contains(a) && !gone.contains(b))) {
                return nullptr;
            }
            qint32 first = -1;
            qint32 last = -1;
            for (qint32 i = qMin(a, b); i <= qMax(a, b); ++i) {
                if (!gone.contains(i)) {
                    first = first < 0 ? i : first;
                    last = i;
                }
            }
            if (first < 0) {
                return Node::makeError(Err::Ref);
            }
            StructRef narrowed = ref;
            narrowed.firstColumn = names.at(first);
            narrowed.lastColumn = first == last ? QString() : names.at(last);
            return Node::makeStructRef(narrowed);
        }));
    }
    QList<TableSpec> tables = book.sheetAt(sheet).tables();
    TableSpec resized = tables.at(index);
    resized.range = range;
    resized.columnNames.clear();
    resized.calculatedColumns.clear();
    for (qint32 c = range.topLeft.col; c <= range.bottomRight.col; ++c) {
        const qint32 old = c - table.range.topLeft.col;
        const bool kept = c >= table.range.topLeft.col && c <= table.range.bottomRight.col;
        resized.columnNames.append(kept ? names.value(old) : QString());
        resized.calculatedColumns.append(kept ? tables.at(index).calculatedColumns.value(old) : QString());
    }
    tables[index] = resized;
    book.sheetAt(sheet).setTables(tables);
    static_cast<void>(syncTableHeaders(book, sheet)); // new columns are named Column<N>
    // Rows that join the table get its calculated columns.
    QList<qint32> added;
    const qint32 firstData = range.topLeft.row + (resized.headerRow ? 1 : 0);
    const qint32 lastData = range.bottomRight.row - (resized.totalsRow ? 1 : 0);
    for (qint32 r = qMax(firstData, table.range.bottomRight.row + 1); r <= lastData; ++r) {
        added.append(r);
    }
    if (!added.isEmpty() && resized.calculatedColumns.join(QString()).size() > 0) {
        fillCalculatedCells(book, sheet, book.sheetAt(sheet).tables().at(index), added, -1);
    }
    book.refreshStructuredRefs();
    return true;
}

} // namespace QindaOffice::Calc
