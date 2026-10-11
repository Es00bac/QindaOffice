// SPDX-License-Identifier: GPL-3.0-or-later
#include "docx_merge.h"

#include "docx_properties.h"
#include "docx_sections.h"
#include "docx_table_format.h"

#include <QAbstractTextDocumentLayout>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextLayout>
#include <QTextTable>

namespace QindaOffice::Docx {

namespace {

// Word's field shading (Highlight Merge Fields): a light grey under the text.
const QColor kFieldShade(0xd9, 0xd9, 0xd9);

struct Span {
    int from = 0;
    int to = 0;
    QString instruction;
    QTextCharFormat format;
};

QList<Span> spansIn(const QTextDocument &document, int start, int stop)
{
    QList<Span> spans;
    for (QTextBlock block = document.findBlock(start); block.isValid() && block.position() < stop;
         block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || fragment.position() >= stop) {
                continue;
            }
            const QString instruction = fragment.charFormat().stringProperty(kFieldInstruction);
            if (instruction.isEmpty() || !isMergeField(instruction)) {
                continue;
            }
            if (!spans.isEmpty() && spans.last().to == fragment.position() && spans.last().instruction == instruction) {
                spans.last().to = fragment.position() + fragment.length();
                continue;
            }
            spans.append({fragment.position(), fragment.position() + fragment.length(), instruction,
                          fragment.charFormat()});
        }
    }
    return spans;
}

// Whether all of `block`'s visible text lies in merge fields whose results
// are empty (`empty` holds those fields' ranges): Word drops such a line.
bool onlyEmptyFields(const QTextBlock &block, const QList<std::pair<int, int>> &empty)
{
    if (block.text().trimmed().isEmpty()) {
        return false; // it was blank before the merge: the author's own
    }
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const QTextFragment fragment = it.fragment();
        const QString text = fragment.text();
        for (int i = 0; i < text.size(); ++i) {
            if (text.at(i).isSpace()) {
                continue;
            }
            const int at = fragment.position() + i;
            bool covered = false;
            for (const auto &[from, to] : empty) {
                covered = covered || (at >= from && at < to);
            }
            if (!covered) {
                return false;
            }
        }
    }
    return true;
}

void removeBlock(QTextDocument *document, const QTextBlock &block)
{
    QTextCursor cursor(document);
    if (block.position() > 0) {
        // Into the paragraph before, which keeps its own format.
        cursor.setPosition(block.position() - 1);
        cursor.setPosition(block.position() + block.length() - 1, QTextCursor::KeepAnchor);
    } else if (block.next().isValid()) {
        cursor.setPosition(0);
        cursor.setPosition(block.next().position(), QTextCursor::KeepAnchor);
    } else {
        return;
    }
    cursor.removeSelectedText();
}

// Replaces the merge fields of [start, stop) with `cursor`'s recipient's
// values; false when a SKIPIF dropped the copy (nothing is replaced then).
bool fillCopy(QTextDocument *out, int start, int stop, MergeCursor &cursor)
{
    const QList<Span> spans = spansIn(*out, start, stop);
    QStringList results;
    for (const Span &span : spans) {
        results.append(evaluateMergeField(span.instruction, cursor));
    }
    if (cursor.skip) {
        return false;
    }
    QList<std::pair<int, int>> empty;
    for (int i = 0; i < spans.size(); ++i) {
        if (results.at(i).isEmpty()) {
            empty.append({spans.at(i).from, spans.at(i).to});
        }
    }
    QList<QTextBlock> blank;
    for (const auto &[from, to] : empty) {
        const QTextBlock block = out->findBlock(from);
        if (!blank.contains(block) && QTextCursor(block).currentTable() == nullptr && onlyEmptyFields(block, empty)) {
            blank.append(block);
        }
    }
    QTextCursor edit(out);
    for (qsizetype i = spans.size() - 1; i >= 0; --i) {
        const Span &span = spans.at(i);
        edit.setPosition(span.from);
        edit.setPosition(span.to, QTextCursor::KeepAnchor);
        QTextCharFormat format = span.format;
        format.clearProperty(kFieldInstruction);
        QString text = results.at(i);
        text.replace(QLatin1Char('\n'), QChar(QChar::LineSeparator));
        if (text.isEmpty()) {
            edit.removeSelectedText();
        } else {
            edit.insertText(text, format);
        }
    }
    for (qsizetype i = blank.size() - 1; i >= 0; --i) {
        if (blank.at(i).isValid() && blank.at(i).text().trimmed().isEmpty()) {
            removeBlock(out, blank.at(i));
        }
    }
    return true;
}

} // namespace

MergeResult mergeDocument(QTextDocument *out, const MergeRequest &request)
{
    MergeResult result;
    if (out == nullptr || request.main == nullptr || request.data == nullptr) {
        return result;
    }
    const QTextDocumentFragment whole(request.main);
    const QTextBlock firstSource = request.main->begin();
    const bool directory = request.kind == u"directory";
    const int last = request.last < 0 ? int(request.order.size()) - 1 : qMin(request.last, int(request.order.size()) - 1);
    MergeCursor cursor;
    cursor.data = request.data;
    cursor.order = request.order;
    cursor.fieldMap = request.fieldMap;
    int position = qMax(0, request.first);
    int previousEnd = -1; // the last block of the copy before, while another may follow
    while (position <= last) {
        cursor.position = position;
        cursor.skip = false;
        cursor.exhausted = false;
        cursor.sequence = result.copies + 1;
        QTextCursor end(out);
        end.movePosition(QTextCursor::End);
        const bool firstCopy = out->isEmpty();
        const int separator = end.position();
        if (!firstCopy) {
            end.insertBlock(firstSource.blockFormat(), firstSource.charFormat());
        }
        const int start = end.position();
        end.insertFragment(whole);
        const int stop = out->characterCount() - 1;
        if (!fillCopy(out, start, stop, cursor)) {
            result.skipped.append(request.order.at(position));
            QTextCursor drop(out);
            drop.setPosition(firstCopy ? 0 : separator);
            drop.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
            drop.removeSelectedText();
            ++position;
            continue;
        }
        if (previousEnd >= 0 && !directory) {
            // The copy before ends its section: each record starts a page.
            QTextCursor mark(out->findBlock(previousEnd));
            QTextBlockFormat format = mark.blockFormat();
            if (!request.sectionXml.isEmpty()) {
                // A section of its own (M88, docx_sections.h); the break
                // flag shows the merged copy paged where no section
                // layout runs (Preview Results, Merge to PDF).
                markSectionEnd(format, QString::fromUtf16(kMergeSectionKey), request.sectionXml);
            }
            format.setPageBreakPolicy(format.pageBreakPolicy() | QTextFormat::PageBreak_AlwaysAfter);
            mark.setBlockFormat(format);
            if (request.sectionXml.isEmpty()) {
                QTextCursor next(out->findBlock(start));
                QTextBlockFormat nextFormat = next.blockFormat();
                nextFormat.setPageBreakPolicy(nextFormat.pageBreakPolicy() | QTextFormat::PageBreak_AlwaysBefore);
                nextFormat.setProperty(kPageBreakRun, true);
                next.setBlockFormat(nextFormat);
            }
        }
        const int lastUsed = cursor.exhausted ? last : cursor.position;
        for (int p = position; p <= lastUsed && p < request.order.size(); ++p) {
            result.records.append(request.order.at(p));
        }
        ++result.copies;
        previousEnd = out->characterCount() - 1;
        position = cursor.exhausted ? last + 1 : cursor.position + 1;
        if (request.singleCopy) {
            break;
        }
    }
    if (result.copies > 0) {
        refreshRowHeights(out); // label rows padded for the addresses now in them
    }
    return result;
}

QList<MergeFieldSpan> mergeFieldSpans(const QTextDocument &document)
{
    QList<MergeFieldSpan> out;
    for (const Span &span : spansIn(document, 0, document.characterCount())) {
        out.append({span.from, span.to - span.from, span.instruction});
    }
    return out;
}

QStringList missingMergeColumns(const QTextDocument &document, const MergeData &data)
{
    QStringList missing;
    for (const MergeFieldSpan &span : mergeFieldSpans(document)) {
        for (const QString &column : mergeFieldColumns(span.instruction)) {
            if (mergeColumnIndex(data, column) < 0 && !missing.contains(column)) {
                missing.append(column);
            }
        }
    }
    return missing;
}

QTextCharFormat insertMergeField(QTextCursor &cursor, const QString &instruction, const QString &display)
{
    QTextCharFormat plain = cursor.charFormat();
    plain.clearProperty(kFieldInstruction);
    plain.clearProperty(kRunSnapshot);
    plain.clearProperty(kRunXml);
    QTextCharFormat field = plain;
    field.setProperty(kFieldInstruction, instruction);
    QString text = display.isEmpty() ? mergeFieldLabel(instruction) : display;
    text.replace(QLatin1Char('\n'), QChar(QChar::LineSeparator));
    cursor.insertText(text, field);
    cursor.setCharFormat(plain);
    return plain;
}

void drawMergeFieldShading(QPainter *painter, const QTextDocument &document, const QRectF &clip)
{
    QAbstractTextDocumentLayout *layout = document.documentLayout();
    painter->save();
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const QRectF box = layout->blockBoundingRect(block);
        const QTextLayout *lines = block.layout();
        if (lines == nullptr || !box.intersects(clip)) {
            continue;
        }
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid() || !isMergeField(fragment.charFormat().stringProperty(kFieldInstruction))) {
                continue;
            }
            const int from = fragment.position() - block.position();
            const int to = from + fragment.length();
            for (int i = 0; i < lines->lineCount(); ++i) {
                const QTextLine line = lines->lineAt(i);
                const int a = qMax(from, line.textStart());
                const int b = qMin(to, line.textStart() + line.textLength());
                if (a >= b) {
                    continue;
                }
                const qreal x1 = line.cursorToX(a);
                const qreal x2 = line.cursorToX(b);
                painter->fillRect(QRectF(box.left() + x1, box.top() + line.y(), x2 - x1, line.height()), kFieldShade);
            }
        }
    }
    painter->restore();
}

} // namespace QindaOffice::Docx
