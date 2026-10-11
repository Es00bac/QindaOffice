// SPDX-License-Identifier: GPL-3.0-or-later
// The formula bar's completion (M89, D-097): functions, defined names,
// tables and a table's columns for the name at the cursor. See
// formula_completion.h for the contract.
#include "formula_completion.h"

#include "functions/functions.h"
#include "sheet.h"
#include "sheet_rules.h"
#include "table_refs.h"

#include <QCollator>
#include <QFile>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace QindaOffice::Calc {

namespace {

// Engine-only functions Excel shows as operators (@ and #), never typed.
const QSet<QString> kHidden{QStringLiteral("SINGLE"), QStringLiteral("ANCHORARRAY")};

bool isNameChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('.') || c == QLatin1Char('\\');
}

bool startsWithCi(const QString &text, const QString &prefix)
{
    return text.startsWith(prefix, Qt::CaseInsensitive);
}

const QHash<QString, QString> &signatures()
{
    static const QHash<QString, QString> table = [] {
        QHash<QString, QString> out;
        QFile file(QStringLiteral(":/qindaoffice/calc/function_signatures.txt"));
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            for (const QString &raw : QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'))) {
                const QString line = raw.trimmed();
                const qsizetype open = line.indexOf(QLatin1Char('('));
                if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')) && open > 0) {
                    out.insert(line.left(open), line);
                }
            }
        }
        return out;
    }();
    return table;
}

// The start of the name that ends at `end` (exclusive), or `end` when none.
int nameStart(const QString &text, int end)
{
    int i = end;
    while (i > 0 && isNameChar(text.at(i - 1))) {
        --i;
    }
    return i;
}

QString escapedColumn(const QString &name)
{
    QString out;
    for (const QChar c : name) {
        if (c == QLatin1Char('\'') || c == QLatin1Char('#') || c == QLatin1Char('[') || c == QLatin1Char(']')) {
            out.append(QLatin1Char('\''));
        }
        out.append(c);
    }
    return out;
}

bool needsBrackets(const QString &name)
{
    return std::any_of(name.cbegin(), name.cend(), [](QChar c) { return !isNameChar(c); });
}

const TableSpec *tableByName(const Workbook &book, const QString &name, qint32 *sheetOut)
{
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        for (const TableSpec &t : book.sheetAt(s).tables()) {
            if (t.name.compare(name, Qt::CaseInsensitive) == 0) {
                *sheetOut = s;
                return &t;
            }
        }
    }
    return nullptr;
}

QString columnName(const Workbook &book, qint32 sheet, const TableSpec &t, int i)
{
    const QString named = i < t.columnNames.size() ? t.columnNames.at(i) : QString();
    if (!named.isEmpty() || !t.headerRow) {
        return named;
    }
    const Cell *header = book.sheetAt(sheet).cellAt(CellAddr{t.range.topLeft.row, t.range.topLeft.col + i});
    return header != nullptr ? toText(header->value) : QString();
}

// Inside a structured reference: the table's columns and special items.
CompletionContext columnCompletions(const Workbook &book, qint32 sheet, const CellAddr &at, const QString &text,
                                    int cursor, int outer, int inner)
{
    CompletionContext out;
    const int tableStart = nameStart(text, outer);
    QString table = text.mid(tableStart, outer - tableStart);
    if (table.isEmpty()) {
        table = containingTableName(book, sheet, at); // "[@Amount]" means the cell's own table
    }
    qint32 tableSheet = -1;
    const TableSpec *spec = table.isEmpty() ? nullptr : tableByName(book, table, &tableSheet);
    if (spec == nullptr) {
        return out;
    }
    QString partial = text.mid(inner + 1, cursor - inner - 1);
    out.from = inner + 1;
    const bool thisRow = partial.startsWith(QLatin1Char('@'));
    if (thisRow) {
        partial = partial.mid(1);
        out.from = inner + 2;
    }
    out.to = cursor;
    const bool nested = inner != outer;
    const QString close = nested ? QStringLiteral("]]") : QStringLiteral("]");
    if (!thisRow && (partial.isEmpty() || partial.startsWith(QLatin1Char('#')))) {
        for (const char *item : {"#All", "#Data", "#Headers", "#Totals", "#This Row"}) {
            const QString label = QString::fromLatin1(item);
            if (startsWithCi(label, partial)) {
                out.entries.append({QStringLiteral("item"), label, QStringLiteral("%1 · special item").arg(spec->name),
                                    label + close});
            }
        }
    }
    const int columns = spec->range.bottomRight.col - spec->range.topLeft.col + 1;
    for (int i = 0; i < columns; ++i) {
        const QString name = columnName(book, tableSheet, *spec, i);
        if (name.isEmpty() || !startsWithCi(name, partial)) {
            continue;
        }
        QString insert = escapedColumn(name);
        if (thisRow && !nested && needsBrackets(name)) {
            insert = QLatin1Char('[') + insert + QLatin1Char(']'); // [@[Unit Price]]
        }
        out.entries.append({QStringLiteral("column"), name, QStringLiteral("%1 · column").arg(spec->name), insert + close});
    }
    return out;
}

void sortAndLimit(QList<Completion> *entries, int limit)
{
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::stable_sort(entries->begin(), entries->end(),
                     [&collator](const Completion &a, const Completion &b) { return collator.compare(a.label, b.label) < 0; });
    if (entries->size() > limit) {
        entries->resize(limit);
    }
}

} // namespace

QString functionSignature(const QString &name)
{
    const QString upper = name.toUpper();
    const QString known = signatures().value(upper);
    if (!known.isEmpty()) {
        return known;
    }
    const auto spec = functionRegistry().constFind(upper);
    if (spec == functionRegistry().constEnd()) {
        return {};
    }
    QStringList args;
    for (int i = 1; i <= spec->minArgs; ++i) {
        args.append(QStringLiteral("value%1").arg(i));
    }
    if (spec->maxArgs > spec->minArgs) {
        args.append(QStringLiteral("[value%1]").arg(spec->minArgs + 1));
        if (spec->maxArgs > spec->minArgs + 1) {
            args.append(QStringLiteral("…"));
        }
    }
    return upper + QLatin1Char('(') + args.join(QStringLiteral(", ")) + QLatin1Char(')');
}

CompletionContext formulaCompletions(const Workbook &book, qint32 sheet, const CellAddr &at, const QString &text,
                                     int cursor, int limit)
{
    CompletionContext out;
    cursor = std::clamp(cursor, 0, int(text.size()));
    if (!text.startsWith(QLatin1Char('=')) || cursor < 2) {
        return out;
    }
    // Where the cursor is: in a string, a quoted sheet name, or brackets.
    bool inString = false;
    bool inQuote = false;
    QList<int> brackets;
    for (int i = 1; i < cursor; ++i) {
        const QChar c = text.at(i);
        if (inString) {
            inString = c != QLatin1Char('"');
        } else if (inQuote) {
            inQuote = c != QLatin1Char('\'');
        } else if (!brackets.isEmpty() && c == QLatin1Char('\'')) {
            ++i; // an escaped character in a structured reference
        } else if (c == QLatin1Char('[')) {
            brackets.append(i);
        } else if (c == QLatin1Char(']') && !brackets.isEmpty()) {
            brackets.removeLast();
        } else if (brackets.isEmpty() && c == QLatin1Char('"')) {
            inString = true;
        } else if (brackets.isEmpty() && c == QLatin1Char('\'')) {
            inQuote = true;
        }
    }
    if (inString || inQuote) {
        return out;
    }
    if (!brackets.isEmpty()) {
        out = columnCompletions(book, sheet, at, text, cursor, int(brackets.first()), int(brackets.last()));
        sortAndLimit(&out.entries, limit);
        return out;
    }
    const int start = nameStart(text, cursor);
    const QString token = text.mid(start, cursor - start);
    if (token.isEmpty() || token.at(0).isDigit()
        || (start > 0 && (text.at(start - 1) == QLatin1Char('!') || text.at(start - 1) == QLatin1Char('$')))) {
        return out;
    }
    out.from = start;
    out.to = cursor;
    for (auto it = functionRegistry().cbegin(); it != functionRegistry().cend(); ++it) {
        if (startsWithCi(it.key(), token) && !kHidden.contains(it.key()) && !it.key().startsWith(QLatin1Char('_'))) {
            out.entries.append({QStringLiteral("function"), it.key(), functionSignature(it.key()), it.key() + QLatin1Char('(')});
        }
    }
    for (const DefinedName &n : book.definedNames()) {
        if ((n.sheet < 0 || n.sheet == sheet) && !n.hidden && !n.name.startsWith(QLatin1String("_xlnm."))
            && startsWithCi(n.name, token)) {
            out.entries.append({QStringLiteral("name"), n.name, QLatin1Char('=') + n.formula, n.name});
        }
    }
    for (qint32 s = 0; s < book.sheetCount(); ++s) {
        for (const TableSpec &t : book.sheetAt(s).tables()) {
            if (startsWithCi(t.name, token)) {
                const QString where = book.sheetName(s) + QLatin1Char('!') + addrText(t.range.topLeft) + QLatin1Char(':')
                                      + addrText(t.range.bottomRight);
                out.entries.append({QStringLiteral("table"), t.name, QStringLiteral("Table · %1").arg(where), t.name});
            }
        }
    }
    sortAndLimit(&out.entries, limit);
    return out;
}

} // namespace QindaOffice::Calc
