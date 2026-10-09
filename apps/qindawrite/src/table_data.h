// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The data commands of Word's Table Layout tab (M70, D-079): Sort, Convert
// to Text, and Insert ▸ Table ▸ Convert Text to Table, as operations on the
// text model. Rows move whole — every cell's paragraphs with their
// formatting, the cells' own formats (shading, borders, the Word markup
// they were read with) and the row's properties — so a sorted Word table
// is written back as Word would write the same rows in a new order.
// Callers group each call into one undo step (they open the edit block).

#include <QString>
#include <QTextCursor>

class QTextTable;

namespace QindaOffice::Apps::Write {

struct SortOptions {
    int column = 0;                       // grid column of the key
    enum Type { Text, Number, Date } type = Text;
    bool descending = false;
    bool header = false;                  // the first row stays where it is
};

// Sorts rows [firstRow, firstRow + rows) of `table` by `options` (stable,
// as Word's is). Returns an empty string, or why it cannot: rows with
// vertically merged cells, differing horizontal merges or nested tables.
QString sortRows(QTextTable *table, int firstRow, int rows, const SortOptions &options);

// The table becomes paragraphs: a row each, its cells' text separated by
// `separator` (a tab by default; a cell's own paragraphs joined by line
// breaks), runs keeping their formatting. The cursor ends where the table
// began.
QTextCursor tableToText(QTextTable *table, QChar separator = QLatin1Char('\t'));

// The selected paragraphs become a table: a row each, cells split at
// `separator` (QChar::ParagraphSeparator: every paragraph is a cell, filled
// `columns` to a row). Returns the table, or null when nothing is selected.
QTextTable *textToTable(QTextCursor &cursor, int columns, QChar separator, qreal contentWidthPx);

// What Convert Text to Table proposes: the most fields one of the selected
// paragraphs has at `separator`.
[[nodiscard]] int fieldCount(const QTextCursor &cursor, QChar separator);

} // namespace QindaOffice::Apps::Write
