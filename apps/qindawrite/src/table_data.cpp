// SPDX-License-Identifier: GPL-3.0-or-later
#include "table_data.h"

#include "docx_table_format.h"
#include "docx_text.h"
#include "write_selection.h"

#include <QCollator>
#include <QDate>
#include <QLocale>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextTable>

#include <algorithm>
#include <numeric>
#include <optional>

namespace QindaOffice::Apps::Write {

namespace {

struct Run {
    QString text;
    QTextCharFormat format;
};
struct Paragraph {
    QTextBlockFormat block;
    QTextCharFormat mark;
    QList<Run> runs;
};
struct CellContent {
    QTextCharFormat format; // the cell's own (shading, borders, Word markup, row properties)
    QList<Paragraph> paragraphs;
    QString plain;
};

QList<Run> runsOf(const QTextBlock &block)
{
    QList<Run> out;
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        out.append({it.fragment().text(), it.fragment().charFormat()});
    }
    return out;
}

CellContent contentOf(const QTextTableCell &cell, bool *nested)
{
    CellContent out;
    out.format = cell.format();
    QStringList lines;
    for (auto it = cell.begin(); !it.atEnd(); ++it) {
        if (it.currentFrame() != nullptr) {
            *nested = true;
            continue;
        }
        const QTextBlock block = it.currentBlock();
        out.paragraphs.append({block.blockFormat(), block.charFormat(), runsOf(block)});
        lines.append(block.text());
    }
    out.plain = lines.join(QLatin1Char(' ')).trimmed();
    return out;
}

void writeRuns(QTextCursor &cursor, const QList<Run> &runs)
{
    for (const Run &run : runs) {
        if (run.format.isImageFormat()) {
            for (qsizetype i = 0; i < run.text.size(); ++i) {
                cursor.insertImage(run.format.toImageFormat());
            }
        } else if (!run.text.isEmpty()) {
            cursor.insertText(run.text, run.format);
        }
    }
}

void fill(QTextTableCell cell, const CellContent &content)
{
    QTextCursor cursor = cell.firstCursorPosition();
    cursor.setPosition(cell.lastCursorPosition().position(), QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    for (qsizetype i = 0; i < content.paragraphs.size(); ++i) {
        const Paragraph &p = content.paragraphs.at(i);
        if (i == 0) {
            cursor.setBlockFormat(p.block);
        } else {
            cursor.insertBlock(p.block, p.mark);
        }
        writeRuns(cursor, p.runs);
    }
    // Last: the cell's format carries its first paragraph's mark as well.
    cell.setFormat(content.format);
}

std::optional<double> numberOf(const QString &text)
{
    QString digits;
    for (const QChar c : text) {
        if (c.isDigit() || c == u'.' || (c == u'-' && digits.isEmpty())) {
            digits.append(c);
        } else if (c == u',' || c.isSpace() || c == u'$' || c == u'€' || c == u'£' || c == u'%') {
            if (c.isSpace() && !digits.isEmpty() && digits != u"-") {
                break;
            }
        } else if (!digits.isEmpty()) {
            break;
        }
    }
    bool ok = false;
    const double value = digits.toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

std::optional<QDate> dateOf(const QString &text)
{
    const QString t = text.trimmed();
    for (const char *format : {"yyyy-MM-dd", "M/d/yyyy", "M/d/yy", "d.M.yyyy", "MMMM d, yyyy", "MMM d, yyyy",
                               "d MMMM yyyy", "d MMM yyyy", "MMMM yyyy"}) {
        const QDate d = QDate::fromString(t, QLatin1String(format));
        if (d.isValid()) {
            return d;
        }
    }
    for (const QLocale::FormatType type : {QLocale::ShortFormat, QLocale::LongFormat}) {
        const QDate d = QLocale().toDate(t, type);
        if (d.isValid()) {
            return d;
        }
    }
    return std::nullopt;
}

} // namespace

QString sortRows(QTextTable *table, int firstRow, int rows, const SortOptions &options)
{
    if (options.header) {
        ++firstRow;
        --rows;
    }
    rows = qMin(rows, table->rows() - firstRow);
    if (rows < 2) {
        return {};
    }
    // Rows move whole: every row must have the same cells.
    QList<int> shape;
    QList<QList<CellContent>> contents;
    bool nested = false;
    for (int r = firstRow; r < firstRow + rows; ++r) {
        QList<int> spans;
        QList<CellContent> cells;
        for (int c = 0; c < table->columns();) {
            const QTextTableCell cell = table->cellAt(r, c);
            if (cell.row() != r || cell.rowSpan() > 1) {
                return QObject::tr("Rows with vertically merged cells cannot be sorted.");
            }
            spans.append(cell.columnSpan());
            cells.append(contentOf(cell, &nested));
            c += qMax(1, cell.columnSpan());
        }
        if (r == firstRow) {
            shape = spans;
        } else if (spans != shape) {
            return QObject::tr("Rows with differently merged cells cannot be sorted.");
        }
        contents.append(cells);
    }
    if (nested) {
        return QObject::tr("Cells holding tables cannot be sorted.");
    }
    // The key column's cell in each row (a merged cell covers its columns).
    int keyIndex = 0;
    for (int c = 0, k = 0; k < shape.size(); c += shape[k], ++k) {
        if (options.column >= c && options.column < c + shape[k]) {
            keyIndex = k;
        }
    }
    QCollator collator(QLocale{});
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    auto less = [&](int a, int b) {
        const QString &x = contents[a][keyIndex].plain;
        const QString &y = contents[b][keyIndex].plain;
        if (options.type == SortOptions::Number) {
            const auto nx = numberOf(x);
            const auto ny = numberOf(y);
            if (nx.has_value() != ny.has_value()) {
                return !nx.has_value(); // what is not a number comes first, as in Word
            }
            return nx.has_value() && *nx < *ny;
        }
        if (options.type == SortOptions::Date) {
            const auto dx = dateOf(x);
            const auto dy = dateOf(y);
            if (dx.has_value() != dy.has_value()) {
                return !dx.has_value();
            }
            return dx.has_value() && *dx < *dy;
        }
        return collator.compare(x, y) < 0;
    };
    QList<int> order(rows);
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return options.descending ? less(b, a) : less(a, b); });
    if (std::is_sorted(order.cbegin(), order.cend())) {
        return {};
    }
    for (int i = 0; i < rows; ++i) {
        if (order[i] == i) {
            continue;
        }
        for (int c = 0, k = 0; k < shape.size(); c += shape[k], ++k) {
            fill(table->cellAt(firstRow + i, c), contents[order[i]][k]);
        }
    }
    return {};
}

QTextCursor tableToText(QTextTable *table, QChar separator)
{
    QTextDocument *document = table->document();
    QList<Paragraph> rows;
    bool nested = false;
    for (int r = 0; r < table->rows(); ++r) {
        Paragraph row;
        bool first = true;
        for (int c = 0; c < table->columns();) {
            const QTextTableCell cell = table->cellAt(r, c);
            c += qMax(1, cell.columnSpan() - (c - cell.column()));
            if (cell.row() != r) {
                continue; // the rest of a vertical merge: its text went with the first row
            }
            const CellContent content = contentOf(cell, &nested);
            if (first && !content.paragraphs.isEmpty()) {
                row.block = content.paragraphs.first().block;
                row.mark = content.paragraphs.first().mark;
            }
            if (!first) {
                row.runs.append({QString(separator), row.runs.isEmpty() ? row.mark : row.runs.last().format});
            }
            for (qsizetype p = 0; p < content.paragraphs.size(); ++p) {
                if (p > 0) {
                    row.runs.append({QString(QChar::LineSeparator), content.paragraphs.at(p).mark});
                }
                row.runs.append(content.paragraphs.at(p).runs);
            }
            first = false;
        }
        rows.append(row);
    }
    QTextCursor cursor(document);
    cursor.setPosition(table->firstPosition() - 1);
    cursor.setPosition(table->lastPosition() + 1, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    const int start = cursor.position();
    const QTextBlockFormat after = cursor.blockFormat();
    const QTextCharFormat afterMark = cursor.block().charFormat();
    for (const Paragraph &row : std::as_const(rows)) {
        cursor.setBlockFormat(row.block);
        Docx::setBlockCharFormatKeepingCell(cursor, row.mark);
        writeRuns(cursor, row.runs);
        cursor.insertBlock(after, afterMark);
    }
    cursor.setPosition(start);
    return cursor;
}

QTextTable *textToTable(QTextCursor &cursor, int columns, QChar separator, qreal contentWidthPx)
{
    if (cursor.currentTable() != nullptr) {
        return nullptr;
    }
    const QList<QTextBlock> blocks = selectedBlocks(cursor);
    if (blocks.isEmpty() || (blocks.size() == 1 && blocks.first().text().isEmpty())) {
        return nullptr;
    }
    // Cells: runs split at the separator (a paragraph a cell for
    // ParagraphSeparator, `columns` to a row).
    QList<QList<Paragraph>> rows;
    QList<Paragraph> pending;
    for (const QTextBlock &block : blocks) {
        QList<Paragraph> cells{Paragraph{block.blockFormat(), block.charFormat(), {}}};
        for (const Run &run : runsOf(block)) {
            const QStringList parts = separator == QChar::ParagraphSeparator ? QStringList{run.text} : run.text.split(separator);
            for (qsizetype i = 0; i < parts.size(); ++i) {
                if (i > 0) {
                    cells.append(Paragraph{block.blockFormat(), block.charFormat(), {}});
                }
                if (!parts.at(i).isEmpty()) {
                    cells.last().runs.append({parts.at(i), run.format});
                }
            }
        }
        if (separator == QChar::ParagraphSeparator) {
            pending.append(cells.first());
            if (pending.size() == qMax(1, columns)) {
                rows.append(pending);
                pending.clear();
            }
        } else {
            rows.append(cells);
        }
    }
    if (!pending.isEmpty()) {
        rows.append(pending);
    }
    int width = qMax(1, columns);
    for (const auto &row : std::as_const(rows)) {
        width = qMax(width, int(row.size()));
    }
    QTextCursor edit(cursor.document());
    edit.setPosition(blocks.first().position());
    edit.setPosition(blocks.last().position() + blocks.last().length() - 1, QTextCursor::KeepAnchor);
    edit.removeSelectedText();
    edit.setBlockFormat(QTextBlockFormat());
    QTextTable *table = edit.insertTable(int(rows.size()), width, Docx::newWordTableFormat(width, contentWidthPx));
    Docx::initWordTable(table);
    for (int r = 0; r < rows.size(); ++r) {
        for (int k = 0; k < rows[r].size(); ++k) {
            QTextCursor cell = table->cellAt(r, k).firstCursorPosition();
            QTextBlockFormat block = rows[r][k].block;
            block.setTopMargin(0); // spacing between the old paragraphs is not the cells'
            block.setBottomMargin(0);
            cell.setBlockFormat(block);
            writeRuns(cell, rows[r][k].runs);
        }
    }
    // Word leaves no empty paragraph behind: the one the text left, which
    // the table went in before, goes — unless a table or the frame's end
    // follows it (a table cannot end a frame).
    QTextCursor after(cursor.document());
    after.setPosition(table->lastPosition() + 1);
    const QTextBlock leftover = after.block();
    const QTextBlock next = leftover.next();
    if (leftover.text().isEmpty() && next.isValid()
        && QTextCursor(next).currentFrame() == QTextCursor(leftover).currentFrame()) {
        const QTextBlockFormat keep = next.blockFormat();
        const QTextCharFormat keepMark = next.charFormat();
        after.setPosition(leftover.position());
        after.deleteChar(); // `next`'s separator: its text joins `leftover`
        after.setBlockFormat(keep);
        Docx::setBlockCharFormatKeepingCell(after, keepMark);
    }
    cursor = table->cellAt(0, 0).firstCursorPosition();
    return table;
}

int fieldCount(const QTextCursor &cursor, QChar separator)
{
    int most = 1;
    for (const QTextBlock &block : selectedBlocks(cursor)) {
        most = qMax(most, int(block.text().count(separator)) + 1);
    }
    return most;
}

} // namespace QindaOffice::Apps::Write
